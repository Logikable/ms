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
#include "src/character/skill_placement.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/item/equip_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Tolerance for the floor below, far smaller than any percentage a skill
// grants.
constexpr double kPercentEpsilon = 1e-9;

// Hyper Stats are given in whole percents, like a worn item's stats.
constexpr double kPercentToFraction = 100.0;

// DEF every character gets from their primary stats before wearing anything:
// 1.5 per point of STR and 0.4 per point of DEX and LUK. INT gives none.
constexpr double kDefPerStr = 1.5;
constexpr double kDefPerDexLuk = 0.4;

// Whether a list of weapon types allows `weapon`. An empty list allows every
// weapon, since a skill that names none works with all of them.
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

// Whether this skill's timed buff also covers the party, not just the caster.
// See Buff.ally_base.
bool GrantsBuffToAllies(const Skill& skill) {
  return LongestBuffDuration(skill.buff()) > 0.0 &&
         (skill.buff().has_ally_base() || skill.buff().has_ally_per_level());
}

// Whether this is a skill that raises other skills' levels. It checks the data
// rather than the name, so the rule that it doesn't raise itself holds for any
// new skill written the same way.
bool GrantsSkillLevels(const Skill& skill) {
  return skill.base().skill_level_bonus() > 0.0;
}

// Whether granted levels apply to this skill. GMS's Combat Orders lists the
// exceptions: hyper and 5th job skills, and everything on the beginner's page,
// which includes the book itself and the link skills.
bool TakesGrantedLevels(const Skill& skill) {
  return !skill.hyper() && skill.v_node() == V_NODE_KIND_UNSPECIFIED &&
         skill.account_levels_per_level() == 0 &&
         skill.link_line() == JOB_UNSPECIFIED &&
         !ListedIn(skill, JOB_ADVANCEMENT_BEGINNER);
}

// Whether this skill gives the rest of the party anything, either permanently
// or while its buff lasts. See Skill.ally_base and Buff.ally_base.
bool GrantsToAllies(const Skill& skill) {
  return skill.has_ally_base() || skill.has_ally_per_level() ||
         GrantsBuffToAllies(skill);
}

// A regeneration pulse as the skill states it, before the character's INT is
// applied. `int_step` is the INT needed for one more helping. It is kept here
// because total INT isn't known until every passive has been read.
struct RawRegen {
  RegenPulse pulse;
  double int_step = 0.0;
};

// The running totals of the learned passives. It is a DerivedStats, and the
// shared fields mean the same thing; the final step in DerivedStatsFor changes
// only what it must. The extra fields hold work in progress: flat grants that
// still have to be combined with the allocation, and counts that mean nothing
// until every passive is read.
//
// The final step writes five inherited fields from scratch: max_hp, max_mp,
// def, base_def and regen_pulses. They stay at zero until then.
struct PassiveTotals : DerivedStats {
  // Flat HP, MP and DEF from the passives. They are kept apart from the totals
  // because each still has to be combined with the allocation and worn gear.
  int hp_grant = 0;
  int mp_grant = 0;
  int def_grant = 0;
  int hp_per_level = 0;
  double max_hp_pct = 0.0;
  int mp_per_level = 0;
  double max_mp_pct = 0.0;
  // Stored as a multiplier on DEF rather than a sum of percentages, because
  // sources multiply: Phoenix's +30% and Reckless Hunt's -25% leave 97.5% of
  // DEF, not 105%.
  double def_factor = 1.0;
  // Stats the passives grant. They become skill_stats once every passive is
  // read.
  int str = 0;
  int dex = 0;
  int int_ = 0;
  int luk = 0;
  int attack = 0;
  int magic_attack = 0;
  // One entry per skill that grants a regeneration pulse, in catalog order.
  // They become regen_pulses once the character's total INT is known.
  std::vector<RawRegen> regen;
  // Extra stacks a buff adds to that cap while it is up. Kept apart until
  // FoldFreezeStacks, which only raises a cap the character already has.
  int freeze_cap_bonus = 0;
  // Pick Pocket's chance and Meso Explosion's damage. They come from two skills
  // and are worth nothing alone, so they are totalled here and paired at the
  // end.
  double meso_drop_chance = 0.0;
  // Per line until FoldMesoExplosion multiplies in the line count.
  double meso_hit_pct = 0.0;
  int meso_lines = 1;
  // Boss damage, plain damage, ignored defence, crit rate and final damage for
  // a thrown meso, after FoldMesoExplosion adds what skills naming Meso
  // Explosion gave it.
  double meso_boss_pct = 0.0;
  double meso_damage_pct = 0.0;
  // Percentage points a thrown meso adds against anything that is not a boss.
  // Like the damage above, it is per line until FoldMesoExplosion multiplies in
  // the line count, because GMS states it per shot.
  double meso_normal_skill_pct = 0.0;
  double meso_ied = 0.0;
  double meso_crit_rate = 0.0;
  double meso_final_dmg_pct = 0.0;
  std::string meso_skill;
  // Seconds the skill book takes off the shortest revival cooldown. Summed
  // apart and subtracted once the shortest cooldown is known.
  double revive_cooldown_cut = 0.0;
  // Share of AP-bought stats given back as flat stats. It is summed, then
  // applied to the allocation once every passive is read; see DerivedStatsFor.
  double ap_stat_pct = 0.0;
  // Share added to the one above before it is applied, so it multiplies Maple
  // Warrior's own grant. Sources add up.
  double ap_stat_bonus_pct = 0.0;
  // Combo Orbs and the bonuses priced per orb. The count is the highest any
  // passive grants, since a character has one ring of orbs however many skills
  // describe it. The per-orb bonuses are applied only once every passive is
  // read.
  int combo_orbs = 0;
  int attack_per_combo_orb = 0;
  double final_dmg_pct_per_combo_orb = 0.0;
  double boss_pct_per_combo_orb = 0.0;
  int def_per_combo_orb = 0;
  // Share added to all four of those at once, and nothing else.
  double combo_orb_gain_pct = 0.0;
  // Extra orbs that count only toward final damage above. They are added to the
  // count rather than multiplied, because GMS's per-orb final damage is
  // additive.
  double final_dmg_combo_orbs = 0.0;
};

// Every lever that is a plain sum, which is most of them.
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

// Adds one level's levers to the running totals. This is separate from
// AddPassive because a weapon bonus is a second set of the same levers. The
// plain sums are in AddSummedLevers; this handles the levers that combine
// another way.
void AddEffect(const SkillEffect& granted, PassiveTotals& totals) {
  AddSummedLevers(granted, totals);
  totals.def_factor *= 1.0 + granted.def_pct();
  // One skill lever raises both attacks. Only potentials grant them separately.
  totals.attack_pct += granted.attack_pct();
  totals.magic_attack_pct += granted.attack_pct();
  // Damage sent to MP never reaches HP, and nothing tracks MP, so Magic Guard
  // counts as damage reduction. Reduction multiplies: two 50% sources leave a
  // quarter. Summing would leave none, and past 100% it would heal.
  totals.damage_taken_pct = 1.0 - (1.0 - totals.damage_taken_pct) *
                                      (1.0 - granted.damage_taken_pct()) *
                                      (1.0 - granted.damage_to_mp_pct());
  // Dodge combines the same way for the same reason: the share two sources let
  // through is the product of what each lets through.
  totals.dodge_chance =
      1.0 - (1.0 - totals.dodge_chance) * (1.0 - granted.dodge_chance());
  // Unlike the two above, this reduction adds up, because it comes off the
  // monster's own attack and GMS states every source as points on that number.
  totals.enemy_attack_pct += granted.enemy_attack_pct();
  totals.enemy_attack_reaches_boss |= granted.enemy_attack_reaches_boss();
  // The pulse and its interval stay separate all the way to the fight, which
  // restores HP on each tick instead of spreading it over the seconds between.
  if (granted.regen_interval_seconds() > 0.0 &&
      (granted.regen_pct() > 0.0 || granted.regen_hp() > 0.0)) {
    totals.regen.push_back(
        RawRegen{{granted.regen_pct(), WholeValue(granted.regen_hp()),
                  granted.regen_interval_seconds()},
                 granted.regen_int_step()});
  }
  totals.status_resistance += granted.status_resistance();
  // Read here rather than next to the cap itself, so a buff that grants either
  // also counts. Buffs are only added through AddEffect.
  totals.freeze_cap_bonus += WholeValue(granted.freeze_stack_cap_bonus());
  // The shortest cooldown wins rather than the sum: two revivals are not one
  // long one, and what matters is how soon the next one comes.
  double revive = granted.revive_cooldown_seconds();
  if (revive > 0.0 && (totals.revive_cooldown_seconds <= 0.0 ||
                       revive < totals.revive_cooldown_seconds)) {
    totals.revive_cooldown_seconds = revive;
  }
  // Unlike the cooldown itself, reductions to it add up: they are amounts of
  // seconds, not a choice between cooldowns. Applied once every passive is
  // read; see DerivedStatsFor.
  totals.revive_cooldown_cut += granted.revive_cooldown_cut_seconds();
  // Same rule for the near-death heal: the shorter cooldown is used, and two
  // sources don't add up.
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
  // The elemental version of ied. It adds up instead of combining
  // multiplicatively, because GMS subtracts it from the resistance directly.
  totals.ier += granted.ier_pct();
  // Mastery takes the best source instead of the sum: two masteries don't make
  // a swing twice as steady.
  totals.mastery = std::max(totals.mastery, granted.mastery());
  // The enemy's condition works the same way: a monster is either afflicted or
  // not, and a skill raising another's rate still gives one rate. It is read
  // here rather than next to the count because a SkillEffect is all an ally
  // passes on.
  totals.condition.final_dmg_pct_when_afflicted =
      std::max(totals.condition.final_dmg_pct_when_afflicted,
               granted.final_dmg_pct_when_afflicted());
  totals.condition.final_dmg_pct_per_dot = std::max(
      totals.condition.final_dmg_pct_per_dot, granted.final_dmg_pct_per_dot());
  // Final damage multiplies: two 10% sources give 21%. It is stored as the
  // combined fraction because that is the single number the damage formula
  // uses.
  totals.final_dmg_pct =
      (1.0 + totals.final_dmg_pct) * (1.0 + granted.final_dmg_pct()) - 1.0;
}

// Adds one skill's Final Attack. It is separate from AddEffect because what
// triggers a Final Attack belongs to the skill, not to the level's levers, and
// AddEffect only sees levers.
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
  source.credit = skill.name();
  source.owner_swings = DealsDamage(skill.kind());
  totals.final_attacks.push_back(source);
}

// Adds one skill's chance to hit a single enemy harder. It is separate from
// AddEffect for the same reason as AddFinalAttack.
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

// Adds Freezing Crush. When two skills grant it, the higher cap and the better
// stack value win rather than summing. That lets Frost Clutch improve a stack
// without granting a cap of its own.
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
  // The stun bonus doesn't stack: one skill applies the stun and names the
  // swings that benefit. A second skill would be a separate status.
  if (skill.stun().final_dmg_pct() > 0.0 &&
      skill.stun().lifted_tag() != SKILL_TAG_UNSPECIFIED) {
    totals.stun_lift.lifted_tag = skill.stun().lifted_tag();
    totals.stun_lift.from_skill = skill.name();
  }
  // The mark works the same way, for the same reason.
  if (skill.mark().final_dmg_pct() > 0.0 &&
      skill.mark().lifted_tag() != SKILL_TAG_UNSPECIFIED) {
    totals.mark_lift.lifted_tag = skill.mark().lifted_tag();
  }
}

// Adds the scar. With two sources, the better value of each wins, the same as
// for freezing: a deeper scar is still one scar.
void AddScar(const SkillEffect& granted, PassiveTotals& totals) {
  totals.scar.chance = std::max(totals.scar.chance, granted.scar_chance());
  totals.scar.seconds = std::max(totals.scar.seconds, granted.scar_seconds());
  totals.scar.final_dmg_pct =
      std::max(totals.scar.final_dmg_pct, granted.final_dmg_pct_when_scarred());
  totals.scar.enemy_attack_pct = std::max(
      totals.scar.enemy_attack_pct, granted.enemy_attack_pct_when_scarred());
}

// How many burns a per-burn skill counts. The rates are added in AddEffect like
// every other lever; only the cap belongs to the Skill.
void AddDotCount(const Skill& skill, PassiveTotals& totals) {
  totals.condition.dot_count_cap =
      std::max(totals.condition.dot_count_cap, skill.dot_count_cap());
}

// Records Meso Explosion without adding it yet. Meso Mastery's points apply to
// each of its lines, and skills are added in catalog order, so the pair waits
// until every passive has been read.
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

// Adds one boost's levers into the entry of the skill it targets. Each lever
// combines with what is already there the usual way.
void AddSkillBonus(const SkillBoost& boost, int level, SkillBonus& into) {
  SkillEffect aimed = EffectAt(boost.effect(), boost.effect_per_level(), level);
  into.skill_pct += aimed.skill_pct();
  into.damage_pct += aimed.damage_pct();
  into.boss_pct += aimed.boss_pct();
  into.normal_pct += aimed.normal_pct();
  into.crit_rate += aimed.crit_rate();
  // These two don't add up, for the usual reasons.
  into.ied = CombineIgnoredDefense(into.ied, aimed.ied_pct());
  into.final_dmg_pct =
      (1.0 + into.final_dmg_pct) * (1.0 + aimed.final_dmg_pct()) - 1.0;
  into.final_attack_chance += aimed.final_attack_chance();
  // A separate field from the other seven because it applies to the burn the
  // skill leaves, not to the swing. See SkillBoost.
  into.dot_skill_pct +=
      boost.dot_skill_pct() + boost.dot_skill_pct_per_level() * (level - 1);
  into.dot_duration_seconds +=
      boost.dot_duration_seconds() +
      boost.dot_duration_seconds_per_level() * (level - 1);
  // This multiplies rather than adds, so two would compound. Nothing grants
  // two; multiplying is what the lever means.
  if (boost.final_attack_chance_mult() > 0.0) {
    into.final_attack_chance_mult *= boost.final_attack_chance_mult();
  }
  // Extra hits the boost gives that skill. Each is read at the granting skill's
  // level here. The swing they join is read at its own level, so a per-level
  // ladder left on them would be applied twice.
  for (const SwingHit& hit : boost.extra_hit()) {
    SwingHit landed = hit;
    *landed.mutable_base() = EffectAt(hit.base(), hit.per_level(), level);
    landed.clear_per_level();
    into.extra_hit.push_back(std::move(landed));
  }
}

// Records the bonuses a list of boosts gives to other skills by name. This is
// kept out of AddEffect, which only sees levers: the target skill is named on
// the boost. Both a skill's own boosts and its buff's boosts come through here.
void AddSkillBonuses(
    const google::protobuf::RepeatedPtrField<SkillBoost>& boosts, int level,
    PassiveTotals& totals) {
  for (const SkillBoost& boost : boosts) {
    // The boost gives nothing until the granting skill reaches its
    // SkillBoost.min_level.
    if (level < boost.min_level()) {
      continue;
    }
    // A form is a separate swing, and the fight looks up its entry by the name
    // it swings under, so a boost reaching it is stored under that name too.
    for (const std::string& name : BoostTargetNames(boost)) {
      AddSkillBonus(boost, level, totals.skill_bonus[name]);
    }
  }
}

// Records the wound a skill gives to the swings that apply it. It is read from
// the skill that states it, like a boost: Assassinate and Sonic Blow don't
// mention it, and neither applies one until Trickblade is learned.
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

// For each exclusive group, the best value of each lever and which skill gives
// it. It is built from everything the character and party have before anything
// is added, because a group is settled among all its members at once.
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
      // A tie goes to the one read first, in catalog order, so two members
      // giving the same value only pay once.
      if (claim.source.empty() || value > claim.value) {
        claim = Claim{value, skill.name()};
      }
    }
  }

  // `effect` minus every lever another member of its group gives more of.
  // Skills in no group are unchanged, and that is nearly all of them.
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
  // Keyed by group, then by the lever's field number.
  std::map<std::string, std::map<int, Claim>> best_;
};

void AddPassive(const Skill& skill, int level, EquipType weapon,
                const ExclusiveBest& exclusive, PassiveTotals& totals) {
  SkillEffect granted =
      exclusive.Thin(skill, EffectAt(skill.base(), skill.per_level(), level));
  if (DealsDamage(skill.kind())) {
    AddEffect(WithoutSwingLevers(granted), totals);
    // The part of an attack skill that stays with the character rather than the
    // swing. See Skill.passive.
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
  // A burn on a passive belongs to the character: the poison stays on the claw,
  // so everything they hit gets it. Burns on attacks and summons are read where
  // those are built.
  if (skill.kind() == SKILL_KIND_PASSIVE &&
      skill.dot().interval_seconds() > 0.0) {
    totals.dots.push_back(CharacterDot{skill.dot(), level, skill.name()});
  }
  AddMesoExplosion(skill, granted, level, totals);
  totals.combo_orbs = std::max(totals.combo_orbs, ComboOrbsAt(skill, level));
  // A weapon bonus is a second set of the same levers for some of the weapons
  // the skill accepts. It is read at level 1 because it is always flat.
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    if (bonus.required_equip_type_size() > 0 &&
        ListAllowsWeapon(bonus.required_equip_type(), weapon)) {
      AddEffect(bonus.effect(), totals);
      AddFinalAttack(skill, bonus.effect(), totals);
    }
  }
}

// Turns the value of one line of a thrown meso into the value of a whole one,
// now that Meso Mastery's points are known. Meso Explosion is not a swing, so
// boosts aimed at it are applied here.
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

// Gives each Final Attack the boosts aimed at the skill that triggers it. This
// runs after all skills are read, because the granting skill may be read after
// the one carrying the Final Attack.
void FoldFinalAttackBoosts(PassiveTotals& totals) {
  for (FinalAttackSource& source : totals.final_attacks) {
    std::map<std::string, SkillBonus>::const_iterator boost =
        totals.skill_bonus.find(source.skill_name);
    if (source.skill_name.empty() || boost == totals.skill_bonus.end()) {
      continue;
    }
    // Scaled last so a doubling includes every additive source before it. It
    // may go past 100%: RolledFinalAttack lands that many hits outright and
    // rolls for the remainder.
    source.chance = (source.chance + boost->second.final_attack_chance) *
                    boost->second.final_attack_chance_mult;
    source.damage_bonus_pct += boost->second.damage_pct;
    source.crit_rate += boost->second.crit_rate;
    source.ied = CombineIgnoredDefense(source.ied, boost->second.ied);
    source.final_dmg_pct =
        (1.0 + source.final_dmg_pct) * (1.0 + boost->second.final_dmg_pct) -
        1.0;
    // Points on the hit's own multiplier, like GMS's "Night Lord's Mark Damage:
    // +100% points". Only when the owning skill isn't also swung; otherwise the
    // points already apply to that swing.
    if (!source.owner_swings) {
      source.damage_pct += boost->second.skill_pct;
    }
  }
}

// Finalises the scar. With nothing to leave a scar, reading one is worthless: a
// character with Chance Attack but no Scarring Sword has half the mechanism,
// and half is worth nothing.
void FoldScar(PassiveTotals& totals) {
  if (totals.scar.chance <= 0.0 || totals.scar.seconds <= 0.0) {
    totals.scar = Scar{};
  }
}

// Finalises the burn bonus by the same rule: a rate per burn with nothing
// counting burns gives nothing.
void FoldEnemyCondition(PassiveTotals& totals) {
  if (totals.condition.dot_count_cap <= 0) {
    totals.condition.final_dmg_pct_per_dot = 0.0;
  }
}

// Finalises the freeze stacks. A cap raised by a buff deepens Freezing Crush's
// stacks, so it only counts when the character has Freezing Crush.
void FoldFreezeStacks(PassiveTotals& totals) {
  if (totals.freeze.cap <= 0) {
    totals.freeze = FreezeStacks{};
    return;
  }
  totals.freeze.cap += totals.freeze_cap_bonus;
}

void FoldComboOrbs(PassiveTotals& totals) {
  // The ring is worth its orb count, and a gain raises the whole ring, so it
  // multiplies once here rather than wherever each lever is read.
  double ring = totals.combo_orbs * (1.0 + totals.combo_orb_gain_pct);
  totals.attack += WholeValue(totals.attack_per_combo_orb * ring);
  totals.boss_pct += totals.boss_pct_per_combo_orb * ring;
  totals.def_grant += WholeValue(totals.def_per_combo_orb * ring);
  // Orbs granted only for final damage join the ring for that one lever, and a
  // gain raises them like the rest. Sword Illusion detonates six Combo Orbs,
  // whatever each one is worth at the time.
  double lit = (totals.combo_orbs + totals.final_dmg_combo_orbs) *
               (1.0 + totals.combo_orb_gain_pct);
  double orbs = totals.final_dmg_pct_per_combo_orb * lit;
  totals.final_dmg_pct = (1.0 + totals.final_dmg_pct) * (1.0 + orbs) - 1.0;
}

// Whether `skill` has a timed buff, one that lasts a while rather than forever.
// See Skill.buff.
bool GrantsBuff(const Skill& skill) {
  return LongestBuffDuration(skill.buff()) > 0.0;
}

// Whether this character gets anything from `skill`: it is in their book, they
// hold the gear it needs, and they have a level in it. It is checked both when
// adding the skill and when letting it supersede another, because a skill that
// grants nothing must not replace anything. Missing gear stops the effect, not
// the skill.
bool GrantsAnything(const CharacterInstance& character, const Skill& skill,
                    int bonus, Activity activity) {
  // A common node belongs to no advancement, so this asks which books the
  // character holds rather than checking the advancement directly.
  return character.HoldsSkillFrom(skill, activity) &&
         SkillGearMet(character, skill, activity) &&
         EffectiveSkillLevel(character, skill, bonus, activity) > 0;
}

// Whether a grant that reaches one member reaches this one. It goes to the
// first name in the roster that isn't the caster's, so every client picks the
// same member without having to agree. Two members with the same name would
// both get it, since a name is all a seated ally carries.
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

// What the rest of the party grants this character. The whole list is gathered
// before anything is added, because both filtering rules need every ally read
// first. An ally's own Combat Orders raises what they grant; a level the party
// granted them does not.
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
      // An Advanced X includes all of the X it replaces, party part included.
      // So a Bishop gives Blessed Harmony and not the Blessed Ensemble under
      // it.
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
  // Neither rule below applies to a stacking grant. It pays for each ally
  // present, so a second Cleric pays a second time.
  std::vector<AllyGrant> grants = std::move(stacking);
  for (const std::pair<const std::string, AllyGrant>& entry : best) {
    // A buff doesn't stack with itself: a character casting Bless already
    // counts their own and gets nothing from the Cleric beside them.
    if (superseded.count(entry.first) > 0 ||
        character.skill_level(*entry.second.skill) > 0) {
      continue;
    }
    grants.push_back(entry.second);
  }
  // This runs last because a grant that lands on one member is still filtered
  // by both rules above: that member gets nothing if they have the skill
  // themselves.
  std::vector<AllyGrant> reaching;
  for (const AllyGrant& grant : grants) {
    if (!grant.skill->ally_grant_reaches_one() ||
        ReachesThisMember(character, grant, allies)) {
      reaching.push_back(grant);
    }
  }
  return reaching;
}

// One active skill from the character's book and the level it counts at. The
// whole list is gathered before anything is added, because an exclusive group
// is settled among all its members.
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
    // An Advanced X includes all of the X it replaces rather than a difference,
    // so both must never count. The replaced skill keeps its level and its page
    // but loses its levers. See Skill.supersedes_skill_name.
    if (superseded.count(skill.name()) > 0) {
      continue;
    }
    // Parashock Guard, for example: GMS pays the caster for shielding someone,
    // so a character alone gets nothing. This check is here rather than in
    // GrantsAnything because an idle skill still supersedes.
    if (skill.requires_party() && allies.empty()) {
      continue;
    }
    // Every skill kind is read, not only passives, because GMS puts permanent
    // grants on active skills too, marked "[Passive Effects: ...]". A skill
    // with no levers adds nothing, whatever its kind.
    if (!GrantsAnything(character, skill, bonus, activity)) {
      continue;
    }
    paying.push_back(
        PayingSkill{&skill, EffectiveSkillLevel(character, skill, bonus)});
  }
  return paying;
}

// What an ally's grant is worth at their level.
SkillEffect AllyEffectOf(const AllyGrant& grant) {
  return EffectAt(grant.skill->ally_base(), grant.skill->ally_per_level(),
                  grant.level);
}

// A buff's party part: the caster's level sets it, and their INT scales
// whatever the buff says scales. The caster's own part is read too, since a
// party share is a slice of what the caster gets.
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

// Adds one active buff. A buff grants what a passive grants while it is up, and
// is added the same way, as its own source. So its ignored defence combines
// with the character's instead of adding.
void AddStandingBuff(const CharacterInstance& character, const BuffUp& up,
                     int bonus, bool in_company, PassiveTotals& totals) {
  const Skill& skill = *up.skill;
  // An ally's buff is read from its party part at the ally's level. Only its
  // levers count; a Final Attack or a boost follows the caster's own swings.
  if (up.caster != nullptr) {
    AddEffect(AllyBuffEffect(skill.buff(), up), totals);
    return;
  }
  int level = EffectiveSkillLevel(character, skill, bonus);
  SkillEffect held =
      EffectAt(skill.buff().base(), skill.buff().per_level(), level);
  AddEffect(held, totals);
  // The share the buff gives only while in a party. It gets its own AddEffect
  // call rather than being summed into the one above, so two final damage
  // shares multiply. See Buff.with_party_base.
  if (in_company && (skill.buff().has_with_party_base() ||
                     skill.buff().has_with_party_per_level())) {
    AddEffect(EffectAt(skill.buff().with_party_base(),
                       skill.buff().with_party_per_level(), level),
              totals);
  }
  // A buff can give a Final Attack while it is up, and what triggers it belongs
  // to the skill, so it goes through AddFinalAttack, not only AddEffect.
  AddFinalAttack(skill, held, totals);
  // What the buff gives a named skill goes through the same path as a permanent
  // boost. Only the buff's uptime makes it temporary.
  AddSkillBonuses(skill.buff().boost(), level, totals);
}

// Sums every passive the character has learned. HP needs its whole flat total
// before any percentage applies, so nothing is folded here.
PassiveTotals LearnedPassives(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills,
                              absl::Span<const BuffUp> buffs_up,
                              absl::Span<const CharacterInstance> allies,
                              Activity activity,
                              std::optional<StatPreset> worn = std::nullopt) {
  PassiveTotals totals;
  // The character's own gear preset. An ally's comes from their own activity.
  const StatPreset gear =
      worn.value_or(character.SlotFor(PresetKind::kEquip, activity));
  EquipType weapon = character.weapon_type(gear);
  int bonus = BonusSkillLevels(character, skills, allies);
  std::vector<PayingSkill> paying =
      PayingSkills(character, skills, bonus, allies, activity);
  std::vector<AllyGrant> party =
      PartyGrants(character, skills, allies, activity);
  // Both sources use one group table: a group includes everything that pays
  // into it, from the character's own book and the party alike.
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
  // A set bonus grants what a passive grants, so it is added the same way. It
  // has no level: a tier is worth what it says at any character level.
  for (const SkillEffect& bonus : character.set_bonuses(gear)) {
    AddEffect(bonus, totals);
  }
  for (const BuffUp& up : buffs_up) {
    AddStandingBuff(character, up, bonus, !allies.empty(), totals);
  }
  // What the party grants them, at each caster's level, added the same way.
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

// What the Hyper Stats add on top of the skill book. The four stats are added
// here rather than to the allocation because GMS treats them as final stats: no
// percentage applies to them. Arcane Force is skipped because the damage
// formula doesn't use it.
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
  // Ignored defence combines multiplicatively with the book's, as usual.
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
  // One stat raises both attacks, so a magician and a warrior use the same row.
  int attack = static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_ATTACK, slot));
  totals.attack += attack;
  totals.magic_attack += attack;
  totals.exp_pct += character.hyper_stat_bonus(HYPER_STAT_FIELD_EXP, slot) /
                    kPercentToFraction;
}

// What Inner Ability adds, line by line. Its stats are final stats, like Hyper
// Stats, but its attack lines are not: GMS scales them like any other source. A
// line below the unlock level gives nothing, so the panel and its stats unlock
// together.
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

// The flat stats `potential` gives a character whose four stats total `pile`:
// its own flat lines, plus the share its %stat lines take of the pile once
// those flat lines are added. It is shared with PotentialStatGrant so the two
// can't drift apart.
//
// A %stat line multiplies the AP pool, worn gear, the book's flat grants and
// the potential's own flat lines. It skips the three final-stat sources, which
// is why the symbols are taken back off. It doesn't use Maple Warrior's rule,
// which reads the AP pool alone. Two %stat sources add rather than compound, so
// both are applied to the same base.
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

// The character's stat pile in the form PotentialFlatGrant wants: everything a
// %stat line may multiply, minus the potentials' own share.
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
  // Nothing has been added yet, so the pile is only the passives' flat grant.
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
  // This is worn, so it shares the worn cap.
  totals.equip_meso_pct += potential.meso_pct;
  totals.item_drop_pct += potential.item_drop_pct;
  totals.cooldown_reduction_seconds += potential.cooldown_seconds;
}

// Applies Maple Warrior to the AP spent. It grants what a ring grants, so it
// goes into the passives' flat stats. It is done here rather than in AddEffect
// because a skill's levers don't know who holds them.
//
// Each stat is rounded down, as GMS does, after the same nudge FoldPercent
// uses. Maple World Goddess's Blessing multiplies the share before it is
// applied, which is why this happens here.
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

// A flat total plus a percentage of it, with the fraction dropped. Every
// percentage on a stat pile goes through here. The nudge before the floor
// handles summed per-level steps: 16 levels of +1% is 0.15999..., which would
// otherwise lose a whole point.
int FoldPercent(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + kPercentEpsilon));
}

}  // namespace

// The levers an attack keeps for its own swing rather than giving to the
// character. They are removed here and read back in OffenseStatsFor for the
// skill being swung, so Gungnir's Descent ignores 30% on its own hit and Dark
// Impale a moment later does not.
//
// Only swings keep them. GMS lists these on a summon only under "[Passive
// Effects]", meaning they go to the character. That is why Arrow Illusion's
// ignored defence goes to the Marksman rather than the decoy.
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

// The swing-only levers, for the skill page, which lists them separately so a
// player sees which numbers apply only to that swing. It sits next to
// WithoutSwingLevers because both must name the same levers.
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
  // What each source gives, keyed by its exclusive group, or by its own name if
  // it has none, so two ungrouped sources still add up. A group gives its best
  // value rather than the sum, like every other lever.
  std::map<std::string, double> by_source;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Their whole book, common nodes included, so this uses HoldsSkillFrom
    // rather than the advancement, which no common node names.
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
  // If the character has no granting skill of their own, the party's counts, as
  // in PartyGrants' buff rule. It is read at the ally's learned level. A skill
  // that grants levels never receives them, so this can't loop.
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
  // Floored after a nudge: the per-level step can't be written exactly, so the
  // top of the ladder lands just under the whole level it should reach.
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
    // A skill that grants nothing replaces nothing. An unlearned Piercing Arrow
    // II leaves the Piercing Arrow it will later replace still swinging.
    if (!skill.supersedes_skill_name().empty() &&
        GrantsAnything(character, skill, bonus, activity)) {
      names.insert(skill.supersedes_skill_name());
    }
    if (skill.replaces_skill_name().empty()) {
      continue;
    }
    // One row of the book shows one of the two forms, and the toggle picks
    // which. A character who never learned the toggle has it off, so every form
    // is dormant for them.
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
    // The three checks every passive passes (whose book, what gear, what
    // level), plus one shared with swings: a skill the book isn't showing has
    // no buff to cast.
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

// The HP and MP pools. A worn percentage adds to what skills grant instead of
// compounding, since both are shares of the same pool. A pendant is not worth
// more for being worn alongside Hyper Body.
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

// Base DEF reads the total stats, not just the allocation, since a ring's LUK
// and a passive's LUK give the same DEF. The percentage then applies to the
// whole pile, as for HP. It can be negative: Reckless Hunt trades DEF for
// attack.
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

// A regeneration pulse gives one more helping per whole step of INT. The
// helping grows but the interval does not. It uses the character's total INT,
// so a ring's INT counts the same as AP.
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

// The character's drop and meso rates: what their gear gives, plus the two
// potions that raise them past what gear alone can reach.
void AddDropAndMesoRates(const CharacterInstance& character,
                         const EquipStats& equipped, Activity preset,
                         DerivedStats& stats) {
  // The worn share is in whole percents and the granted share is a fraction.
  // They add up, like boss damage in OffenseStatsFor.
  stats.item_drop_pct += equipped.item_drop_rate() / 100.0;
  // Meso doesn't: the worn share has its own cap, so it is kept apart until
  // MesoBonus combines the two.
  stats.equip_meso_pct += equipped.meso_rate() / 100.0;
  // The Wealth Acquisition Potion does three things: a meso share past the
  // equipment cap, the same share of drop rate, and a multiplier on the meso
  // from both. It works only when farming, not in boss fights.
  if (preset == Activity::kFarming &&
      character.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION)) {
    stats.meso_pct += kWealthPotionMesoPct;
    stats.item_drop_pct += kWealthPotionDropPct;
    stats.meso_final_mult *= kWealthPotionMesoMult;
  }
  // The Extreme Green Potion is the boss-fight counterpart: attack speed stages
  // that work only in boss fights and can pass the soft cap.
  if (preset == Activity::kBossing &&
      character.ConsumableInEffect(CONSUMABLE_TYPE_EXTREME_GREEN_POTION)) {
    stats.uncapped_attack_speed_bonus += kGreenPotionAttackSpeed;
  }
}

// Pick Pocket and Meso Explosion, which are worth nothing apart. The thrown
// meso follows the swing like a Final Attack but rolls per line, so it is added
// to the same list instead of getting its own mechanism.
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
  meso.credit = passives.meso_skill;
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
  // The activity picks the gear as well as the allocations, unless the caller
  // picked a preset; see stat_preset.h.
  const StatPreset worn =
      gear.value_or(character.SlotFor(PresetKind::kEquip, preset));
  const EquipStats& equipped = character.equip_stats(worn);
  PassiveTotals passives =
      LearnedPassives(character, skills, buffs_up, allies, preset, worn);
  // Before the fold: a potential's %stat and Maple Warrior both read a base the
  // other hasn't changed, and their shares add rather than compound.
  AddPotentials(character, preset, worn, passives);
  FoldApStats(allocated, passives);
  // After the fold, never before: a Hyper Stat is a final stat, and Maple
  // Warrior takes its share of the allocation alone. The activity picks the
  // slot; see stat_preset.h.
  AddHyperStats(character, character.SlotFor(PresetKind::kHyperStats, preset),
                passives);
  AddInnerAbility(character,
                  character.SlotFor(PresetKind::kInnerAbility, preset),
                  passives);
  // Last of all, because it uses crit rate once nothing more will add to it. It
  // reads the rate uncapped and with the base rate included, as the stats page
  // shows it.
  passives.crit_dmg +=
      passives.crit_dmg_per_crit_rate * (passives.crit_rate + kBaseCritRate);

  // Copied from the totals: every field they share is already set, and what
  // follows only changes what the fold has to change.
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
  // Capped at 1. A monster whose attack is fully removed still deals the 1
  // damage GMS always allows.
  stats.enemy_attack_pct = std::min(1.0, stats.enemy_attack_pct);
  // A revival cooldown of 0 means the character never revives, so the reduction
  // stops one second short of 0.
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
  passives.force_pct = derived.force_damage_factor;
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
  // Applied here rather than in skill_stats because it scales the weapon as
  // well as the skill's grant. Both attack fields get it.
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
