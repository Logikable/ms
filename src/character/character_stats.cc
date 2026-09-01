#include "src/character/character_stats.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
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
  return skill.buff().duration_seconds() > 0.0 &&
         (skill.buff().has_ally_base() || skill.buff().has_ally_per_level());
}

// Whether this is the skill that raises other skills' levels. Asked of the
// data rather than of the name, so the rule that it does not raise itself
// holds for any later skill written the same way.
bool GrantsSkillLevels(const Skill& skill) {
  return skill.base().skill_level_bonus() > 0.0;
}

// Whether this skill gives the rest of the party anything -- for good, or for
// as long as its buff stands. See Skill.ally_base and Buff.ally_base.
bool GrantsToAllies(const Skill& skill) {
  return skill.has_ally_base() || skill.has_ally_per_level() ||
         GrantsBuffToAllies(skill);
}

// A fountain as its own skill wrote it, before the character's INT has had
// its say. The step is the INT that buys one more helping, 0 for a pour that
// does not grow -- kept here because what the total INT is cannot be known
// until every passive has been read.
struct RawRegen {
  RegenPulse pulse;
  double int_step = 0.0;
};

// What the learned passives come to, as they are summed. Every lever is
// base + per_level * (L - 1).
//
// It IS a DerivedStats, and the fields it shares with one are the same field:
// the fold at the end of DerivedStatsFor keeps only what it has to transform,
// and everything else arrives already in place. What is added here is the
// pre-fold working: the flat grants that meet the allocation and the worn
// stats before they are a total, and the counts that are worth nothing until
// every passive has been read.
//
// The five inherited fields the fold writes from scratch -- max_hp, max_mp,
// def, base_def and regen_pulses -- stay at nothing while the passives are
// read. What a passive grants toward each is the *_grant or regen field here.
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
  // Boss damage, plain damage and ignored defence a thrown meso carries, once
  // FoldMesoExplosion has cashed in what the skills naming Meso Explosion
  // granted it.
  double meso_boss_pct = 0.0;
  double meso_damage_pct = 0.0;
  double meso_ied = 0.0;
  std::string meso_skill;
  // What the book takes off the shortest revival wait. Summed apart and
  // subtracted once that shortest is known.
  double revive_cooldown_cut = 0.0;
  // Share of what AP bought that comes back as flat stat. Summed, and cashed
  // in against the allocation once every passive is read -- see
  // DerivedStatsFor.
  double ap_stat_pct = 0.0;
  // Combo Orbs, and the bargains priced per orb. The count is the best any
  // learned passive grants rather than the sum -- a character carries one ring
  // of orbs however many skills describe it -- and the bargains are folded
  // against it only once every passive has been read, because the skill
  // offering one is not the skill that says how many orbs there are.
  int combo_orbs = 0;
  int attack_per_combo_orb = 0;
  double final_dmg_pct_per_combo_orb = 0.0;
  double boss_pct_per_combo_orb = 0.0;
  int def_per_combo_orb = 0;
};

// Folds one skill's levers in, on top of whatever is already there. Handed the
// grant already read up to its level -- see EffectAt -- so a lever that meets
// what is there is spelled once here and nowhere else.
//
// Split out from AddPassive because a weapon bonus is a second helping of the
// same levers, gated on the weapon rather than on the skill.
void AddEffect(const SkillEffect& granted, PassiveTotals& totals) {
  totals.hp_grant += granted.max_hp();
  totals.mp_grant += granted.max_mp();
  totals.hp_per_level += granted.max_hp_per_level();
  totals.max_hp_pct += granted.max_hp_pct();
  totals.mp_per_level += granted.max_mp_per_level();
  totals.max_mp_pct += granted.max_mp_pct();
  totals.def_grant += granted.def();
  totals.def_factor *= 1.0 + granted.def_pct();
  totals.str += granted.str();
  totals.dex += granted.dex();
  totals.int_ += granted.int_();
  totals.luk += granted.luk();
  totals.attack += granted.attack();
  totals.attack_pct += granted.attack_pct();
  totals.magic_attack += granted.magic_attack();
  // Damage sent to the MP pool is damage the HP pool never sees, and nothing
  // here tracks MP -- so Magic Guard reads as reduction, which is its whole
  // effect. Reduction multiplies rather than adds: two halves leave a quarter
  // of the hit, where summing them would leave none of it and then go on to
  // heal the character.
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
  totals.damage_reflect_pct += granted.damage_reflect_pct();
  totals.crit_rate += granted.crit_rate();
  totals.crit_dmg += granted.crit_dmg();
  totals.hp_recover_pct += granted.hp_recover_pct();
  totals.exp_pct += granted.exp_pct();
  // The pulse and its interval stay apart all the way to the fight, which
  // pours on the clock rather than smearing it over the seconds between.
  if (granted.regen_interval_seconds() > 0.0 &&
      (granted.regen_pct() > 0.0 || granted.regen_hp() > 0)) {
    totals.regen.push_back(RawRegen{{granted.regen_pct(), granted.regen_hp(),
                                     granted.regen_interval_seconds()},
                                    granted.regen_int_step()});
  }
  totals.status_resistance += granted.status_resistance();
  totals.elemental_resistance += granted.elemental_resistance();
  totals.damage_pct += granted.damage_pct();
  totals.boss_pct += granted.boss_pct();
  totals.meso_pct += granted.meso_pct();
  totals.item_drop_pct += granted.item_drop_pct();
  totals.buff_duration_pct += granted.buff_duration_pct();
  totals.meso_drop_chance += granted.meso_drop_chance();
  totals.mirror_line_pct += granted.mirror_line_pct();
  totals.bonus_attack_lines += granted.bonus_attack_lines();
  totals.attack_per_combo_orb += granted.attack_per_combo_orb();
  totals.final_dmg_pct_per_combo_orb += granted.final_dmg_pct_per_combo_orb();
  totals.boss_pct_per_combo_orb += granted.boss_pct_per_combo_orb();
  totals.def_per_combo_orb += granted.def_per_combo_orb();
  totals.ap_stat_pct += granted.ap_stat_pct();
  // Read here rather than beside the cap itself, so that a BUFF granting
  // either lands them: a buff folds in through this door alone.
  totals.freeze_cap_bonus += granted.freeze_stack_cap_bonus();
  totals.freeze.matt_per_stack += granted.magic_attack_per_freeze_stack();
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
  totals.attack_speed_bonus += granted.attack_speed();
  totals.ied = CombineIgnoredDefense(totals.ied, granted.ied_pct());
  // The one lever taken at its best rather than summed: two masteries are not
  // twice as steady a swing, they are the better of the two.
  totals.mastery = std::max(totals.mastery, granted.mastery());
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
  source.lines = std::max(1, granted.final_attack_lines());
  source.required_tag = skill.follows_skill_tag();
  source.single_enemy = skill.final_attack_single_enemy();
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

// Folds Freezing Crush in. Two skills granting any of it leave the deeper pile
// and the better stack standing rather than summing two, which is what lets
// Frost Clutch better a stack without naming a pile of its own. A character
// holding no cap at all is emptied out again by FoldFreezeStacks.
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

// Folds in what the enemy's own condition is worth. The better of each stands
// rather than the sum, as the scar and the freeze do: an afflicted monster is
// afflicted, and Fervent Drain raising Elemental Drain's rate is one rate.
void AddEnemyCondition(const Skill& skill, const SkillEffect& granted,
                       PassiveTotals& totals) {
  totals.condition.final_dmg_pct_when_afflicted =
      std::max(totals.condition.final_dmg_pct_when_afflicted,
               granted.final_dmg_pct_when_afflicted());
  totals.condition.final_dmg_pct_per_dot = std::max(
      totals.condition.final_dmg_pct_per_dot, granted.final_dmg_pct_per_dot());
  totals.condition.dot_count_cap =
      std::max(totals.condition.dot_count_cap, skill.dot_count_cap());
}

// Notes Meso Explosion down. Recorded rather than folded: Meso Mastery's
// points land on each of its lines, the two skills fold in catalog order, and
// so the pair cannot be settled until every passive is in. See
// FoldMesoExplosion.
void AddMesoExplosion(const Skill& skill, const SkillEffect& granted, int level,
                      PassiveTotals& totals) {
  double per_line = granted.meso_hit_pct();
  if (per_line <= 0.0) {
    return;
  }
  totals.meso_skill = skill.name();
  totals.meso_hit_pct += per_line;
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
}

// Notes down what `skill` hands other skills by name. Kept out of AddEffect,
// which is handed levers with no skill behind them: which skill is
// strengthened is written on the boost, not on the lever.
void AddSkillBonuses(const Skill& skill, int level, PassiveTotals& totals) {
  for (const SkillBoost& boost : skill.boost()) {
    AddSkillBonus(boost, level, totals.skill_bonus[boost.skill_name()]);
    // A boost that follows the skill into its empowered form is filed under
    // the form's name too: the form is a swing of its own, and the fight looks
    // its entry up by the name it swings under.
    if (boost.reaches_empowered_form()) {
      AddSkillBonus(boost, level,
                    totals.skill_bonus[EmpoweredSkillName(boost.skill_name())]);
    }
  }
}

void AddPassive(const Skill& skill, int level, EquipType weapon,
                PassiveTotals& totals) {
  SkillEffect granted = EffectAt(skill.base(), skill.per_level(), level);
  if (skill.kind() == SKILL_KIND_ATTACK) {
    AddEffect(WithoutSwingLevers(granted), totals);
    // The half an attack states apart because it keeps it: no lever of this
    // one leaves with the swing. See Skill.passive.
    AddEffect(EffectAt(skill.passive(), skill.passive_per_level(), level),
              totals);
  } else {
    AddEffect(granted, totals);
  }
  AddSkillBonuses(skill, level, totals);
  AddFinalAttack(skill, granted, totals);
  AddProc(skill, level, totals);
  AddFreezeStacks(skill, granted, totals);
  AddScar(granted, totals);
  AddEnemyCondition(skill, granted, totals);
  // A burn on a PASSIVE belongs to the character rather than to one swing: the
  // poison stays on the claw, so everything the claw hits takes it. One on an
  // attack is that swing's own, and one on a summon is its pulses' -- both are
  // read where those are built.
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

// Turns what one line of a thrown meso is worth into what one whole meso is,
// now that Meso Mastery's points are certain to be in. Meso Explosion is not a
// swing, so everything the book aims at it by name is cashed in here rather
// than in OffenseStatsFor.
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
  }
  totals.meso_hit_pct *= totals.meso_lines;
}

// Hands each Final Attack what the book aimed at the skill that sets it off.
// Folded here rather than where the source is built, because the skill
// granting the boost may be read after the skill carrying the Final Attack --
// and a boost aimed at a passive reaches nothing else: what it strengthens is
// the extra hit, not a swing. See SkillBoost::effect.
void FoldFinalAttackBoosts(PassiveTotals& totals) {
  for (FinalAttackSource& source : totals.final_attacks) {
    std::map<std::string, SkillBonus>::const_iterator boost =
        totals.skill_bonus.find(source.skill_name);
    if (source.skill_name.empty() || boost == totals.skill_bonus.end()) {
      continue;
    }
    source.chance += boost->second.final_attack_chance;
    source.damage_bonus_pct += boost->second.damage_pct;
    // Points on the strike's own multiplier, which is what GMS's "Night Lord's
    // Mark Damage: +100% points" is -- the other damage a boost can hand a
    // Final Attack, and the one every star of it lands. Only where the skill
    // carrying it does not swing: the same points already land on a swing of
    // that name, and no grant is read twice.
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
// deeper, so the bonus pays only where there is a pile: a character who never
// learned the mechanism holds no stacks for Glacial Fury to deepen or to pay
// for.
void FoldFreezeStacks(PassiveTotals& totals) {
  if (totals.freeze.cap <= 0) {
    totals.freeze = FreezeStacks{};
    return;
  }
  totals.freeze.cap += totals.freeze_cap_bonus;
}

void FoldComboOrbs(PassiveTotals& totals) {
  totals.attack += totals.attack_per_combo_orb * totals.combo_orbs;
  totals.boss_pct += totals.boss_pct_per_combo_orb * totals.combo_orbs;
  totals.def_grant += totals.def_per_combo_orb * totals.combo_orbs;
  double orbs = totals.final_dmg_pct_per_combo_orb * totals.combo_orbs;
  totals.final_dmg_pct = (1.0 + totals.final_dmg_pct) * (1.0 + orbs) - 1.0;
}

// Whether `skill` puts up a timed buff -- one the character has for a while
// rather than for good. See Skill.buff.
bool GrantsBuff(const Skill& skill) {
  return skill.buff().duration_seconds() > 0.0;
}

// Whether this character reads anything at all off `skill`: their own book,
// the gear it demands in hand, and a level in it. Asked twice -- once to fold
// the skill in and once to let it supersede another -- because a skill
// granting nothing must not be replacing anything either.
//
// Gear lapses the effect rather than the skill: Final Attack does not fire off
// a wand and Shield Mastery does nothing with an empty off hand, but both stay
// learned.
bool GrantsAnything(const CharacterInstance& character, const Skill& skill,
                    int bonus) {
  // Learned levels are keyed by display name, and the warrior branches share
  // several names -- so only the character's own book counts, or the other
  // branch's copy would fold in beside it.
  return character.HasAdvancement(skill.job_advancement()) &&
         SkillGearMet(character, skill) &&
         EffectiveSkillLevel(character, skill, bonus) > 0;
}

// What the rest of the party is holding over this character. Gathered whole
// before anything folds, because both rules that thin the list -- the buff
// rule and the party's supersessions -- need every ally read first.
//
// An ally's own Combat Orders lifts what they grant, but a level the party
// granted THEM does not. See DerivedStatsFor.
std::vector<AllyGrant> PartyGrants(const CharacterInstance& character,
                                   const std::map<std::string, Skill>& skills,
                                   absl::Span<const CharacterInstance> allies) {
  std::map<std::string, AllyGrant> best;
  std::vector<AllyGrant> stacking;
  std::set<std::string> superseded;
  for (const CharacterInstance& ally : allies) {
    int bonus = BonusSkillLevels(ally, skills);
    std::set<std::string> theirs = DormantSkillNames(ally, skills, bonus);
    for (const std::pair<const std::string, Skill>& entry : skills) {
      const Skill& skill = entry.second;
      // An Advanced X states the whole of the X it replaces, its party half
      // included -- so a Bishop hands out Blessed Harmony and not the Blessed
      // Ensemble under it.
      if (theirs.count(skill.name()) > 0 || !GrantsToAllies(skill) ||
          !GrantsAnything(ally, skill, bonus)) {
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
  // A stacking grant answers to neither rule below. It is not a buff standing
  // over the party -- it pays for the company kept, so a second Cleric is a
  // second payment, and a Bishop's book replacing their own copy does not
  // reach the Cleric's.
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
  return grants;
}

// Sums every passive the character has learned. HP has to know its whole flat
// total before any percentage lands on it, so nothing is folded here.
PassiveTotals LearnedPassives(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills,
                              absl::Span<const Skill* const> buffs_up,
                              absl::Span<const CharacterInstance> allies) {
  PassiveTotals totals;
  EquipType weapon = character.weapon_type();
  int bonus = BonusSkillLevels(character, skills, allies);
  std::set<std::string> superseded =
      DormantSkillNames(character, skills, bonus);
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
    // Every kind is read, not only the passives: GMS hangs permanent grants off
    // active skills too, and marks them "[Passive Effects: ...]" when it does.
    // Phoenix is the first here -- a summon that also raises DEF for good. A
    // skill with no lever contributes nothing whatever kind it is, so this
    // costs the rest of the catalog nothing.
    if (!GrantsAnything(character, skill, bonus)) {
      continue;
    }
    AddPassive(skill, EffectiveSkillLevel(character, skill, bonus), weapon,
               totals);
  }
  // A set bonus grants what a passive grants, so it folds in through the same
  // door. It carries no level and no per-level step: a tier is worth what it
  // says however far the character has come.
  for (const SkillEffect& bonus : character.set_bonuses()) {
    AddEffect(bonus, totals);
  }
  // A buff standing right now grants what a passive grants for as long as it
  // is up, and folds in through the same door for the same reason -- as a
  // source of its own, so its ignored defence combines with the character's
  // rather than summing with it.
  for (const Skill* skill : buffs_up) {
    AddEffect(EffectAt(skill->buff().base(), skill->buff().per_level(),
                       EffectiveSkillLevel(character, *skill, bonus)),
              totals);
  }
  // What the party is holding over them, at the level its caster has it. The
  // same door again, and for the same reason.
  for (const AllyGrant& grant : PartyGrants(character, skills, allies)) {
    AddEffect(EffectAt(grant.skill->ally_base(), grant.skill->ally_per_level(),
                       grant.level),
              totals);
  }
  FoldMesoExplosion(totals);
  FoldFinalAttackBoosts(totals);
  FoldFreezeStacks(totals);
  FoldScar(totals);
  FoldEnemyCondition(totals);
  FoldComboOrbs(totals);
  return totals;
}

// What the character's Hyper Stats add, on top of everything their book
// granted. The four stats land here rather than in the allocation because
// GMS calls them final stat: nothing takes a percentage of them, which is
// exactly what a passive's flat grant already gets.
//
// Percentages arrive as whole percents and are divided here, the way a worn
// item's are. Arcane Force is not among them -- it is not a stat the damage
// chain reads, and CharacterInstance::arcane_force answers for it.
void AddHyperStats(const CharacterInstance& character, StatPreset preset,
                   PassiveTotals& totals) {
  static_assert(HyperStatField_ARRAYSIZE == 16,
                "a new Hyper Stat needs somewhere to land");
  if (character.proto().level() < kHyperStatUnlockLevel) {
    return;
  }
  totals.str += static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_STR, preset));
  totals.dex += static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_DEX, preset));
  totals.int_ += static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_INT, preset));
  totals.luk += static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_LUK, preset));
  totals.max_hp_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_MAX_HP, preset) /
      kPercentToFraction;
  totals.crit_rate +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_CRIT_RATE, preset) /
      kPercentToFraction;
  totals.crit_dmg +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_CRIT_DAMAGE, preset) /
      kPercentToFraction;
  // Ignored defence meets what the book already ignores in reverse, the way
  // two sources of it always meet.
  totals.ied = CombineIgnoredDefense(
      totals.ied, character.hyper_stat_bonus(HYPER_STAT_FIELD_IED, preset) /
                      kPercentToFraction);
  totals.damage_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_DAMAGE, preset) /
      kPercentToFraction;
  totals.boss_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_BOSS_DAMAGE, preset) /
      kPercentToFraction;
  totals.normal_pct +=
      character.hyper_stat_bonus(HYPER_STAT_FIELD_NORMAL_DAMAGE, preset) /
      kPercentToFraction;
  // One stat pays both, so a magician and a warrior read the same row.
  int attack = static_cast<int>(
      character.hyper_stat_bonus(HYPER_STAT_FIELD_ATTACK, preset));
  totals.attack += attack;
  totals.magic_attack += attack;
  totals.exp_pct += character.hyper_stat_bonus(HYPER_STAT_FIELD_EXP, preset) /
                    kPercentToFraction;
}

// What the character's Inner Ability adds, line by line. The stats land here
// for the same reason a Hyper Stat's do -- GMS calls them final stat, and the
// %stat Maple Warrior grants has already taken its share of the allocation.
// The two attacks do not: GMS scales them the way it scales any other source,
// which is what %ATT over the summed total already does.
//
// A line below the unlock level pays nothing, so the panel opening and the
// stats arriving are one event.
void AddInnerAbility(const CharacterInstance& character, StatPreset preset,
                     PassiveTotals& totals) {
  static_assert(AbilityLineType_ARRAYSIZE == 17,
                "a new Inner Ability line needs somewhere to land");
  if (!character.inner_ability_unlocked()) {
    return;
  }
  for (const AbilityLine& line : character.ability(preset).lines()) {
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

// Cashes Maple Warrior in against the AP the character has spent. It grants
// what a ring grants, so it lands in the same pile the passives' flat stats
// do -- and it is read here rather than in AddEffect because a skill's levers
// know nothing about the character carrying them.
//
// Rounded down per stat, as GMS rounds it, and nudged first for the reason
// FoldPercent is: a per-level step that cannot be written exactly lands a hair
// under the share it climbs to.
void FoldApStats(const AllocatedStats& allocated, PassiveTotals& totals) {
  if (totals.ap_stat_pct <= 0.0) {
    return;
  }
  const int stats[] = {allocated.str(), allocated.dex(), allocated.int_(),
                       allocated.luk()};
  int granted[4];
  for (int i = 0; i < 4; ++i) {
    granted[i] = static_cast<int>(
        std::floor(stats[i] * totals.ap_stat_pct + kPercentEpsilon));
  }
  totals.str += granted[0];
  totals.dex += granted[1];
  totals.int_ += granted[2];
  totals.luk += granted[3];
}

// A flat total, then the percentage over the whole of it, with the fraction
// dropped. Every pile that takes a percentage folds through here: the HP and
// MP pools, DEF, and what the character swings with. The nudge before the
// floor is for the percentage: summing a skill's per-level steps lands a hair
// under the round figure (16 levels of +1% is 0.15999...), which would
// otherwise cost a whole point.
int FoldPercent(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + kPercentEpsilon));
}

}  // namespace

// The levers an attack keeps for its own swing rather than handing to the
// character. Stripped here, and read back in OffenseStatsFor against the skill
// being swung -- so Gungnir's Descent ignores 30% of a monster's defence when
// it lands and Dark Impale, swung a moment later, does not. Snipe's certain
// critical is the third of them, and Mist Eruption's final damage the fourth:
// GMS pays that for the mists the cast set off, which is a fact about the cast.
// The fifth is not damage at all -- Angel Ray heals the Bishop as it lands,
// and the swing beside it does nothing of the kind.
//
// Only a swing keeps them. A skill on its own clock is not one the character
// chose, and GMS writes these on a summon only under "[Passive Effects]",
// meaning the character -- which is how Arrow Illusion's ignored defence
// follows the Marksman rather than staying with the decoy.
SkillEffect WithoutSwingLevers(const SkillEffect& effect) {
  SkillEffect kept = effect;
  kept.clear_ied_pct();
  kept.clear_boss_pct();
  kept.clear_crit_rate();
  kept.clear_final_dmg_pct();
  kept.clear_hp_recover_pct();
  kept.clear_meso_drop_cut();
  kept.clear_max_hp_damage_pct();
  return kept;
}

// The other half, for the skill page, which heads the two apart so a player
// can see which numbers leave with the swing. Written beside the function it
// is the complement of: the two must name the same levers, and apart they
// would drift.
SkillEffect SwingLeversOf(const SkillEffect& effect) {
  SkillEffect swing;
  swing.set_ied_pct(effect.ied_pct());
  swing.set_boss_pct(effect.boss_pct());
  swing.set_crit_rate(effect.crit_rate());
  swing.set_final_dmg_pct(effect.final_dmg_pct());
  swing.set_hp_recover_pct(effect.hp_recover_pct());
  swing.set_meso_drop_cut(effect.meso_drop_cut());
  swing.set_max_hp_damage_pct(effect.max_hp_damage_pct());
  return swing;
}

bool SkillAllowsWeapon(const Skill& skill, EquipType weapon) {
  return ListAllowsWeapon(skill.required_equip_type(), weapon);
}

int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills,
                     absl::Span<const CharacterInstance> allies) {
  double bonus = 0.0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (!GrantsSkillLevels(skill) ||
        !character.HasAdvancement(skill.job_advancement())) {
      continue;
    }
    int level = character.skill_level(skill);
    if (level > 0) {
      bonus += skill.base().skill_level_bonus() +
               skill.per_level().skill_level_bonus() * (level - 1);
    }
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
            !ally.HasAdvancement(skill.job_advancement())) {
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
  if (learned <= 0 || GrantsSkillLevels(skill)) {
    return learned;
  }
  int ceiling = skill.max_level();
  if (skill.exceeds_master_level()) {
    ceiling += kLevelsPastMasterLevel;
  }
  return std::min(learned + bonus, ceiling);
}

int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus) {
  return LevelWithBonus(skill, character.skill_level(skill), bonus);
}

std::set<std::string> DormantSkillNames(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus) {
  std::set<std::string> names;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // A skill granting nothing replaces nothing: an unlearned Piercing Arrow
    // II leaves the Piercing Arrow it will one day take over still swinging.
    if (!skill.supersedes_skill_name().empty() &&
        GrantsAnything(character, skill, bonus)) {
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

bool SkillGearMet(const CharacterInstance& character, const Skill& skill) {
  if (skill.requires_secondary() && !character.has_secondary()) {
    return false;
  }
  return SkillAllowsWeapon(skill, character.weapon_type());
}

std::vector<const Skill*> BuffSkillsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills) {
  std::vector<const Skill*> buffs;
  std::set<std::string> dormant =
      DormantSkillNames(character, skills, BonusSkillLevels(character, skills));
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // The same three gates every passive passes: whose book it is, whether the
    // gear it demands is in hand, and whether it is learned at all -- plus the
    // one a buff shares with a swing, since a skill the book is not showing
    // has no buff to raise either.
    if (!GrantsBuff(skill) ||
        !character.HasAdvancement(skill.job_advancement()) ||
        !SkillGearMet(character, skill) || character.skill_level(skill) <= 0 ||
        dormant.count(skill.name()) > 0) {
      continue;
    }
    buffs.push_back(&skill);
  }
  return buffs;
}

double BuffDurationPctFor(const CharacterInstance& character,
                          const std::map<std::string, Skill>& skills) {
  return LearnedPassives(character, skills, {}, {}).buff_duration_pct;
}

std::vector<AllyGrant> AllyBuffsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills,
    absl::Span<const CharacterInstance> allies) {
  std::vector<AllyGrant> buffs;
  for (const AllyGrant& grant : PartyGrants(character, skills, allies)) {
    if (GrantsBuffToAllies(*grant.skill)) {
      buffs.push_back(grant);
    }
  }
  return buffs;
}

DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills,
                             absl::Span<const Skill* const> buffs_up,
                             absl::Span<const CharacterInstance> allies,
                             StatPreset preset) {
  const Character& proto = character.proto();
  const AllocatedStats& allocated = proto.allocated_stats();
  const EquipStats& equipped = character.equip_stats();
  PassiveTotals passives = LearnedPassives(character, skills, buffs_up, allies);
  FoldApStats(allocated, passives);
  // After the fold, never before it: a Hyper Stat is final stat, and Maple
  // Warrior takes its share of the allocation alone.
  AddHyperStats(character, preset, passives);
  AddInnerAbility(character, preset, passives);

  // Sliced off the totals: every lever the two share is already in place, and
  // what is left below is only what the fold has to change.
  DerivedStats stats = passives;
  // A worn percentage sums with what the skills grant rather than compounding
  // with it, the same deal item_drop_rate takes: both are shares of the one
  // pile, and the pendant is not worth more for being worn beside Hyper Body.
  stats.max_hp =
      FoldPercent(allocated.hp() + equipped.max_hp() + passives.hp_grant +
                      passives.hp_per_level * proto.level(),
                  passives.max_hp_pct + equipped.max_hp_pct() / 100.0);
  stats.max_mp =
      FoldPercent(allocated.mp() + equipped.max_mp() + passives.mp_grant +
                      passives.mp_per_level * proto.level(),
                  passives.max_mp_pct + equipped.max_mp_pct() / 100.0);
  stats.skill_stats.set_def(passives.def_grant);
  stats.skill_stats.set_str(passives.str);
  stats.skill_stats.set_dex(passives.dex);
  stats.skill_stats.set_int_(passives.int_);
  stats.skill_stats.set_luk(passives.luk);
  stats.skill_stats.set_attack(passives.attack);
  stats.skill_stats.set_magic_attack(passives.magic_attack);
  // Base DEF reads the totals rather than the allocation: a ring's LUK and a
  // passive's LUK are worth the same DEF. Floored once at the end, as GMS
  // shows it -- the worn and granted DEF are whole numbers already.
  int str = allocated.str() + equipped.str() + passives.str;
  int dex = allocated.dex() + equipped.dex() + passives.dex;
  int luk = allocated.luk() + equipped.luk() + passives.luk;
  stats.base_def = static_cast<int>(
      std::floor(kDefPerStr * str + kDefPerDexLuk * (dex + luk)));
  // The percentage lands over the whole pile, exactly as it does on the HP
  // pool: what a character wears and what their stats buy are the same DEF.
  // It can also be a loss -- Reckless Hunt buys attack by giving DEF up -- and
  // a character deep enough in the red ends with less DEF than their stats
  // alone bought them.
  stats.def = FoldPercent(stats.base_def + equipped.def() + passives.def_grant,
                          passives.def_factor - 1.0);
  // Floored at nothing rather than clamped to a share: a monster stripped of
  // the whole of its attack still lands the 1 damage GMS insists on.
  stats.enemy_attack_pct = std::min(1.0, stats.enemy_attack_pct);
  // A pact that came back at once would read as no pact at all -- 0 is what
  // says a character is never revived -- so the cut stops a second short.
  if (stats.revive_cooldown_seconds > 0.0) {
    stats.revive_cooldown_seconds = std::max(
        1.0, stats.revive_cooldown_seconds - passives.revive_cooldown_cut);
  }
  // A fountain pours one more helping per whole step of INT, so Holy Water
  // puts back twice its stated share at 2500 and three times it at 5000. The
  // helping grows; the clock does not. Charged against the character's WHOLE
  // INT -- what a ring grants and what Maple Warrior grants back count the
  // same as what AP bought.
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
  // The worn share is whole percents and the granted share a fraction. They
  // meet by summing, the way boss damage does in OffenseStatsFor.
  stats.item_drop_pct += equipped.item_drop_rate() / 100.0;
  // Meso does not: what is worn is capped on its own, so it is kept apart
  // until MesoBonus puts the two together.
  stats.equip_meso_pct += equipped.meso_rate() / 100.0;
  // The Wealth Acquisition Potion, worth three things at once: a share past
  // the equipment cap, the same share of drop rate, and a multiplier over the
  // purse the two of them fill.
  if (character.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION)) {
    stats.meso_pct += kWealthPotionMesoPct;
    stats.item_drop_pct += kWealthPotionDropPct;
    stats.meso_final_mult *= kWealthPotionMesoMult;
  }
  // Pick Pocket and Meso Explosion, worth nothing apart: a meso falls out of
  // an enemy and is thrown straight back at them. It rides the swing exactly
  // as a Final Attack does, except that the roll is per line -- so it is one
  // more source in the same list rather than a mechanism of its own.
  if (passives.meso_drop_chance > 0.0 && passives.meso_hit_pct > 0.0) {
    FinalAttackSource meso;
    meso.chance = passives.meso_drop_chance;
    meso.damage_pct = passives.meso_hit_pct;
    meso.boss_pct = passives.meso_boss_pct;
    meso.damage_bonus_pct = passives.meso_damage_pct;
    meso.ied = passives.meso_ied;
    meso.per_line = true;
    stats.final_attacks.push_back(meso);
  }
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
  passives.skill_bonus = derived.skill_bonus;
  passives.arcane_pct = derived.arcane_damage_factor;
  return passives;
}

EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived) {
  const EquipStats sources[] = {character.equip_stats(), derived.skill_stats};
  EquipStats total = SumEquipStats(absl::MakeConstSpan(sources));
  // The percentage lands here rather than in skill_stats, because what it
  // scales is the weapon in the character's hand as much as the skill's own
  // grant. Both attack fields take it: a magician swings on magic attack, and
  // a percentage of what you swing on means the same thing either way.
  total.set_attack(FoldPercent(total.attack(), derived.attack_pct));
  total.set_magic_attack(FoldPercent(total.magic_attack(), derived.attack_pct));
  return total;
}

}  // namespace ms
