#include "src/character/character_stats.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/combat/damage.h"
#include "src/item/equip_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Slack for the floor below, far smaller than any percentage a skill grants.
constexpr double kPercentEpsilon = 1e-9;

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

// Whether this is the skill that raises other skills' levels. Asked of the
// data rather than of the name, so the rule that it does not raise itself
// holds for any later skill written the same way.
bool GrantsSkillLevels(const Skill& skill) {
  return skill.base().skill_level_bonus() > 0.0;
}

// Whether this skill gives the rest of the party anything. See
// Skill.ally_base.
bool GrantsToAllies(const Skill& skill) {
  return skill.has_ally_base() || skill.has_ally_per_level();
}

// A fountain as its own skill wrote it, before the character's INT has had
// its say. The step is the INT that buys one more helping, 0 for a pour that
// does not grow -- kept here because what the total INT is cannot be known
// until every passive has been read.
struct RawRegen {
  RegenPulse pulse;
  double int_step = 0.0;
};

// What one learned passive is worth at `level`, in the shape they are summed
// in. Every lever is base + per_level * (L - 1).
struct PassiveTotals {
  int max_hp = 0;
  int max_mp = 0;
  int hp_per_level = 0;
  double max_hp_pct = 0.0;
  int mp_per_level = 0;
  double max_mp_pct = 0.0;
  int def = 0;
  // Held as the factor the DEF pile is multiplied by rather than as a sum of
  // percentages, because two sources multiply: Phoenix's +30% and Reckless
  // Hunt's -25% leave 97.5% of the armour, not 105% of it.
  double def_factor = 1.0;
  int str = 0;
  int dex = 0;
  int int_ = 0;
  int luk = 0;
  int attack = 0;
  double attack_pct = 0.0;
  int magic_attack = 0;
  double damage_taken_pct = 0.0;
  double dodge_chance = 0.0;
  double damage_reflect_pct = 0.0;
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
  double mastery = 0.0;
  double hp_recover_pct = 0.0;
  double exp_pct = 0.0;
  // One entry per skill granting a fountain, in catalog order.
  std::vector<RawRegen> regen;
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // One per skill granting one, in catalog order. Two that follow the same
  // swings stay apart: they are independent rolls.
  std::vector<FinalAttackSource> final_attacks;
  // The burns a passive leaves on every swing, likewise.
  std::vector<CharacterDot> dots;
  // The chances a passive gives every swing to land harder on one enemy.
  std::vector<SwingProc> procs;
  // What a Freeze Stack is worth, and how many the character holds.
  FreezeStacks freeze;
  // Pick Pocket's chance and Meso Explosion's damage, which live on two
  // different skills and are worth nothing apart -- totalled here and paired
  // once the fold is done.
  double meso_drop_chance = 0.0;
  // Per line until FoldMesoExplosion multiplies the count in.
  double meso_hit_pct = 0.0;
  int meso_lines = 1;
  // Boss damage a thrown meso carries, once FoldMesoExplosion has cashed in
  // what the skills naming Meso Explosion granted it.
  double meso_boss_pct = 0.0;
  std::string meso_skill;
  double meso_pct = 0.0;
  double item_drop_pct = 0.0;
  double buff_duration_pct = 0.0;
  // The shortest wait between revivals any passive grants, and 0 for the
  // characters no passive revives.
  double revive_cooldown_seconds = 0.0;
  // Share of what AP bought that comes back as flat stat. Summed, and cashed
  // in against the allocation once every passive is read -- see
  // DerivedStatsFor.
  double ap_stat_pct = 0.0;
  // What the book hands one named skill apiece. Summed per name, so two
  // passives strengthening the same swing both count.
  std::map<std::string, SkillBonus> skill_bonus;
  double damage_pct = 0.0;
  double boss_pct = 0.0;
  double mirror_line_pct = 0.0;
  int bonus_attack_lines = 0;
  double final_dmg_pct = 0.0;
  double ied = 0.0;
  int attack_speed = 0;
  // Combo Orbs, and the two bargains priced per orb. The count is the best any
  // learned passive grants rather than the sum -- a character carries one ring
  // of orbs however many skills describe it -- and the bargains are folded
  // against it only once every passive has been read, because the skill
  // offering one is not the skill that says how many orbs there are.
  int combo_orbs = 0;
  int attack_per_combo_orb = 0;
  double final_dmg_pct_per_combo_orb = 0.0;
};

// Folds one skill's levers in at `level`, on top of whatever is already there.
// Split out from AddPassive because a weapon bonus is a second helping of the
// same levers, gated on the weapon rather than on the skill.
void AddEffect(const SkillEffect& base, const SkillEffect& per, int level,
               PassiveTotals& totals) {
  totals.max_hp += base.max_hp() + per.max_hp() * (level - 1);
  totals.max_mp += base.max_mp() + per.max_mp() * (level - 1);
  totals.hp_per_level +=
      base.max_hp_per_level() + per.max_hp_per_level() * (level - 1);
  totals.max_hp_pct += base.max_hp_pct() + per.max_hp_pct() * (level - 1);
  totals.mp_per_level +=
      base.max_mp_per_level() + per.max_mp_per_level() * (level - 1);
  totals.max_mp_pct += base.max_mp_pct() + per.max_mp_pct() * (level - 1);
  totals.def += base.def() + per.def() * (level - 1);
  totals.def_factor *= 1.0 + base.def_pct() + per.def_pct() * (level - 1);
  totals.str += base.str() + per.str() * (level - 1);
  totals.dex += base.dex() + per.dex() * (level - 1);
  totals.int_ += base.int_() + per.int_() * (level - 1);
  totals.luk += base.luk() + per.luk() * (level - 1);
  totals.attack += base.attack() + per.attack() * (level - 1);
  totals.attack_pct += base.attack_pct() + per.attack_pct() * (level - 1);
  totals.magic_attack += base.magic_attack() + per.magic_attack() * (level - 1);
  // Damage sent to the MP pool is damage the HP pool never sees, and nothing
  // here tracks MP -- so Magic Guard reads as reduction, which is its whole
  // effect. Reduction multiplies rather than adds: two halves leave a quarter
  // of the hit, where summing them would leave none of it and then go on to
  // heal the character.
  double taken = base.damage_taken_pct() + per.damage_taken_pct() * (level - 1);
  double to_mp = base.damage_to_mp_pct() + per.damage_to_mp_pct() * (level - 1);
  totals.damage_taken_pct =
      1.0 - (1.0 - totals.damage_taken_pct) * (1.0 - taken) * (1.0 - to_mp);
  // Dodging combines the same way and for the same reason: what two sources
  // leave standing is the product of what each leaves standing.
  double dodge = base.dodge_chance() + per.dodge_chance() * (level - 1);
  totals.dodge_chance = 1.0 - (1.0 - totals.dodge_chance) * (1.0 - dodge);
  totals.damage_reflect_pct +=
      base.damage_reflect_pct() + per.damage_reflect_pct() * (level - 1);
  totals.crit_rate += base.crit_rate() + per.crit_rate() * (level - 1);
  totals.crit_dmg += base.crit_dmg() + per.crit_dmg() * (level - 1);
  totals.hp_recover_pct +=
      base.hp_recover_pct() + per.hp_recover_pct() * (level - 1);
  totals.exp_pct += base.exp_pct() + per.exp_pct() * (level - 1);
  // The pulse and its interval stay apart all the way to the fight, which
  // pours on the clock rather than smearing it over the seconds between.
  double regen_interval = base.regen_interval_seconds() +
                          per.regen_interval_seconds() * (level - 1);
  double regen_pct = base.regen_pct() + per.regen_pct() * (level - 1);
  if (regen_interval > 0.0 && regen_pct > 0.0) {
    double step = base.regen_int_step() + per.regen_int_step() * (level - 1);
    totals.regen.push_back(RawRegen{{regen_pct, regen_interval}, step});
  }
  totals.status_resistance +=
      base.status_resistance() + per.status_resistance() * (level - 1);
  totals.elemental_resistance +=
      base.elemental_resistance() + per.elemental_resistance() * (level - 1);
  totals.damage_pct += base.damage_pct() + per.damage_pct() * (level - 1);
  totals.boss_pct += base.boss_pct() + per.boss_pct() * (level - 1);
  totals.meso_pct += base.meso_pct() + per.meso_pct() * (level - 1);
  totals.item_drop_pct +=
      base.item_drop_pct() + per.item_drop_pct() * (level - 1);
  totals.buff_duration_pct +=
      base.buff_duration_pct() + per.buff_duration_pct() * (level - 1);
  totals.meso_drop_chance +=
      base.meso_drop_chance() + per.meso_drop_chance() * (level - 1);
  totals.mirror_line_pct +=
      base.mirror_line_pct() + per.mirror_line_pct() * (level - 1);
  totals.bonus_attack_lines +=
      base.bonus_attack_lines() + per.bonus_attack_lines() * (level - 1);
  totals.attack_per_combo_orb +=
      base.attack_per_combo_orb() + per.attack_per_combo_orb() * (level - 1);
  totals.final_dmg_pct_per_combo_orb +=
      base.final_dmg_pct_per_combo_orb() +
      per.final_dmg_pct_per_combo_orb() * (level - 1);
  totals.ap_stat_pct += base.ap_stat_pct() + per.ap_stat_pct() * (level - 1);
  // The shortest wait rather than the sum: two pacts are not one long one,
  // and what a character wants to know is how soon the next one comes.
  double revive = base.revive_cooldown_seconds() +
                  per.revive_cooldown_seconds() * (level - 1);
  if (revive > 0.0 && (totals.revive_cooldown_seconds <= 0.0 ||
                       revive < totals.revive_cooldown_seconds)) {
    totals.revive_cooldown_seconds = revive;
  }
  totals.attack_speed += base.attack_speed() + per.attack_speed() * (level - 1);
  totals.ied = CombineIgnoredDefense(
      totals.ied, base.ied_pct() + per.ied_pct() * (level - 1));
  // The one lever taken at its best rather than summed: two masteries are not
  // twice as steady a swing, they are the better of the two.
  totals.mastery =
      std::max(totals.mastery, base.mastery() + per.mastery() * (level - 1));
  // Final damage is the one that multiplies: two sources of 10% are worth 21%.
  // Kept as the combined fraction, since that is the single number the damage
  // chain applies.
  double final_dmg = base.final_dmg_pct() + per.final_dmg_pct() * (level - 1);
  totals.final_dmg_pct = (1.0 + totals.final_dmg_pct) * (1.0 + final_dmg) - 1.0;
}

// Folds one skill's Final Attack in. Split from AddEffect because what sets a
// Final Attack off belongs to the SKILL, not to the level's levers -- and
// AddEffect is handed levers with no skill behind them.
void AddFinalAttack(const Skill& skill, const SkillEffect& base,
                    const SkillEffect& per, int level, PassiveTotals& totals) {
  FinalAttackSource source;
  source.chance =
      base.final_attack_chance() + per.final_attack_chance() * (level - 1);
  source.damage_pct =
      base.final_attack_pct() + per.final_attack_pct() * (level - 1);
  if (source.chance <= 0.0 || source.damage_pct <= 0.0) {
    return;
  }
  source.lines = std::max(
      1, base.final_attack_lines() + per.final_attack_lines() * (level - 1));
  source.required_tag = skill.follows_skill_tag();
  source.single_enemy = skill.final_attack_single_enemy();
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
  rolled.damage_pct =
      proc.base().damage_pct() + proc.per_level().damage_pct() * (level - 1);
  rolled.hp_recover_pct = proc.base().hp_recover_pct() +
                          proc.per_level().hp_recover_pct() * (level - 1);
  totals.procs.push_back(rolled);
}

// Folds Freezing Crush in. The cap and what a stack is worth live on the one
// skill, so they are read together; a second skill granting any would leave
// the deeper pile and the better stack standing rather than summing two.
void AddFreezeStacks(const Skill& skill, int level, PassiveTotals& totals) {
  if (skill.freeze_stack_cap() <= 0) {
    return;
  }
  const SkillEffect& base = skill.base();
  const SkillEffect& per = skill.per_level();
  totals.freeze.cap = std::max(totals.freeze.cap, skill.freeze_stack_cap());
  totals.freeze.crit_dmg_per_stack =
      std::max(totals.freeze.crit_dmg_per_stack,
               base.crit_dmg_per_freeze_stack() +
                   per.crit_dmg_per_freeze_stack() * (level - 1));
  totals.freeze.final_dmg_pct_per_stack =
      std::max(totals.freeze.final_dmg_pct_per_stack,
               base.final_dmg_pct_per_freeze_stack() +
                   per.final_dmg_pct_per_freeze_stack() * (level - 1));
}

// Notes Meso Explosion down. Recorded rather than folded: Meso Mastery's
// points land on each of its lines, the two skills fold in catalog order, and
// so the pair cannot be settled until every passive is in. See
// FoldMesoExplosion.
void AddMesoExplosion(const Skill& skill, int level, PassiveTotals& totals) {
  double per_line = skill.base().meso_hit_pct() +
                    skill.per_level().meso_hit_pct() * (level - 1);
  if (per_line <= 0.0) {
    return;
  }
  totals.meso_skill = skill.name();
  totals.meso_hit_pct += per_line;
  totals.meso_lines = SkillLinesAt(skill, level);
}

// Notes down what `skill` hands other skills by name. Kept out of AddEffect,
// which is handed levers with no skill behind them: which skill is
// strengthened is written on the boost, not on the lever.
void AddSkillBonuses(const Skill& skill, int level, PassiveTotals& totals) {
  for (const SkillBoost& boost : skill.boost()) {
    const SkillEffect& base = boost.effect();
    const SkillEffect& per = boost.effect_per_level();
    SkillBonus& into = totals.skill_bonus[boost.skill_name()];
    into.skill_pct += base.skill_pct() + per.skill_pct() * (level - 1);
    into.damage_pct += base.damage_pct() + per.damage_pct() * (level - 1);
    into.boss_pct += base.boss_pct() + per.boss_pct() * (level - 1);
    into.crit_rate += base.crit_rate() + per.crit_rate() * (level - 1);
    // The two that do not sum, for the reason they never do.
    into.ied = CombineIgnoredDefense(
        into.ied, base.ied_pct() + per.ied_pct() * (level - 1));
    double fd = base.final_dmg_pct() + per.final_dmg_pct() * (level - 1);
    into.final_dmg_pct = (1.0 + into.final_dmg_pct) * (1.0 + fd) - 1.0;
  }
}

void AddPassive(const Skill& skill, int level, EquipType weapon,
                PassiveTotals& totals) {
  if (skill.kind() == SKILL_KIND_ATTACK) {
    AddEffect(WithoutSwingLevers(skill.base()),
              WithoutSwingLevers(skill.per_level()), level, totals);
    // The half an attack states apart because it keeps it: no lever of this
    // one leaves with the swing. See Skill.passive.
    AddEffect(skill.passive(), skill.passive_per_level(), level, totals);
  } else {
    AddEffect(skill.base(), skill.per_level(), level, totals);
  }
  AddSkillBonuses(skill, level, totals);
  AddFinalAttack(skill, skill.base(), skill.per_level(), level, totals);
  AddProc(skill, level, totals);
  AddFreezeStacks(skill, level, totals);
  // A burn on a PASSIVE belongs to the character rather than to one swing: the
  // poison stays on the claw, so everything the claw hits takes it. One on an
  // attack is that swing's own, and one on a summon is its pulses' -- both are
  // read where those are built.
  if (skill.kind() == SKILL_KIND_PASSIVE &&
      skill.dot().interval_seconds() > 0.0) {
    totals.dots.push_back(CharacterDot{skill.dot(), level});
  }
  AddMesoExplosion(skill, level, totals);
  totals.combo_orbs = std::max(totals.combo_orbs, ComboOrbsAt(skill, level));
  // A weapon bonus is a second helping of the same levers for a subset of the
  // weapons the skill accepts. Read at level 1: it is flat by construction.
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    if (bonus.required_equip_type_size() > 0 &&
        ListAllowsWeapon(bonus.required_equip_type(), weapon)) {
      AddEffect(bonus.effect(), SkillEffect::default_instance(), 1, totals);
      AddFinalAttack(skill, bonus.effect(), SkillEffect::default_instance(), 1,
                     totals);
    }
  }
}

// Cashes in the per-orb bargains against the ring of orbs the character
// carries. The orbs are taken as full: a fight paid out in expected damage has
// nowhere to put a counter, and GMS builds them up over 40% of the swings.
// The final damage lands as ONE source however many skills priced it, which is
// what "total applied between combo orbs" means.
// Turns what one line of a thrown meso is worth into what one whole meso is,
// now that Meso Mastery's points are certain to be in.
void FoldMesoExplosion(PassiveTotals& totals) {
  if (totals.meso_hit_pct <= 0.0) {
    return;
  }
  std::map<std::string, SkillBonus>::const_iterator boost =
      totals.skill_bonus.find(totals.meso_skill);
  if (boost != totals.skill_bonus.end()) {
    totals.meso_hit_pct += boost->second.skill_pct;
    totals.meso_boss_pct = boost->second.boss_pct;
  }
  totals.meso_hit_pct *= totals.meso_lines;
}

void FoldComboOrbs(PassiveTotals& totals) {
  totals.attack += totals.attack_per_combo_orb * totals.combo_orbs;
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

// One skill an ally is holding over the party, and the level their book has
// it at.
struct AllyGrant {
  const Skill* skill = nullptr;
  int level = 0;
};

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
    std::set<std::string> theirs = SupersededSkillNames(ally, skills, bonus);
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
        stacking.push_back(AllyGrant{&skill, level});
      } else if (best[skill.name()].level < level) {
        best[skill.name()] = AllyGrant{&skill, level};
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
      SupersededSkillNames(character, skills, bonus);
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
    AddEffect(bonus, SkillEffect::default_instance(), 1, totals);
  }
  // A buff standing right now grants what a passive grants for as long as it
  // is up, and folds in through the same door for the same reason -- as a
  // source of its own, so its ignored defence combines with the character's
  // rather than summing with it.
  for (const Skill* skill : buffs_up) {
    AddEffect(skill->buff().base(), skill->buff().per_level(),
              EffectiveSkillLevel(character, *skill, bonus), totals);
  }
  // What the party is holding over them, at the level its caster has it. The
  // same door again, and for the same reason.
  for (const AllyGrant& grant : PartyGrants(character, skills, allies)) {
    AddEffect(grant.skill->ally_base(), grant.skill->ally_per_level(),
              grant.level, totals);
  }
  FoldMesoExplosion(totals);
  FoldComboOrbs(totals);
  return totals;
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

std::set<std::string> SupersededSkillNames(
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
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // The same three gates every passive passes: whose book it is, whether the
    // gear it demands is in hand, and whether it is learned at all.
    if (!GrantsBuff(skill) ||
        !character.HasAdvancement(skill.job_advancement()) ||
        !SkillGearMet(character, skill) || character.skill_level(skill) <= 0) {
      continue;
    }
    buffs.push_back(&skill);
  }
  return buffs;
}

DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills,
                             absl::Span<const Skill* const> buffs_up,
                             absl::Span<const CharacterInstance> allies) {
  const Character& proto = character.proto();
  const AllocatedStats& allocated = proto.allocated_stats();
  const EquipStats& equipped = character.equip_stats();
  PassiveTotals passives = LearnedPassives(character, skills, buffs_up, allies);
  FoldApStats(allocated, passives);

  DerivedStats stats;
  stats.max_hp =
      FoldPercent(allocated.hp() + equipped.max_hp() + passives.max_hp +
                      passives.hp_per_level * proto.level(),
                  passives.max_hp_pct);
  stats.max_mp =
      FoldPercent(allocated.mp() + equipped.max_mp() + passives.max_mp +
                      passives.mp_per_level * proto.level(),
                  passives.max_mp_pct);
  stats.skill_stats.set_def(passives.def);
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
  stats.def = FoldPercent(stats.base_def + equipped.def() + passives.def,
                          passives.def_factor - 1.0);
  stats.damage_taken_pct = passives.damage_taken_pct;
  stats.dodge_chance = passives.dodge_chance;
  stats.damage_reflect_pct = passives.damage_reflect_pct;
  stats.crit_rate = passives.crit_rate;
  stats.crit_dmg = passives.crit_dmg;
  stats.hp_recover_pct = passives.hp_recover_pct;
  stats.revive_cooldown_seconds = passives.revive_cooldown_seconds;
  stats.exp_pct = passives.exp_pct;
  // A fountain pours one more helping per whole step of INT, so Holy Water
  // puts back twice its stated share at 2500 and three times it at 5000. The
  // helping grows; the clock does not. Charged against the character's WHOLE
  // INT -- what a ring grants and what Maple Warrior grants back count the
  // same as what AP bought.
  int total_int = allocated.int_() + equipped.int_() + passives.int_;
  for (const RawRegen& source : passives.regen) {
    RegenPulse pulse = source.pulse;
    if (source.int_step > 0.0) {
      pulse.pct *= 1.0 + std::floor(total_int / source.int_step);
    }
    stats.regen_pulses.push_back(pulse);
  }
  stats.status_resistance = passives.status_resistance;
  stats.elemental_resistance = passives.elemental_resistance;
  stats.damage_pct = passives.damage_pct;
  stats.boss_pct = passives.boss_pct;
  stats.meso_pct = passives.meso_pct;
  // The worn share is whole percents and the granted share a fraction. They
  // meet by summing, the way boss damage does in OffenseStatsFor.
  stats.item_drop_pct =
      passives.item_drop_pct + equipped.item_drop_rate() / 100.0;
  stats.buff_duration_pct = passives.buff_duration_pct;
  stats.mirror_line_pct = passives.mirror_line_pct;
  stats.bonus_attack_lines = passives.bonus_attack_lines;
  stats.final_dmg_pct = passives.final_dmg_pct;
  stats.ied = passives.ied;
  stats.mastery = passives.mastery;
  stats.final_attacks = passives.final_attacks;
  stats.dots = passives.dots;
  stats.procs = passives.procs;
  stats.freeze = passives.freeze;
  // Pick Pocket and Meso Explosion, worth nothing apart: a meso falls out of
  // an enemy and is thrown straight back at them. It rides the swing exactly
  // as a Final Attack does, except that the roll is per line -- so it is one
  // more source in the same list rather than a mechanism of its own.
  if (passives.meso_drop_chance > 0.0 && passives.meso_hit_pct > 0.0) {
    FinalAttackSource meso;
    meso.chance = passives.meso_drop_chance;
    meso.damage_pct = passives.meso_hit_pct;
    meso.boss_pct = passives.meso_boss_pct;
    meso.per_line = true;
    stats.final_attacks.push_back(meso);
  }
  stats.attack_speed_bonus = passives.attack_speed;
  stats.attack_pct = passives.attack_pct;
  stats.skill_bonus = passives.skill_bonus;
  return stats;
}

PassiveOffense PassiveOffenseFor(const DerivedStats& derived) {
  PassiveOffense passives;
  passives.crit_rate = derived.crit_rate;
  passives.crit_dmg = derived.crit_dmg;
  passives.mastery = derived.mastery;
  passives.damage_pct = derived.damage_pct;
  passives.boss_pct = derived.boss_pct;
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
