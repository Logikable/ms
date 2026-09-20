#include "src/character/character_stats.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/item/equip_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Slack for the floor below, far smaller than any percentage a skill grants.
constexpr double kPercentEpsilon = 1e-9;

// Hyper Stats are stated in whole percents, as a worn item's levers are.
constexpr double kPercentToFraction = 100.0;

// DEF every character carries for their primary stats, before anything is
// worn: 1.5 for each point of STR and 0.4 for each point of DEX and of LUK.
// INT buys none -- a magician's bulk comes from elsewhere.
constexpr double kDefPerStr = 1.5;
constexpr double kDefPerDexLuk = 0.4;

// Whether a list of weapon types admits `weapon`. An empty list admits every
// weapon, which is what a skill naming none means.
bool ListAllowsWeapon(const google::protobuf::RepeatedField<int>& types,
                      EquipType weapon) {
  if (types.empty()) {
    return true;
  }
  for (int type : types) {
    if (type == weapon) {
      return true;
    }
  }
  return false;
}

// Whether this skill's timed buff stands over the party as well as over the
// caster. Smokescreen alone. See Buff.ally_base.
bool GrantsBuffToAllies(const Skill& skill) {
  return LongestBuffDuration(skill.buff()) > 0.0 &&
         (skill.buff().has_ally_base() || skill.buff().has_ally_per_level());
}

// Whether this is the skill that raises other skills' levels. Asked of the
// data rather than of the name, so the rule that it does not raise itself
// holds for any later skill written the same way.
bool GrantsSkillLevels(const Skill& skill) {
  return skill.base().skill_level_bonus() > 0.0;
}

// Whether a granted level reaches this skill. GMS's Combat Orders names its
// exceptions: beginner, hyper and 5th job skills stay where they stand.
bool TakesGrantedLevels(const Skill& skill) {
  return !skill.hyper() && skill.v_node() == V_NODE_KIND_UNSPECIFIED &&
         skill.account_levels_per_level() == 0;
}

// Whether this skill gives the rest of the party anything -- for good, or for
// as long as its buff stands. See Skill.ally_base and Buff.ally_base.
bool GrantsToAllies(const Skill& skill) {
  return skill.has_ally_base() || skill.has_ally_per_level() ||
         GrantsBuffToAllies(skill);
}

// A fountain as its skill wrote it, before the character's INT has its say.
// The step is the INT that buys one more helping; kept here because the total
// INT is not known until every passive has been read.
struct RawRegen {
  RegenPulse pulse;
  double int_step = 0.0;
};

// What the learned passives come to as they are summed. It IS a DerivedStats,
// and the fields it shares are the same field -- the fold at the end of
// DerivedStatsFor transforms only what it must. What is added here is the
// pre-fold working: flat grants that have yet to meet the allocation, and
// counts worth nothing until every passive is read.
//
// The five inherited fields the fold writes from scratch -- max_hp, max_mp,
// def, base_def and regen_pulses -- stay at nothing meanwhile.
struct PassiveTotals : DerivedStats {
  // The flat HP, MP and DEF the passives grant, held apart from the totals
  // above them because each has an allocation and a worn share still to meet.
  int hp_grant = 0;
  int mp_grant = 0;
  int def_grant = 0;
  int hp_per_level = 0;
  double max_hp_pct = 0.0;
  int mp_per_level = 0;
  double max_mp_pct = 0.0;
  // Held as the factor the DEF pile is multiplied by rather than as a sum of
  // percentages, because two sources multiply: Phoenix's +30% and Reckless
  // Hunt's -25% leave 97.5% of the armour, not 105% of it.
  double def_factor = 1.0;
  // The stats the passives grant, which become skill_stats once they are read.
  int str = 0;
  int dex = 0;
  int int_ = 0;
  int luk = 0;
  int attack = 0;
  int magic_attack = 0;
  // One entry per skill granting a fountain, in catalog order. Becomes
  // regen_pulses once the character's total INT is known.
  std::vector<RawRegen> regen;
  // Stacks a buff adds to that cap while it stands. Held apart until
  // FoldFreezeStacks, which only deepens a pile the character already has.
  int freeze_cap_bonus = 0;
  // Pick Pocket's chance and Meso Explosion's damage, which live on two
  // different skills and are worth nothing apart -- totalled here and paired
  // once the fold is done.
  double meso_drop_chance = 0.0;
  // Per line until FoldMesoExplosion multiplies the count in.
  double meso_hit_pct = 0.0;
  int meso_lines = 1;
  // Boss damage, plain damage, ignored defence, critical rate and final damage
  // a thrown meso carries, once FoldMesoExplosion has cashed in what the skills
  // naming Meso Explosion granted it.
  double meso_boss_pct = 0.0;
  double meso_damage_pct = 0.0;
  // Percentage points a thrown meso adds against anything that is not a boss.
  // Per line until FoldMesoExplosion multiplies the count in, as the damage
  // above is: GMS states it per shot.
  double meso_normal_skill_pct = 0.0;
  double meso_ied = 0.0;
  double meso_crit_rate = 0.0;
  double meso_final_dmg_pct = 0.0;
  std::string meso_skill;
  // What the book takes off the shortest revival wait. Summed apart and
  // subtracted once that shortest is known.
  double revive_cooldown_cut = 0.0;
  // Share of what AP bought that comes back as flat stat. Summed, and cashed
  // in against the allocation once every passive is read -- see
  // DerivedStatsFor.
  double ap_stat_pct = 0.0;
  // Share added to the one above before it is cashed in, so what the character
  // gets back is Maple Warrior's own grant multiplied. Summed, as every share
  // here is.
  double ap_stat_bonus_pct = 0.0;
  // Combo Orbs and the bargains priced per orb. The count is the BEST any
  // passive grants, a character carrying one ring however many skills describe
  // it, and the bargains fold against it only once every passive is read.
  int combo_orbs = 0;
  int attack_per_combo_orb = 0;
  double final_dmg_pct_per_combo_orb = 0.0;
  double boss_pct_per_combo_orb = 0.0;
  int def_per_combo_orb = 0;
  // Share added to all four of those at once, and to nothing else.
  double combo_orb_gain_pct = 0.0;
  // Orbs' worth of final damage granted beside the ring, which only the final
  // damage above reads. Added to the count rather than multiplied against it,
  // GMS's per-orb final damage being additive with itself.
  double final_dmg_combo_orbs = 0.0;
};

// Folds one skill's levers in on top of what is there, handed the grant
// already read up to its level. Split out from AddPassive because a weapon
// bonus is a second helping of the same levers, gated on the weapon.
// Every lever that is simply a sum, which is most of them.
void AddSummedLevers(const SkillEffect& granted, PassiveTotals& totals) {
  totals.hp_grant += WholeValue(granted.max_hp());
  totals.mp_grant += WholeValue(granted.max_mp());
  totals.hp_per_level += WholeValue(granted.max_hp_per_level());
  totals.max_hp_pct += granted.max_hp_pct();
  totals.mp_per_level += WholeValue(granted.max_mp_per_level());
  totals.max_mp_pct += granted.max_mp_pct();
  totals.def_grant += WholeValue(granted.def());
  totals.str += WholeValue(granted.str());
  totals.dex += WholeValue(granted.dex());
  totals.int_ += WholeValue(granted.int_());
  totals.luk += WholeValue(granted.luk());
  totals.attack += WholeValue(granted.attack());
  totals.magic_attack += WholeValue(granted.magic_attack());
  totals.damage_reflect_pct += granted.damage_reflect_pct();
  totals.crit_rate += granted.crit_rate();
  totals.crit_dmg_per_crit_rate += granted.crit_dmg_per_crit_rate();
  totals.crit_dmg += granted.crit_dmg();
  totals.hp_recover_pct += granted.hp_recover_pct();
  totals.exp_pct += granted.exp_pct();
  totals.elemental_resistance += granted.elemental_resistance();
  totals.damage_pct += granted.damage_pct();
  totals.boss_pct += granted.boss_pct();
  totals.normal_pct += granted.normal_pct();
  totals.meso_pct += granted.meso_pct();
  totals.item_drop_pct += granted.item_drop_pct();
  totals.buff_duration_pct += granted.buff_duration_pct();
  totals.meso_drop_chance += granted.meso_drop_chance();
  totals.mirror_line_pct += granted.mirror_line_pct();
  totals.bonus_attack_lines += WholeValue(granted.bonus_attack_lines());
  totals.attack_per_combo_orb += WholeValue(granted.attack_per_combo_orb());
  totals.final_dmg_pct_per_combo_orb += granted.final_dmg_pct_per_combo_orb();
  totals.boss_pct_per_combo_orb += granted.boss_pct_per_combo_orb();
  totals.def_per_combo_orb += WholeValue(granted.def_per_combo_orb());
  totals.combo_orb_gain_pct += granted.combo_orb_gain_pct();
  totals.final_dmg_combo_orbs += granted.final_dmg_combo_orbs();
  totals.ap_stat_pct += granted.ap_stat_pct();
  totals.ap_stat_bonus_pct += granted.ap_stat_bonus_pct();
  totals.freeze.matt_per_stack +=
      WholeValue(granted.magic_attack_per_freeze_stack());
  totals.attack_speed_bonus += WholeValue(granted.attack_speed());
  totals.uncapped_attack_speed_bonus +=
      WholeValue(granted.uncapped_attack_speed());
}

// Folds one level's levers into the running totals. The sums are next door;
// what is here is every lever that combines some OTHER way, and the comment
// on each says which.
void AddEffect(const SkillEffect& granted, PassiveTotals& totals) {
  AddSummedLevers(granted, totals);
  totals.def_factor *= 1.0 + granted.def_pct();
  // One lever on a skill pays both attacks; only a potential tells them apart.
  totals.attack_pct += granted.attack_pct();
  totals.magic_attack_pct += granted.attack_pct();
  // Damage sent to MP is damage the HP pool never sees and nothing tracks MP,
  // so Magic Guard reads as reduction. Reduction MULTIPLIES: two halves leave
  // a quarter, where summing would leave none and then heal the character.
  totals.damage_taken_pct = 1.0 - (1.0 - totals.damage_taken_pct) *
                                      (1.0 - granted.damage_taken_pct()) *
                                      (1.0 - granted.damage_to_mp_pct());
  // Dodging combines the same way and for the same reason: what two sources
  // leave standing is the product of what each leaves standing.
  totals.dodge_chance =
      1.0 - (1.0 - totals.dodge_chance) * (1.0 - granted.dodge_chance());
  // The barrier sums rather than combining, unlike the two above: what it takes
  // off is the monster's own attack, and GMS states every source of it as
  // points on that one number.
  totals.enemy_attack_pct += granted.enemy_attack_pct();
  totals.enemy_attack_reaches_boss |= granted.enemy_attack_reaches_boss();
  // The pulse and its interval stay apart all the way to the fight, which
  // pours on the clock rather than smearing it over the seconds between.
  if (granted.regen_interval_seconds() > 0.0 &&
      (granted.regen_pct() > 0.0 || granted.regen_hp() > 0.0)) {
    totals.regen.push_back(
        RawRegen{{granted.regen_pct(), WholeValue(granted.regen_hp()),
                  granted.regen_interval_seconds()},
                 granted.regen_int_step()});
  }
  totals.status_resistance += granted.status_resistance();
  // Read here rather than beside the cap itself, so that a BUFF granting
  // either lands them: a buff folds in through this door alone.
  totals.freeze_cap_bonus += WholeValue(granted.freeze_stack_cap_bonus());
  // The shortest wait rather than the sum: two pacts are not one long one,
  // and what a character wants to know is how soon the next one comes.
  double revive = granted.revive_cooldown_seconds();
  if (revive > 0.0 && (totals.revive_cooldown_seconds <= 0.0 ||
                       revive < totals.revive_cooldown_seconds)) {
    totals.revive_cooldown_seconds = revive;
  }
  // What comes OFF that wait sums, unlike the wait itself: it is a quantity of
  // seconds rather than a choice between clocks. Cashed in once every passive
  // is read -- see DerivedStatsFor.
  totals.revive_cooldown_cut += granted.revive_cooldown_cut_seconds();
  // The same bargain, over the heal that answers nearly dying: the shorter
  // wait stands whole rather than two of them adding up.
  if (granted.emergency_heal_pct() > 0.0 &&
      (totals.emergency_heal.pct <= 0.0 ||
       granted.emergency_heal_cooldown_seconds() <
           totals.emergency_heal.cooldown_seconds)) {
    totals.emergency_heal = {granted.emergency_heal_pct(),
                             granted.emergency_heal_seconds(),
                             granted.emergency_heal_hp_threshold(),
                             granted.emergency_heal_cooldown_seconds()};
  }
  totals.ied = CombineIgnoredDefense(totals.ied, granted.ied_pct());
  // Its elemental twin, which sums rather than combining in reverse: GMS
  // applies it to the resistance itself, not to what the last source left.
  totals.ier += granted.ier_pct();
  // The one lever taken at its best rather than summed: two masteries are not
  // twice as steady a swing, they are the better of the two.
  totals.mastery = std::max(totals.mastery, granted.mastery());
  // The enemy's condition folds the same way: an afflicted monster is
  // afflicted, and one skill raising another's rate is one rate. Here rather
  // than beside the count, a SkillEffect being all an ALLY hands over.
  totals.condition.final_dmg_pct_when_afflicted =
      std::max(totals.condition.final_dmg_pct_when_afflicted,
               granted.final_dmg_pct_when_afflicted());
  totals.condition.final_dmg_pct_per_dot = std::max(
      totals.condition.final_dmg_pct_per_dot, granted.final_dmg_pct_per_dot());
  // Final damage is the one that multiplies: two sources of 10% are worth 21%.
  // Kept as the combined fraction, since that is the single number the damage
  // chain applies.
  totals.final_dmg_pct =
      (1.0 + totals.final_dmg_pct) * (1.0 + granted.final_dmg_pct()) - 1.0;
}

// Folds one skill's Final Attack in. Split from AddEffect because what sets a
// Final Attack off belongs to the SKILL, not to the level's levers -- and
// AddEffect is handed levers with no skill behind them.
void AddFinalAttack(const Skill& skill, const SkillEffect& granted,
                    PassiveTotals& totals) {
  FinalAttackSource source;
  source.chance = granted.final_attack_chance();
  source.damage_pct = granted.final_attack_pct();
  if (source.chance <= 0.0 || source.damage_pct <= 0.0) {
    return;
  }
  source.lines = std::max(1, WholeValue(granted.final_attack_lines()));
  source.required_tag = skill.follows_skill_tag();
  source.max_enemies = skill.final_attack_max_enemies();
  source.follows_own_clock = skill.follows_own_clock();
  source.skill_name = skill.name();
  source.owner_swings = DealsDamage(skill.kind());
  totals.final_attacks.push_back(source);
}

// Folds one skill's chance to land harder on a single enemy in. Split from
// AddEffect for the reason AddFinalAttack is: what is rolled belongs to the
// skill rather than to the level's levers.
void AddProc(const Skill& skill, int level, PassiveTotals& totals) {
  const Proc& proc = skill.proc();
  SwingProc rolled;
  rolled.chance = proc.chance() + proc.chance_per_level() * (level - 1);
  if (rolled.chance <= 0.0) {
    return;
  }
  SkillEffect lands = EffectAt(proc.base(), proc.per_level(), level);
  rolled.damage_pct = lands.damage_pct();
  rolled.hp_recover_pct = lands.hp_recover_pct();
  totals.procs.push_back(rolled);
}

// Folds Freezing Crush in. Two skills granting it leave the DEEPER pile and
// the better stack rather than summing, which is what lets Frost Clutch better
// a stack without naming a pile of its own.
void AddFreezeStacks(const Skill& skill, const SkillEffect& granted,
                     PassiveTotals& totals) {
  totals.freeze.cap = std::max(totals.freeze.cap, skill.freeze_stack_cap());
  totals.freeze.crit_dmg_per_stack = std::max(
      totals.freeze.crit_dmg_per_stack, granted.crit_dmg_per_freeze_stack());
  totals.freeze.final_dmg_pct_per_stack =
      std::max(totals.freeze.final_dmg_pct_per_stack,
               granted.final_dmg_pct_per_freeze_stack());
  totals.freeze.ied_pct_per_stack = std::max(
      totals.freeze.ied_pct_per_stack, granted.ied_pct_per_freeze_stack());
  // The stun's lift is not a pile and does not stack: one skill leaves it and
  // names the swings that take it. A second would be a second status.
  if (skill.stun().final_dmg_pct() > 0.0 &&
      skill.stun().lifted_tag() != SKILL_TAG_UNSPECIFIED) {
    totals.stun_lift.lifted_tag = skill.stun().lifted_tag();
    totals.stun_lift.from_skill = skill.name();
  }
  // The mark's, on the same footing and for the same reason.
  if (skill.mark().final_dmg_pct() > 0.0 &&
      skill.mark().lifted_tag() != SKILL_TAG_UNSPECIFIED) {
    totals.mark_lift.lifted_tag = skill.mark().lifted_tag();
  }
}

// Folds the scar in. Two sources would leave the better of each standing
// rather than summing, exactly as the freeze does: a deeper scar is one scar,
// and a second skill restating it says nothing new.
void AddScar(const SkillEffect& granted, PassiveTotals& totals) {
  totals.scar.chance = std::max(totals.scar.chance, granted.scar_chance());
  totals.scar.seconds = std::max(totals.scar.seconds, granted.scar_seconds());
  totals.scar.final_dmg_pct =
      std::max(totals.scar.final_dmg_pct, granted.final_dmg_pct_when_scarred());
  totals.scar.enemy_attack_pct = std::max(
      totals.scar.enemy_attack_pct, granted.enemy_attack_pct_when_scarred());
}

// How many burns a skill paying per burn will count. The rates themselves fold
// in AddEffect with every other lever; only the cap is the Skill's own.
void AddDotCount(const Skill& skill, PassiveTotals& totals) {
  totals.condition.dot_count_cap =
      std::max(totals.condition.dot_count_cap, skill.dot_count_cap());
}

// Notes Meso Explosion down. RECORDED rather than folded: Meso Mastery's
// points land on each of its lines and the two fold in catalog order, so the
// pair waits until every passive is in.
void AddMesoExplosion(const Skill& skill, const SkillEffect& granted, int level,
                      PassiveTotals& totals) {
  double per_line = granted.meso_hit_pct();
  if (per_line <= 0.0) {
    return;
  }
  totals.meso_skill = skill.name();
  totals.meso_hit_pct += per_line;
  totals.meso_normal_skill_pct += granted.normal_skill_pct();
  totals.meso_lines = SkillLinesAt(skill, level);
}

// One boost's levers, summed into the entry of whichever skill is collecting
// them -- each meeting what is already there the way two sources of it always
// meet.
void AddSkillBonus(const SkillBoost& boost, int level, SkillBonus& into) {
  SkillEffect aimed = EffectAt(boost.effect(), boost.effect_per_level(), level);
  into.skill_pct += aimed.skill_pct();
  into.damage_pct += aimed.damage_pct();
  into.boss_pct += aimed.boss_pct();
  into.normal_pct += aimed.normal_pct();
  into.crit_rate += aimed.crit_rate();
  // The two that do not sum, for the reason they never do.
  into.ied = CombineIgnoredDefense(into.ied, aimed.ied_pct());
  into.final_dmg_pct =
      (1.0 + into.final_dmg_pct) * (1.0 + aimed.final_dmg_pct()) - 1.0;
  into.final_attack_chance += aimed.final_attack_chance();
  // Its own field rather than one of the seven, because it is aimed at the
  // mark the skill leaves rather than the swing -- see SkillBoost.
  into.dot_skill_pct +=
      boost.dot_skill_pct() + boost.dot_skill_pct_per_level() * (level - 1);
  into.dot_duration_seconds +=
      boost.dot_duration_seconds() +
      boost.dot_duration_seconds_per_level() * (level - 1);
  // Multiplies rather than sums, so two of them would compound. Nothing grants
  // a second one; the shape is what the lever means.
  if (boost.final_attack_chance_mult() > 0.0) {
    into.final_attack_chance_mult *= boost.final_attack_chance_mult();
  }
  // The hits it hands that skill, each cashed in at the GRANTING skill's level
  // on the way: the swing they join is read at its own, and a ladder left on
  // them would be climbed a second time there.
  for (const SwingHit& hit : boost.extra_hit()) {
    SwingHit landed = hit;
    *landed.mutable_base() = EffectAt(hit.base(), hit.per_level(), level);
    landed.clear_per_level();
    into.extra_hit.push_back(std::move(landed));
  }
}

// Notes what one list of boosts hands other skills by name. Kept out of
// AddEffect, which sees levers with no skill behind them: which skill is
// strengthened is written on the BOOST. A skill's own list and its buff's both
// come here, differing only in which fold reads them.
void AddSkillBonuses(
    const google::protobuf::RepeatedPtrField<SkillBoost>& boosts, int level,
    PassiveTotals& totals) {
  for (const SkillBoost& boost : boosts) {
    // Nothing until the granting skill reaches the level the gift is gated
    // behind -- see SkillBoost.min_level.
    if (level < boost.min_level()) {
      continue;
    }
    // The form is a swing of its own and the fight looks its entry up by the
    // name it swings under, so a grant reaching it is filed there too.
    for (const std::string& name : BoostTargetNames(boost)) {
      AddSkillBonus(boost, level, totals.skill_bonus[name]);
    }
  }
}

// Notes the wound a skill hands the swings that leave it. Read off the skill
// that STATES it, as a boost is: Assassinate and Sonic Blow mention none, and
// neither leaves one until Trickblade is bought.
void AddWoundSources(const Skill& skill, PassiveTotals& totals) {
  const Wound& wound = skill.wound();
  if (wound.max_stacks() <= 0) {
    return;
  }
  for (const Wound::Source& source : wound.source()) {
    SkillBonus& into = totals.skill_bonus[source.skill_name()];
    into.wound_stacks += source.stacks();
    into.wound_max_stacks = wound.max_stacks();
    into.wound_seconds = wound.duration_seconds();
  }
}

// The best each exclusive group pays for each lever, and who pays it. Built
// over everything the character and party hold BEFORE anything folds: a group
// is settled between its members, not as each arrives.
class ExclusiveBest {
 public:
  void Consider(const Skill& skill, const SkillEffect& effect) {
    if (skill.exclusive_group().empty()) {
      return;
    }
    std::map<int, Claim>& group = best_[skill.exclusive_group()];
    std::vector<const google::protobuf::FieldDescriptor*> paid;
    effect.GetReflection()->ListFields(effect, &paid);
    for (const google::protobuf::FieldDescriptor* field : paid) {
      if (field->cpp_type() !=
          google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
        continue;
      }
      double value = effect.GetReflection()->GetDouble(effect, field);
      Claim& claim = group[field->number()];
      // A tie goes to whichever was read first -- the catalog's own order --
      // so two members paying the same pay once between them.
      if (claim.source.empty() || value > claim.value) {
        claim = Claim{value, skill.name()};
      }
    }
  }

  // `effect` with every lever another member of its group pays more of taken
  // out. Unchanged for a skill in no group, which is nearly every skill.
  SkillEffect Thin(const Skill& skill, const SkillEffect& effect) const {
    std::map<std::string, std::map<int, Claim>>::const_iterator group =
        best_.find(skill.exclusive_group());
    if (skill.exclusive_group().empty() || group == best_.end()) {
      return effect;
    }
    SkillEffect thinned = effect;
    std::vector<const google::protobuf::FieldDescriptor*> paid;
    thinned.GetReflection()->ListFields(thinned, &paid);
    for (const google::protobuf::FieldDescriptor* field : paid) {
      std::map<int, Claim>::const_iterator claim =
          group->second.find(field->number());
      if (claim != group->second.end() &&
          claim->second.source != skill.name()) {
        thinned.GetReflection()->ClearField(&thinned, field);
      }
    }
    return thinned;
  }

 private:
  struct Claim {
    double value = 0.0;
    std::string source;
  };
  // Group, then the lever's field number: what it pays and who pays it.
  std::map<std::string, std::map<int, Claim>> best_;
};

void AddPassive(const Skill& skill, int level, EquipType weapon,
                const ExclusiveBest& exclusive, PassiveTotals& totals) {
  SkillEffect granted =
      exclusive.Thin(skill, EffectAt(skill.base(), skill.per_level(), level));
  if (DealsDamage(skill.kind())) {
    AddEffect(WithoutSwingLevers(granted), totals);
    // The half an attack states apart because it keeps it: no lever of this
    // one leaves with the swing. See Skill.passive.
    AddEffect(EffectAt(skill.passive(), skill.passive_per_level(), level),
              totals);
  } else {
    AddEffect(granted, totals);
  }
  AddSkillBonuses(skill.boost(), level, totals);
  AddWoundSources(skill, totals);
  AddFinalAttack(skill, granted, totals);
  AddProc(skill, level, totals);
  AddFreezeStacks(skill, granted, totals);
  AddScar(granted, totals);
  AddDotCount(skill, totals);
  // A burn on a PASSIVE belongs to the character: the poison stays on the
  // claw, so everything it hits takes it. One on an attack or a summon is read
  // where those are built.
  if (skill.kind() == SKILL_KIND_PASSIVE &&
      skill.dot().interval_seconds() > 0.0) {
    totals.dots.push_back(CharacterDot{skill.dot(), level});
  }
  AddMesoExplosion(skill, granted, level, totals);
  totals.combo_orbs = std::max(totals.combo_orbs, ComboOrbsAt(skill, level));
  // A weapon bonus is a second helping of the same levers for a subset of the
  // weapons the skill accepts. Read at level 1: it is flat by construction.
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    if (bonus.required_equip_type_size() > 0 &&
        ListAllowsWeapon(bonus.required_equip_type(), weapon)) {
      AddEffect(bonus.effect(), totals);
      AddFinalAttack(skill, bonus.effect(), totals);
    }
  }
}

// Turns what one LINE of a thrown meso is worth into what a whole one is, Meso
// Mastery's points now being certain to be in. Meso Explosion is not a swing,
// so what the book aims at it is cashed in here.
void FoldMesoExplosion(PassiveTotals& totals) {
  if (totals.meso_hit_pct <= 0.0) {
    return;
  }
  std::map<std::string, SkillBonus>::const_iterator boost =
      totals.skill_bonus.find(totals.meso_skill);
  if (boost != totals.skill_bonus.end()) {
    totals.meso_hit_pct += boost->second.skill_pct;
    totals.meso_boss_pct = boost->second.boss_pct;
    totals.meso_damage_pct = boost->second.damage_pct;
    totals.meso_ied = boost->second.ied;
    totals.meso_crit_rate = boost->second.crit_rate;
    totals.meso_final_dmg_pct = boost->second.final_dmg_pct;
  }
  totals.meso_hit_pct *= totals.meso_lines;
  totals.meso_normal_skill_pct *= totals.meso_lines;
}

// Hands each Final Attack what the book aimed at the skill setting it off.
// Here rather than where the source is built: the granting skill may be read
// after the one carrying the Final Attack.
void FoldFinalAttackBoosts(PassiveTotals& totals) {
  for (FinalAttackSource& source : totals.final_attacks) {
    std::map<std::string, SkillBonus>::const_iterator boost =
        totals.skill_bonus.find(source.skill_name);
    if (source.skill_name.empty() || boost == totals.skill_bonus.end()) {
      continue;
    }
    // Scaled last, so a doubling reads every additive source that came before
    // it. Past certainty is allowed: RolledFinalAttack lands that many hits
    // outright and rolls for the remainder.
    source.chance = (source.chance + boost->second.final_attack_chance) *
                    boost->second.final_attack_chance_mult;
    source.damage_bonus_pct += boost->second.damage_pct;
    source.crit_rate += boost->second.crit_rate;
    source.ied = CombineIgnoredDefense(source.ied, boost->second.ied);
    source.final_dmg_pct =
        (1.0 + source.final_dmg_pct) * (1.0 + boost->second.final_dmg_pct) -
        1.0;
    // Points on the strike's own multiplier -- GMS's "Night Lord's Mark
    // Damage: +100% points". Only where the carrying skill does not SWING: the
    // points already land on a swing of that name.
    if (!source.owner_swings) {
      source.damage_pct += boost->second.skill_pct;
    }
  }
}

// Settles the scar. Nothing to leave one with means nothing to read one for:
// a character carrying Chance Attack and no Scarring Sword is holding half a
// mechanism, and half of it is worth nothing at all.
void FoldScar(PassiveTotals& totals) {
  if (totals.scar.chance <= 0.0 || totals.scar.seconds <= 0.0) {
    totals.scar = Scar{};
  }
}

// Settles the drain, on the same rule: a rate per burn with nothing counting
// them pays for nothing.
void FoldEnemyCondition(PassiveTotals& totals) {
  if (totals.condition.dot_count_cap <= 0) {
    totals.condition.final_dmg_pct_per_dot = 0.0;
  }
}

// Settles the pile. A cap raised by a buff is Freezing Crush's pile grown
// deeper, so it pays only where there IS a pile.
void FoldFreezeStacks(PassiveTotals& totals) {
  if (totals.freeze.cap <= 0) {
    totals.freeze = FreezeStacks{};
    return;
  }
  totals.freeze.cap += totals.freeze_cap_bonus;
}

void FoldComboOrbs(PassiveTotals& totals) {
  // The ring is worth its count of orbs, and a gain lifts the whole ring --
  // so it multiplies here, once, rather than each lever where it is read.
  double ring = totals.combo_orbs * (1.0 + totals.combo_orb_gain_pct);
  totals.attack += WholeValue(totals.attack_per_combo_orb * ring);
  totals.boss_pct += totals.boss_pct_per_combo_orb * ring;
  totals.def_grant += WholeValue(totals.def_per_combo_orb * ring);
  // Orbs granted for their final damage alone join the ring for that lever and
  // no other, and are lifted by a gain as the rest of the ring is: what Sword
  // Illusion detonates is six Combo Orbs, whatever they are worth today.
  double lit = (totals.combo_orbs + totals.final_dmg_combo_orbs) *
               (1.0 + totals.combo_orb_gain_pct);
  double orbs = totals.final_dmg_pct_per_combo_orb * lit;
  totals.final_dmg_pct = (1.0 + totals.final_dmg_pct) * (1.0 + orbs) - 1.0;
}

// Whether `skill` puts up a timed buff -- one the character has for a while
// rather than for good. See Skill.buff.
bool GrantsBuff(const Skill& skill) {
  return LongestBuffDuration(skill.buff()) > 0.0;
}

// Whether this character reads anything off `skill`: their own book, the gear
// it demands in hand, and a level in it. Asked twice -- to fold the skill in,
// and to let it supersede another -- because a skill granting nothing must not
// be replacing anything. Gear lapses the EFFECT, not the skill.
bool GrantsAnything(const CharacterInstance& character, const Skill& skill,
                    int bonus, Activity activity) {
  // A common node belongs to no advancement at all, so this asks the character
  // which books they hold rather than the advancement directly.
  return character.HoldsSkillFrom(skill, activity) &&
         SkillGearMet(character, skill, activity) &&
         EffectiveSkillLevel(character, skill, bonus, activity) > 0;
}

// Whether a grant reaching ONE member reaches this one: the first name in the
// roster that is not the caster's, so every client settles on the same member
// with nothing sent to agree it. Two members sharing a name would both take
// it, a name being all a seated ally carries.
bool ReachesThisMember(const CharacterInstance& character,
                       const AllyGrant& grant,
                       absl::Span<const CharacterInstance> allies) {
  std::string chosen = character.username();
  for (const CharacterInstance& ally : allies) {
    if (&ally != grant.caster && ally.username() < chosen) {
      chosen = ally.username();
    }
  }
  return chosen == character.username();
}

// What the rest of the party holds over this character. Gathered whole before
// anything folds: both rules that thin the list need every ally read first. An
// ally's own Combat Orders lifts what they grant; a level the party granted
// THEM does not.
std::vector<AllyGrant> PartyGrants(const CharacterInstance& character,
                                   const std::map<std::string, Skill>& skills,
                                   absl::Span<const CharacterInstance> allies,
                                   Activity activity) {
  std::map<std::string, AllyGrant> best;
  std::vector<AllyGrant> stacking;
  std::set<std::string> superseded;
  for (const CharacterInstance& ally : allies) {
    int bonus = BonusSkillLevels(ally, skills);
    std::set<std::string> theirs =
        DormantSkillNames(ally, skills, bonus, activity);
    for (const std::pair<const std::string, Skill>& entry : skills) {
      const Skill& skill = entry.second;
      // An Advanced X states the whole of the X it replaces, its party half
      // included -- so a Bishop hands out Blessed Harmony and not the Blessed
      // Ensemble under it.
      if (theirs.count(skill.name()) > 0 || !GrantsToAllies(skill) ||
          !GrantsAnything(ally, skill, bonus, activity)) {
        continue;
      }
      int level = EffectiveSkillLevel(ally, skill, bonus);
      if (skill.ally_effect_stacks()) {
        stacking.push_back(AllyGrant{&skill, level, &ally});
      } else if (best[skill.name()].level < level) {
        best[skill.name()] = AllyGrant{&skill, level, &ally};
      }
    }
    superseded.insert(theirs.begin(), theirs.end());
  }
  // A stacking grant answers to neither rule below: it pays for the COMPANY
  // kept, so a second Cleric is a second payment.
  std::vector<AllyGrant> grants = std::move(stacking);
  for (const std::pair<const std::string, AllyGrant>& entry : best) {
    // A buff does not stack with itself: a character casting Bless already has
    // it folded in and takes nothing from the Cleric beside them.
    if (superseded.count(entry.first) > 0 ||
        character.skill_level(*entry.second.skill) > 0) {
      continue;
    }
    grants.push_back(entry.second);
  }
  // Last, because a grant landing on one member is still thinned by both rules
  // above: whoever it falls on takes nothing if they hold the skill themselves.
  std::vector<AllyGrant> reaching;
  for (const AllyGrant& grant : grants) {
    if (!grant.skill->ally_grant_reaches_one() ||
        ReachesThisMember(character, grant, allies)) {
      reaching.push_back(grant);
    }
  }
  return reaching;
}

// One skill out of the character's book that is paying, and the level it pays
// at. Gathered whole before anything folds: an exclusive group is settled
// between its members, so every member has to be read first.
struct PayingSkill {
  const Skill* skill = nullptr;
  int level = 0;
};

std::vector<PayingSkill> PayingSkills(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus,
    absl::Span<const CharacterInstance> allies, Activity activity) {
  std::set<std::string> superseded =
      DormantSkillNames(character, skills, bonus, activity);
  std::vector<PayingSkill> paying;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // An Advanced X states the whole of the X it replaces rather than a delta,
    // so the two must never both pay. The skill keeps its level and its page;
    // what it loses is its levers. See Skill.supersedes_skill_name.
    if (superseded.count(skill.name()) > 0) {
      continue;
    }
    // Parashock Guard alone: GMS pays the caster for shielding somebody, so a
    // character standing alone is paid nothing. Read here rather than in
    // GrantsAnything because a skill lying idle still supersedes.
    if (skill.requires_party() && allies.empty()) {
      continue;
    }
    // Every KIND is read, not only the passives: GMS hangs permanent grants
    // off active skills too, marked "[Passive Effects: ...]". A skill with no
    // lever contributes nothing whatever kind it is.
    if (!GrantsAnything(character, skill, bonus, activity)) {
      continue;
    }
    paying.push_back(
        PayingSkill{&skill, EffectiveSkillLevel(character, skill, bonus)});
  }
  return paying;
}

// What an ally's grant comes to at the level they hold it.
SkillEffect AllyEffectOf(const AllyGrant& grant) {
  return EffectAt(grant.skill->ally_base(), grant.skill->ally_per_level(),
                  grant.level);
}

// A buff's party half: the caster's level settles it and their INT grows what
// the buff says grows. Their OWN half is read too, a party share being a slice
// of what the caster keeps.
SkillEffect AllyBuffEffect(const Buff& buff, const BuffUp& up) {
  SkillEffect half =
      EffectAt(buff.ally_base(), buff.ally_per_level(), up.caster_level);
  if (buff.ally_int_lever().empty()) {
    return half;
  }
  return GrownByCasterInt(
      buff, half, EffectAt(buff.base(), buff.per_level(), up.caster_level),
      up.caster_int, up.party_size);
}

// Sums every passive the character has learned. HP has to know its whole flat
// total before any percentage lands on it, so nothing is folded here.
// Folds one standing buff in. A buff grants what a passive grants while it is
// up, through the same door and as a source of its OWN -- so its ignored
// defence combines with the character's rather than summing.
void AddStandingBuff(const CharacterInstance& character, const BuffUp& up,
                     int bonus, bool in_company, PassiveTotals& totals) {
  const Skill& skill = *up.skill;
  // An ally's, read off the party half at THEIR level. Levers and nothing
  // else: a Final Attack and a boost follow the caster's own swings.
  if (up.caster != nullptr) {
    AddEffect(AllyBuffEffect(skill.buff(), up), totals);
    return;
  }
  int level = EffectiveSkillLevel(character, skill, bonus);
  SkillEffect held =
      EffectAt(skill.buff().base(), skill.buff().per_level(), level);
  AddEffect(held, totals);
  // The share the buff pays only for company. Its own AddEffect rather than a
  // sum into the one above: a source apiece is what makes two shares of final
  // damage multiply. See Buff.with_party_base.
  if (in_company && (skill.buff().has_with_party_base() ||
                     skill.buff().has_with_party_per_level())) {
    AddEffect(EffectAt(skill.buff().with_party_base(),
                       skill.buff().with_party_per_level(), level),
              totals);
  }
  // A buff can hand over a Final Attack while it stands, and what sets one off
  // belongs to the skill -- so it goes through a passive's door rather than
  // AddEffect alone.
  AddFinalAttack(skill, held, totals);
  // What the buff hands a named skill, through the same door a permanent boost
  // takes -- it is only this fold that makes it a window rather than a gift
  // for good.
  AddSkillBonuses(skill.buff().boost(), level, totals);
}

PassiveTotals LearnedPassives(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills,
                              absl::Span<const BuffUp> buffs_up,
                              absl::Span<const CharacterInstance> allies,
                              Activity activity,
                              std::optional<StatPreset> worn = std::nullopt) {
  PassiveTotals totals;
  // The character's own gear; an ally's is read off theirs, which is what the
  // activity answers for them.
  const StatPreset gear =
      worn.value_or(character.SlotFor(PresetKind::kEquip, activity));
  EquipType weapon = character.weapon_type(gear);
  int bonus = BonusSkillLevels(character, skills, allies);
  std::vector<PayingSkill> paying =
      PayingSkills(character, skills, bonus, allies, activity);
  std::vector<AllyGrant> party =
      PartyGrants(character, skills, allies, activity);
  // Both halves of one rule: a group holds whatever is paying into it, the
  // character's own book and the party's alike.
  ExclusiveBest exclusive;
  for (const PayingSkill& entry : paying) {
    exclusive.Consider(
        *entry.skill,
        EffectAt(entry.skill->base(), entry.skill->per_level(), entry.level));
  }
  for (const AllyGrant& grant : party) {
    exclusive.Consider(*grant.skill, AllyEffectOf(grant));
  }
  for (const PayingSkill& entry : paying) {
    AddPassive(*entry.skill, entry.level, weapon, exclusive, totals);
  }
  // A set bonus grants what a passive grants, so it folds in through the same
  // door. It carries no level and no per-level step: a tier is worth what it
  // says however far the character has come.
  for (const SkillEffect& bonus : character.set_bonuses(gear)) {
    AddEffect(bonus, totals);
  }
  for (const BuffUp& up : buffs_up) {
    AddStandingBuff(character, up, bonus, !allies.empty(), totals);
  }
  // What the party is holding over them, at the level its caster has it. The
  // same door again, and for the same reason.
  for (const AllyGrant& grant : party) {
    AddEffect(exclusive.Thin(*grant.skill, AllyEffectOf(grant)), totals);
  }
  FoldMesoExplosion(totals);
  FoldFinalAttackBoosts(totals);
  FoldFreezeStacks(totals);
  FoldScar(totals);
  FoldEnemyCondition(totals);
  FoldComboOrbs(totals);
  return totals;
}

// What the Hyper Stats add, on top of what the book granted. The four stats
// land here rather than in the allocation because GMS calls them FINAL stat:
// nothing takes a percentage of them. Arcane Force is not among them -- the
// damage chain does not read it.
void AddHyperStats(const CharacterInstance& character, StatPreset slot,
                   PassiveTotals& totals) {
  static_assert(HyperStatField_ARRAYSIZE == 16,
                "a new Hyper Stat needs somewhere to land");
  if (character.proto().level() < kHyperStatUnlockLevel) {
    return;
  }
  totals.str +=
      static_cast<int>(character.hyper_stat_bonus(HYPER_STAT_FIELD_STR, slot));
  totals.dex +=
      static_cast<int>(character.hyper_stat_bonus(HYPER_STAT_FIELD_DEX, slot));
  totals.int_ +=
      static_cast<int>(character.hyper_stat_bonus(HYPER_STAT_FIELD_INT, slot));
  totals.luk +=
      static_cast<int>(character.hyper_stat_bonus(HYPER_STAT_FIELD_LUK, slot));
  totals.max_hp_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_MAX_HP, slot) /
      kPercentToFraction;
  totals.crit_rate +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_CRIT_RATE, slot) /
      kPercentToFraction;
  totals.crit_dmg +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_CRIT_DAMAGE, slot) /
      kPercentToFraction;
  // Ignored defence meets what the book already ignores in reverse, the way
  // two sources of it always meet.
  totals.ied = CombineIgnoredDefense(
      totals.ied, character.hyper_stat_bonus(HYPER_STAT_FIELD_IED, slot) /
                      kPercentToFraction);
  totals.damage_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_DAMAGE, slot) /
      kPercentToFraction;
  totals.boss_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_BOSS_DAMAGE, slot) /
      kPercentToFraction;
  totals.normal_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_NORMAL_DAMAGE, slot) /
      kPercentToFraction;
  // One stat pays both, so a magician and a warrior read the same row.
  int attack = static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_ATTACK, slot));
  totals.attack += attack;
  totals.magic_attack += attack;
  totals.exp_pct += character.hyper_stat_bonus(HYPER_STAT_FIELD_EXP, slot) /
                    kPercentToFraction;
}

// What Inner Ability adds, line by line. The stats land here for a Hyper
// Stat's reason -- final stat -- but the two attacks do not: GMS scales them
// as it scales any other source. A line below the unlock level pays nothing,
// so the panel opening and the stats arriving are one event.
void AddInnerAbility(const CharacterInstance& character, StatPreset slot,
                     PassiveTotals& totals) {
  static_assert(AbilityLineType_ARRAYSIZE == 17,
                "a new Inner Ability line needs somewhere to land");
  if (!character.inner_ability_unlocked()) {
    return;
  }
  for (const AbilityLine& line : character.ability(slot).lines()) {
    const int value = AbilityLineValue(line.type(), line.rank());
    const double share = value / kPercentToFraction;
    switch (line.type()) {
      case ABILITY_LINE_TYPE_STR:
        totals.str += value;
        break;
      case ABILITY_LINE_TYPE_DEX:
        totals.dex += value;
        break;
      case ABILITY_LINE_TYPE_INT:
        totals.int_ += value;
        break;
      case ABILITY_LINE_TYPE_LUK:
        totals.luk += value;
        break;
      case ABILITY_LINE_TYPE_ALL_STATS:
        totals.str += value;
        totals.dex += value;
        totals.int_ += value;
        totals.luk += value;
        break;
      case ABILITY_LINE_TYPE_MAX_HP:
        totals.hp_grant += value;
        break;
      case ABILITY_LINE_TYPE_MAX_HP_PCT:
        totals.max_hp_pct += share;
        break;
      case ABILITY_LINE_TYPE_ATTACK:
        totals.attack += value;
        break;
      case ABILITY_LINE_TYPE_MAGIC_ATTACK:
        totals.magic_attack += value;
        break;
      case ABILITY_LINE_TYPE_CRIT_RATE:
        totals.crit_rate += share;
        break;
      case ABILITY_LINE_TYPE_BOSS_DAMAGE:
        totals.boss_pct += share;
        break;
      case ABILITY_LINE_TYPE_NORMAL_DAMAGE:
        totals.normal_pct += share;
        break;
      case ABILITY_LINE_TYPE_BUFF_DURATION:
        totals.buff_duration_pct += share;
        break;
      case ABILITY_LINE_TYPE_ITEM_DROP:
        totals.item_drop_pct += share;
        break;
      case ABILITY_LINE_TYPE_MESO:
        totals.meso_pct += share;
        break;
      case ABILITY_LINE_TYPE_ATTACK_SPEED:
        totals.attack_speed_bonus += value;
        break;
      case ABILITY_LINE_TYPE_UNSPECIFIED:
        break;
    }
  }
}

// The flat stat `potential` pays a character whose four stats stand at `pile`:
// its own flat lines, plus the share its %stat lines take of that pile once
// those lines are in it. Split out so the fold below and PotentialStatGrant
// cannot drift apart.
//
// A %stat line multiplies the AP pool, what is worn, what the book grants flat
// and the potential's own flat lines -- everything but the three final-stat
// sources, which is what taking the symbols back off is for. NOT Maple
// Warrior's deal, which reads the AP pool alone. Two %stat sources sum rather
// than compound, so both are worked against the same untouched base.
EquipStats PotentialFlatGrant(const int pile[4],
                              const PotentialTotals& potential) {
  const EquipStats& flat = potential.flat;
  const int base[4] = {
      pile[0] + flat.str(),
      pile[1] + flat.dex(),
      pile[2] + flat.int_(),
      pile[3] + flat.luk(),
  };
  const double share[4] = {potential.str_pct, potential.dex_pct,
                           potential.int_pct, potential.luk_pct};
  int granted[4];
  for (int i = 0; i < 4; ++i) {
    granted[i] =
        static_cast<int>(std::floor(base[i] * share[i] + kPercentEpsilon));
  }
  EquipStats paid;
  paid.set_str(flat.str() + granted[0]);
  paid.set_dex(flat.dex() + granted[1]);
  paid.set_int_(flat.int_() + granted[2]);
  paid.set_luk(flat.luk() + granted[3]);
  paid.set_max_hp(flat.max_hp());
  return paid;
}

// The character's stat pile as PotentialFlatGrant wants it: everything a
// %stat line may multiply, with the potentials' own share taken back off.
void StatPileFor(const CharacterInstance& character, const EquipStats& passives,
                 const EquipStats& paid, StatPreset gear, int pile[4]) {
  const AllocatedStats& allocated = character.proto().allocated_stats();
  const EquipStats& worn = character.equip_stats(gear);
  const EquipStats& symbols = character.symbol_stats(gear);
  pile[0] = allocated.str() + worn.str() - symbols.str() + passives.str() -
            paid.str();
  pile[1] = allocated.dex() + worn.dex() - symbols.dex() + passives.dex() -
            paid.dex();
  pile[2] = allocated.int_() + worn.int_() - symbols.int_() + passives.int_() -
            paid.int_();
  pile[3] = allocated.luk() + worn.luk() - symbols.luk() + passives.luk() -
            paid.luk();
}

void AddPotentials(const CharacterInstance& character, Activity activity,
                   StatPreset worn, PassiveTotals& totals) {
  const PotentialTotals& potential = character.potential_totals(worn);
  // Nothing has been paid yet, so the pile is the passives' own flat grant.
  EquipStats passives;
  passives.set_str(totals.str);
  passives.set_dex(totals.dex);
  passives.set_int_(totals.int_);
  passives.set_luk(totals.luk);
  int pile[4];
  StatPileFor(character, passives, EquipStats(), worn, pile);
  const EquipStats paid = PotentialFlatGrant(pile, potential);
  totals.potential_stats = paid;
  totals.str += paid.str();
  totals.dex += paid.dex();
  totals.int_ += paid.int_();
  totals.luk += paid.luk();
  totals.hp_grant += paid.max_hp();

  totals.max_hp_pct += potential.max_hp_pct;
  totals.attack_pct += potential.attack_pct;
  totals.magic_attack_pct += potential.magic_attack_pct;
  totals.damage_pct += potential.damage_pct;
  totals.boss_pct += potential.boss_pct;
  totals.ied = CombineIgnoredDefense(totals.ied, potential.ied);
  totals.crit_dmg += potential.crit_dmg;
  // Worn, so it takes the cap the worn share takes.
  totals.equip_meso_pct += potential.meso_pct;
  totals.item_drop_pct += potential.item_drop_pct;
  totals.cooldown_reduction_seconds += potential.cooldown_seconds;
}

// Cashes Maple Warrior in against the AP SPENT. It grants what a ring grants,
// so it joins the passives' flat pile; read here rather than in AddEffect
// because a skill's levers know nothing about who carries them.
//
// Rounded down per stat as GMS rounds it, and nudged first for FoldPercent's
// reason. Maple World Goddess's Blessing multiplies the share before it is
// cashed in, which is why it lands here.
void FoldApStats(const AllocatedStats& allocated, PassiveTotals& totals) {
  if (totals.ap_stat_pct <= 0.0) {
    return;
  }
  const int stats[] = {allocated.str(), allocated.dex(), allocated.int_(),
                       allocated.luk()};
  const double share = totals.ap_stat_pct * (1.0 + totals.ap_stat_bonus_pct);
  int granted[4];
  for (int i = 0; i < 4; ++i) {
    granted[i] =
        static_cast<int>(std::floor(stats[i] * share + kPercentEpsilon));
  }
  totals.str += granted[0];
  totals.dex += granted[1];
  totals.int_ += granted[2];
  totals.luk += granted[3];
}

// A flat total, then the percentage over the whole of it, fraction dropped.
// Every pile taking a percentage folds through here. The nudge before the
// floor covers summed per-level steps: 16 levels of +1% is 0.15999..., which
// would otherwise cost a whole point.
int FoldPercent(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + kPercentEpsilon));
}

}  // namespace

// The levers an attack keeps for its own swing rather than handing to the
// character. Stripped here and read back in OffenseStatsFor against the skill
// being swung, so Gungnir's Descent ignores 30% when it lands and Dark Impale
// a moment later does not.
//
// Only a SWING keeps them: GMS writes these on a summon only under "[Passive
// Effects]", meaning the character -- which is how Arrow Illusion's ignored
// defence follows the Marksman rather than the decoy.
SkillEffect WithoutSwingLevers(const SkillEffect& effect) {
  SkillEffect kept = effect;
  kept.clear_ied_pct();
  kept.clear_ier_pct();
  kept.clear_boss_pct();
  kept.clear_damage_pct();
  kept.clear_normal_pct();
  kept.clear_crit_rate();
  kept.clear_final_dmg_pct();
  kept.clear_hp_recover_pct();
  kept.clear_meso_drop_cut();
  kept.clear_final_attack_chance_cut();
  kept.clear_max_hp_damage_pct();
  return kept;
}

// The other half, for the skill page, which heads the two apart so a player
// sees which numbers leave with the swing. Beside its complement, the two
// having to name the same levers.
SkillEffect SwingLeversOf(const SkillEffect& effect) {
  SkillEffect swing;
  swing.set_ied_pct(effect.ied_pct());
  swing.set_ier_pct(effect.ier_pct());
  swing.set_boss_pct(effect.boss_pct());
  swing.set_damage_pct(effect.damage_pct());
  swing.set_normal_pct(effect.normal_pct());
  swing.set_crit_rate(effect.crit_rate());
  swing.set_final_dmg_pct(effect.final_dmg_pct());
  swing.set_hp_recover_pct(effect.hp_recover_pct());
  swing.set_meso_drop_cut(effect.meso_drop_cut());
  swing.set_final_attack_chance_cut(effect.final_attack_chance_cut());
  swing.set_max_hp_damage_pct(effect.max_hp_damage_pct());
  return swing;
}

bool SkillAllowsWeapon(const Skill& skill, EquipType weapon) {
  return ListAllowsWeapon(skill.required_equip_type(), weapon);
}

int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills,
                     absl::Span<const CharacterInstance> allies) {
  // What each source pays, keyed by its exclusive group -- or by its own name
  // where it is in none, so two ungrouped sources still sum. A group pays its
  // best rather than the sum, exactly as every other lever does.
  std::map<std::string, double> by_source;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Their whole book, a common node included: HoldsSkillFrom rather than the
    // advancement, which no common node names.
    if (!GrantsSkillLevels(skill) || !character.HoldsSkillFrom(skill)) {
      continue;
    }
    int level = character.skill_level(skill);
    if (level > 0) {
      double& paid =
          by_source[skill.exclusive_group().empty() ? skill.name()
                                                    : skill.exclusive_group()];
      paid = std::max(paid,
                      skill.base().skill_level_bonus() +
                          skill.per_level().skill_level_bonus() * (level - 1));
    }
  }
  double bonus = 0.0;
  for (const std::pair<const std::string, double>& source : by_source) {
    bonus += source.second;
  }
  // What the party is holding out, if the character has none of their own --
  // the buff rule PartyGrants keeps. Read at the ally's LEARNED level: a skill
  // that hands levels out never receives them, so nothing here can loop.
  if (bonus <= 0.0) {
    for (const CharacterInstance& ally : allies) {
      for (const std::pair<const std::string, Skill>& entry : skills) {
        const Skill& skill = entry.second;
        int level = ally.skill_level(skill);
        if (!GrantsToAllies(skill) || level <= 0 ||
            !ally.HoldsSkillFrom(skill)) {
          continue;
        }
        bonus = std::max(bonus, skill.ally_base().skill_level_bonus() +
                                    skill.ally_per_level().skill_level_bonus() *
                                        (level - 1));
      }
    }
  }
  // Floored, and nudged first: the per-level step is a fraction that cannot be
  // written exactly, so the top of the ladder lands a hair under the whole
  // level it is meant to reach.
  return static_cast<int>(std::floor(bonus + kPercentEpsilon));
}

int LevelWithBonus(const Skill& skill, int learned, int bonus) {
  if (learned <= 0 || GrantsSkillLevels(skill) || !TakesGrantedLevels(skill)) {
    return learned;
  }
  int ceiling = SkillMaxLevel(skill);
  if (skill.exceeds_master_level()) {
    ceiling += kLevelsPastMasterLevel;
  }
  return std::min(learned + bonus, ceiling);
}

int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus, Activity activity) {
  return LevelWithBonus(skill, character.skill_level(skill, activity), bonus);
}

std::set<std::string> DormantSkillNames(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus, Activity activity) {
  std::set<std::string> names;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // A skill granting nothing replaces nothing: an unlearned Piercing Arrow
    // II leaves the Piercing Arrow it will one day take over still swinging.
    if (!skill.supersedes_skill_name().empty() &&
        GrantsAnything(character, skill, bonus, activity)) {
      names.insert(skill.supersedes_skill_name());
    }
    if (skill.replaces_skill_name().empty()) {
      continue;
    }
    // One row of the book shows one of the two forms, and the switch says
    // which. A character who never learned the toggle has it off, so every
    // form in the catalog sleeps for them.
    if (character.SkillToggledOn(skill.toggle_skill_name())) {
      names.insert(skill.replaces_skill_name());
    } else {
      names.insert(skill.name());
    }
  }
  return names;
}

bool SkillGearMet(const CharacterInstance& character, const Skill& skill,
                  Activity activity) {
  const StatPreset gear = character.SlotFor(PresetKind::kEquip, activity);
  if (skill.requires_secondary() && !character.has_secondary(gear)) {
    return false;
  }
  return SkillAllowsWeapon(skill, character.weapon_type(gear));
}

std::vector<const Skill*> BuffSkillsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, Activity activity) {
  std::vector<const Skill*> buffs;
  std::set<std::string> dormant = DormantSkillNames(
      character, skills, BonusSkillLevels(character, skills), activity);
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // The three gates every passive passes -- whose book, what gear, what
    // level -- plus the one a buff shares with a swing: a skill the book is
    // not showing has no buff to raise.
    if (!GrantsBuff(skill) || !character.HoldsSkillFrom(skill, activity) ||
        !SkillGearMet(character, skill, activity) ||
        character.skill_level(skill, activity) <= 0 ||
        dormant.count(skill.name()) > 0) {
      continue;
    }
    buffs.push_back(&skill);
  }
  return buffs;
}

double BuffDurationPctFor(const CharacterInstance& character,
                          const std::map<std::string, Skill>& skills) {
  return LearnedPassives(character, skills, {}, {}, Activity::kFarming)
      .buff_duration_pct;
}

std::vector<AllyGrant> AllyBuffsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills,
    absl::Span<const CharacterInstance> allies) {
  std::vector<AllyGrant> buffs;
  for (const AllyGrant& grant :
       PartyGrants(character, skills, allies, Activity::kFarming)) {
    if (GrantsBuffToAllies(*grant.skill)) {
      buffs.push_back(grant);
    }
  }
  return buffs;
}

namespace {

// The HP and MP pools. A worn percentage SUMS with what the skills grant
// rather than compounding: both are shares of one pile, and the pendant is not
// worth more for being worn beside Hyper Body.
void AddPools(int level, const AllocatedStats& allocated,
              const EquipStats& equipped, const PassiveTotals& passives,
              DerivedStats& stats) {
  stats.max_hp =
      FoldPercent(allocated.hp() + equipped.max_hp() + passives.hp_grant +
                      passives.hp_per_level * level,
                  passives.max_hp_pct + equipped.max_hp_pct() / 100.0);
  stats.max_mp =
      FoldPercent(allocated.mp() + equipped.max_mp() + passives.mp_grant +
                      passives.mp_per_level * level,
                  passives.max_mp_pct + equipped.max_mp_pct() / 100.0);
}

// Base DEF reads the TOTALS rather than the allocation: a ring's LUK and a
// passive's LUK are worth the same DEF. The percentage then lands over the
// whole pile, as it does on the HP pool, and it can be a loss -- Reckless Hunt
// buys attack by giving DEF up.
void AddDefense(const AllocatedStats& allocated, const EquipStats& equipped,
                const PassiveTotals& passives, DerivedStats& stats) {
  int str = allocated.str() + equipped.str() + passives.str;
  int dex = allocated.dex() + equipped.dex() + passives.dex;
  int luk = allocated.luk() + equipped.luk() + passives.luk;
  stats.base_def = static_cast<int>(
      std::floor(kDefPerStr * str + kDefPerDexLuk * (dex + luk)));
  stats.def = FoldPercent(stats.base_def + equipped.def() + passives.def_grant,
                          passives.def_factor - 1.0);
}

// A fountain pours one more helping per whole step of INT: the helping grows,
// the clock does not. Charged against the character's WHOLE INT, a ring's
// counting the same as what AP bought.
void AddRegenPulses(const AllocatedStats& allocated, const EquipStats& equipped,
                    const PassiveTotals& passives, DerivedStats& stats) {
  int total_int = allocated.int_() + equipped.int_() + passives.int_;
  for (const RawRegen& source : passives.regen) {
    RegenPulse pulse = source.pulse;
    if (source.int_step > 0.0) {
      double helpings = 1.0 + std::floor(total_int / source.int_step);
      pulse.pct *= helpings;
      pulse.hp = static_cast<int>(pulse.hp * helpings);
    }
    stats.regen_pulses.push_back(pulse);
  }
}

// What the character shakes loose: the drop and meso shares they wear, and the
// two potions that lift them past what gear alone can reach.
void AddDropAndMesoRates(const CharacterInstance& character,
                         const EquipStats& equipped, Activity preset,
                         DerivedStats& stats) {
  // The worn share is whole percents and the granted share a fraction. They
  // meet by summing, the way boss damage does in OffenseStatsFor.
  stats.item_drop_pct += equipped.item_drop_rate() / 100.0;
  // Meso does not: what is worn is capped on its own, so it is kept apart
  // until MesoBonus puts the two together.
  stats.equip_meso_pct += equipped.meso_rate() / 100.0;
  // The Wealth Acquisition Potion, worth three things at once: a share past
  // the equipment cap, the same share of drop rate, and a multiplier over the
  // purse the two of them fill. Farming only -- a boss pays none of it.
  if (preset == Activity::kFarming &&
      character.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION)) {
    stats.meso_pct += kWealthPotionMesoPct;
    stats.item_drop_pct += kWealthPotionDropPct;
    stats.meso_final_mult *= kWealthPotionMesoMult;
  }
  // The Extreme Green Potion, the other half of that deal: stages in a boss
  // fight and nowhere else, and stages that pass the soft cap.
  if (preset == Activity::kBossing &&
      character.ConsumableInEffect(CONSUMABLE_TYPE_EXTREME_GREEN_POTION)) {
    stats.uncapped_attack_speed_bonus += kGreenPotionAttackSpeed;
  }
}

// Pick Pocket and Meso Explosion, worth nothing apart. It rides the swing as a
// Final Attack does but rolls per LINE, so it is one more source in the same
// list rather than a mechanism of its own.
void AddMesoStrike(const PassiveTotals& passives, DerivedStats& stats) {
  if (passives.meso_drop_chance <= 0.0 || passives.meso_hit_pct <= 0.0) {
    return;
  }
  FinalAttackSource meso;
  meso.chance = passives.meso_drop_chance;
  meso.damage_pct = passives.meso_hit_pct;
  meso.normal_skill_pct = passives.meso_normal_skill_pct;
  meso.boss_pct = passives.meso_boss_pct;
  meso.damage_bonus_pct = passives.meso_damage_pct;
  meso.ied = passives.meso_ied;
  meso.crit_rate = passives.meso_crit_rate;
  meso.final_dmg_pct = passives.meso_final_dmg_pct;
  meso.per_line = true;
  stats.final_attacks.push_back(meso);
}

}  // namespace

DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills,
                             absl::Span<const BuffUp> buffs_up,
                             absl::Span<const CharacterInstance> allies,
                             Activity preset, std::optional<StatPreset> gear) {
  const Character& proto = character.proto();
  const AllocatedStats& allocated = proto.allocated_stats();
  // The activity names the gear as well as the allocations, unless the caller
  // named a preset itself -- see stat_preset.h.
  const StatPreset worn =
      gear.value_or(character.SlotFor(PresetKind::kEquip, preset));
  const EquipStats& equipped = character.equip_stats(worn);
  PassiveTotals passives =
      LearnedPassives(character, skills, buffs_up, allies, preset, worn);
  // Before the fold: a potential's %stat and Maple Warrior's both read a base
  // the other has not touched, and the two shares are added rather than
  // compounded.
  AddPotentials(character, preset, worn, passives);
  FoldApStats(allocated, passives);
  // After the fold, never before: a Hyper Stat is final stat, and Maple
  // Warrior takes its share of the allocation alone. The activity names the
  // slot that answers -- see stat_preset.h.
  AddHyperStats(character, character.SlotFor(PresetKind::kHyperStats, preset),
                passives);
  AddInnerAbility(character,
                  character.SlotFor(PresetKind::kInnerAbility, preset),
                  passives);
  // Last of all, because it spends a crit rate nothing more will add to. Read
  // uncapped and with the base rate in, the way the stats page shows it.
  passives.crit_dmg +=
      passives.crit_dmg_per_crit_rate * (passives.crit_rate + kBaseCritRate);

  // Sliced off the totals: every lever the two share is already in place, and
  // what is left below is only what the fold has to change.
  DerivedStats stats = passives;
  stats.activity = preset;
  stats.gear = worn;
  AddPools(proto.level(), allocated, equipped, passives, stats);
  stats.skill_stats.set_def(passives.def_grant);
  stats.skill_stats.set_str(passives.str);
  stats.skill_stats.set_dex(passives.dex);
  stats.skill_stats.set_int_(passives.int_);
  stats.skill_stats.set_luk(passives.luk);
  stats.skill_stats.set_attack(passives.attack);
  stats.skill_stats.set_magic_attack(passives.magic_attack);
  AddDefense(allocated, equipped, passives, stats);
  // Floored at nothing rather than clamped to a share: a monster stripped of
  // the whole of its attack still lands the 1 damage GMS insists on.
  stats.enemy_attack_pct = std::min(1.0, stats.enemy_attack_pct);
  // A pact that came back at once would read as no pact at all -- 0 is what
  // says a character is never revived -- so the cut stops a second short.
  if (stats.revive_cooldown_seconds > 0.0) {
    stats.revive_cooldown_seconds = std::max(
        1.0, stats.revive_cooldown_seconds - passives.revive_cooldown_cut);
  }
  AddRegenPulses(allocated, equipped, passives, stats);
  AddDropAndMesoRates(character, equipped, preset, stats);
  AddMesoStrike(passives, stats);
  return stats;
}

double MesoBonus(const DerivedStats& derived) {
  double worn = std::min(derived.equip_meso_pct, kEquipMesoSoftCap);
  return std::min(worn + derived.meso_pct, kMesoHardCap);
}

PassiveOffense PassiveOffenseFor(const DerivedStats& derived) {
  PassiveOffense passives;
  passives.crit_rate = derived.crit_rate;
  passives.crit_dmg = derived.crit_dmg;
  passives.mastery = derived.mastery;
  passives.damage_pct = derived.damage_pct;
  passives.boss_pct = derived.boss_pct;
  passives.normal_pct = derived.normal_pct;
  passives.mirror_line_pct = derived.mirror_line_pct;
  passives.bonus_attack_lines = derived.bonus_attack_lines;
  passives.final_dmg_pct = derived.final_dmg_pct;
  passives.ied = derived.ied;
  passives.ier = derived.ier;
  passives.skill_bonus = derived.skill_bonus;
  passives.arcane_pct = derived.arcane_damage_factor;
  return passives;
}

EquipStats PotentialStatGrant(const CharacterInstance& character,
                              const DerivedStats& derived,
                              const PotentialTotals& totals) {
  int pile[4];
  StatPileFor(character, derived.skill_stats, derived.potential_stats,
              derived.gear, pile);
  return PotentialFlatGrant(pile, totals);
}

int TotalIntFor(const CharacterInstance& character,
                const std::map<std::string, Skill>& skills) {
  DerivedStats derived = DerivedStatsFor(character, skills);
  return character.proto().allocated_stats().int_() +
         TotalEquipStats(character, derived).int_();
}

EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived) {
  const EquipStats sources[] = {character.equip_stats(derived.gear),
                                derived.skill_stats};
  EquipStats total = SumEquipStats(absl::MakeConstSpan(sources));
  // Here rather than in skill_stats because what it scales is the weapon in
  // hand as much as the skill's grant. Both attack fields take it.
  total.set_attack(FoldPercent(total.attack(), derived.attack_pct));
  total.set_magic_attack(
      FoldPercent(total.magic_attack(), derived.magic_attack_pct));
  return total;
}

OffenseStats CharacterOffense(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills,
                              Activity preset, std::optional<StatPreset> gear) {
  const Character& p = character.proto();
  DerivedStats derived = DerivedStatsFor(character, skills, /*buffs_up=*/{},
                                         /*allies=*/{}, preset, gear);
  return OffenseStatsFor(p.job(), p.level(), p.allocated_stats(),
                         TotalEquipStats(character, derived),
                         character.weapon_type(derived.gear),
                         /*attack_skill=*/nullptr,
                         /*attack_level=*/0, PassiveOffenseFor(derived));
}

int CharacterCombatPower(const CharacterInstance& character,
                         const std::map<std::string, Skill>& skills,
                         Activity preset, std::optional<StatPreset> gear) {
  return CombatPower(CharacterOffense(character, skills, preset, gear),
                     preset == Activity::kBossing);
}

}  // namespace ms
