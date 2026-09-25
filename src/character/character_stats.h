/* Adds a character's AP stats, worn equipment and learned passive skills into
 * the totals they carry into play. combat/damage.h's OffenseStatsFor says what
 * a character deals; this file says what a character has.
 */
#ifndef MS_SRC_CHARACTER_CHARACTER_STATS_H_
#define MS_SRC_CHARACTER_CHARACTER_STATS_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/combat/damage.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One Final Attack the character carries: what it is worth against each enemy a
// swing reaches, and which swings trigger it.
struct FinalAttackSource {
  // Chance and damage stay separate because a fight rolls one and pays the
  // other. Their product is what it is worth on an average swing.
  double chance = 0.0;
  double damage_pct = 0.0;
  // Extra hits it lands, each worth damage_pct. They are not summed into one
  // because each rolls its own crit and mastery.
  int lines = 1;
  // Which swings this follows. SKILL_TAG_UNSPECIFIED means all of them, which
  // suits a Final Attack gated on the weapon in hand.
  SkillTag required_tag = SKILL_TAG_UNSPECIFIED;
  // True if it rolls once per line instead of once per enemy. A Final Attack
  // rolls per enemy. A dropped meso rolls per line, so a four-line swing drops
  // four.
  bool per_line = false;
  // Boss damage only this source carries, on top of the character's. Meso
  // Explosion - Guardbreak gives it to thrown mesos.
  double boss_pct = 0.0;
  // Ignored defence only these hits carry. It combines with the character's the
  // usual way.
  double ied = 0.0;
  // Plain % damage only these hits get, added to the character's. Points on
  // damage_pct go to damage_pct instead. See SkillBoost::effect.
  double damage_bonus_pct = 0.0;
  // The skill that triggers this, so a boost naming that skill can find it.
  // Empty for a thrown meso, which no skill owns.
  std::string skill_name;
  // The skill a damage breakdown lists these hits under: skill_name, or Meso
  // Explosion for a thrown meso.
  std::string credit;
  // Percentage points these hits add, per hit, against anything that is not a
  // boss. Only thrown mesos have it, because GMS states that bonus in points.
  double normal_skill_pct = 0.0;
  // Critical rate and final damage only these hits carry, from a boost aimed at
  // the skill that triggers them. Crit rate adds to the character's; final
  // damage multiplies.
  double crit_rate = 0.0;
  double final_dmg_pct = 0.0;
  // Whether the character also swings that skill. A boost's points go on the
  // swing when there is one, so only a passive's hits collect them here.
  // Otherwise the same grant would count twice.
  bool owner_swings = false;
  // How many enemies this hits when the swing rolls it once, or 0 if it follows
  // the swing onto each enemy. See final_attack_max_enemies.
  int max_enemies = 0;
  // Whether an attack on its own clock triggers this as often as a swing does.
  // See Skill.follows_own_clock.
  bool follows_own_clock = false;
};

// A burn the character leaves on everything they hit, from a passive skill
// rather than the swing, like the poison on a rogue's claw. The level is kept
// because what a tick is worth depends on the map's mobs.
struct CharacterDot {
  Dot dot;
  int level = 1;
  // The skill that applies it. A damage breakdown lists its ticks under this
  // name.
  std::string skill_name;
};

// A chance for a swing to hit one enemy harder. It is rolled once per swing;
// see the Proc message.
struct SwingProc {
  double chance = 0.0;
  // Share added to the damage that enemy already takes, so a harder swing gives
  // a harder proc.
  double damage_pct = 0.0;
  double hp_recover_pct = 0.0;
};

// The Freezing Crush ladder: what one Freeze Stack is worth and how many can be
// held. A cap of 0 means none.
struct FreezeStacks {
  double crit_dmg_per_stack = 0.0;
  double final_dmg_pct_per_stack = 0.0;
  // From Shatter: enemy defence ignored per held stack, by any swing.
  double ied_pct_per_stack = 0.0;
  int cap = 0;
  // From Glacial Fury: magic attack per held stack, for ice swings.
  int matt_per_stack = 0;
};

// The stun one of the character's skills applies, and which swings get a bonus
// from it. The skill is named because a skill never benefits from its own stun.
// GMS says so of Jupiter Thunder.
struct StunLift {
  SkillTag lifted_tag = SKILL_TAG_UNSPECIFIED;
  std::string from_skill;
};

// The mark one of the character's skills leaves, and which swings can spend it.
// Unlike StunLift there is no `from_skill`. The angel that leaves the mark has
// no element, so it cannot spend one.
struct MarkLift {
  SkillTag lifted_tag = SKILL_TAG_UNSPECIFIED;
};

// What the enemy's condition is worth to the character: whether a monster has
// any status, and how many burns are on the group.
struct EnemyCondition {
  // From Storm Magic and Burning Magic: final damage on every line against a
  // monster that is frozen or burning. It is on or off.
  double final_dmg_pct_when_afflicted = 0.0;
  // Final damage per burn on the group, up to `dot_count_cap`.
  double final_dmg_pct_per_dot = 0.0;
  int dot_count_cap = 0;
};

// What a scar is worth to the character who leaves it: how often their swings
// leave one, how long it lasts, and its two effects.
struct Scar {
  // Chance that one line of a swing scars the enemy it hits.
  double chance = 0.0;
  double seconds = 0.0;
  // Final damage against a scarred monster. The monster's attack also drops by
  // this share, on top of the character's existing reduction.
  double final_dmg_pct = 0.0;
  double enemy_attack_pct = 0.0;
};

// One regeneration pulse: the share of the pool it restores and the time
// between pulses. Each skill keeps its own because each runs on its own clock.
struct RegenPulse {
  double pct = 0.0;
  // Flat HP restored by the same pulse, alongside the share. See
  // SkillEffect::regen_hp.
  int hp = 0;
  double interval_seconds = 0.0;
};

// A heal for nearly dying. Once HP drops below `threshold` of the pool, it
// restores `pct` of the pool per second for `seconds`, then waits
// `cooldown_seconds`. `pct` of zero means the character has none.
struct EmergencyHeal {
  double pct = 0.0;
  double seconds = 0.0;
  double threshold = 0.0;
  double cooldown_seconds = 0.0;
};

struct DerivedStats {
  // What the character was doing when these were read, so a later step reads
  // the same activity.
  Activity activity = Activity::kFarming;
  // The gear preset these were read from. It is stored because a caller may
  // pick a preset the activity doesn't, and every later step must use the same
  // one.
  StatPreset gear = StatPreset::kFirst;
  int max_hp = 0;
  int max_mp = 0;
  // DEF from stats alone: 1.5 per STR, 0.4 per DEX and LUK. Kept separate
  // because the stats page shows both numbers.
  int base_def = 0;
  // base_def plus everything worn and granted, percentages included. DEF stops
  // helping once it passes a share of the monster's attack, and a levelled
  // character is already past that cap.
  int def = 0;
  // Share of incoming damage cancelled; sources multiply. Magic Guard counts,
  // since the damage it moves to MP is never taken as HP.
  double damage_taken_pct = 0.0;
  // Chance an incoming hit misses. Sources multiply what gets through, so two
  // 50% dodges let a quarter of hits land.
  double dodge_chance = 0.0;
  // Share taken off the attacker's attack, summed and capped at 1. It weakens
  // the monster before the hit is rolled, so armour then cancels a larger share
  // of what is left.
  double enemy_attack_pct = 0.0;
  // Whether that reduction works on bosses as well as normal monsters.
  bool enemy_attack_reaches_boss = false;
  // Share of a hit reflected back at the attacker. Sources add up.
  double damage_reflect_pct = 0.0;
  // Added crit chance (0.40 is 40%). OffenseStatsFor reads it.
  double crit_rate = 0.0;
  // Added critical damage, summed. It only counts on swings that crit.
  double crit_dmg = 0.0;
  // Vicious Shot's crit damage per crit rate. It is added into crit_dmg once
  // crit_rate is final, and not read after that.
  double crit_dmg_per_crit_rate = 0.0;
  // Expected share of the HP pool a landed swing restores. Unlike a healing
  // skill it costs no swing; the fight adds it after the hit lands.
  double hp_recover_pct = 0.0;
  // Seconds between revivals, for a character with a passive that revives them.
  // A hit that would kill restores full HP instead.
  double revive_cooldown_seconds = 0.0;
  // The heal for nearly dying. A revival is for dying. When two sources give
  // one, they don't stack; the shorter cooldown is used.
  EmergencyHeal emergency_heal;
  // Extra EXP from every kill, summed. Unlike most fields here it is read
  // outside a fight; see AwardCombatRewards.
  double exp_pct = 0.0;
  // Regeneration pulses, one per skill that grants one, each on its own clock.
  // Unlike hp_recover_pct they need no swing or hit.
  std::vector<RegenPulse> regen_pulses;
  // Resistance to status effects and to elemental damage. Nothing in the game
  // applies either yet, so these only show on the stats page.
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // % damage sums over the passives; final damage multiplies, so two 10%
  // sources make 21%.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of one line's damage a shadow copy deals, per line of the swing.
  double mirror_line_pct = 0.0;
  // Extra hits added to every multi-line swing. Bolt Surplus grants them.
  int bonus_attack_lines = 0;
  // Share added to the meso a kill drops (0.20 is +20%), from everything except
  // equipment: passives, Hyper Stats, Inner Ability and potions.
  double meso_pct = 0.0;
  // The same share from worn equipment. It is kept separate because only this
  // part has a soft cap. Read both through MesoBonus.
  double equip_meso_pct = 0.0;
  // Multiplier on kill meso, applied after the shares above are summed. It has
  // no cap, and only a consumable changes it from 1.
  double meso_final_mult = 1.0;
  // Share added to how often a kill drops anything, meso included. It comes
  // from passives and gear and, like exp_pct, is read outside the fight.
  double item_drop_pct = 0.0;
  // Share added to the duration of every buff. Sources add up. Cooldowns are
  // not affected.
  double buff_duration_pct = 0.0;
  // Share added to damage against bosses only. It is summed, then added to the
  // equipment's own; see OffenseStatsFor.
  double boss_pct = 0.0;
  // The same against every monster that is not a boss. It comes from Hyper
  // Stats, Inner Ability and skills.
  double normal_pct = 0.0;
  // Share of the monster's DEF ignored. Sources combine multiplicatively, and
  // gear's combines the same way.
  double ied = 0.0;
  // Share of a boss's elemental resistance ignored, summed. Half of every boss
  // hit is subject to it, and only this and the base in constants.h reduce it.
  // No gear grants any.
  double ier = 0.0;
  // The best weapon mastery from the passives, not their sum. The job line's
  // base mastery is the floor under it.
  double mastery = 0.0;
  // The Final Attacks the passives grant, one entry each. Two that follow the
  // same swings still stay separate because they roll independently.
  std::vector<FinalAttackSource> final_attacks;
  // The burns the character's passives put on the swings they apply to.
  std::vector<CharacterDot> dots;
  // Chances from passives for a swing to hit one enemy harder.
  std::vector<SwingProc> procs;
  // What a Freeze Stack is worth to them, and how many they can hold.
  FreezeStacks freeze;
  // The stun one of their skills applies, and which swings it helps.
  StunLift stun_lift;
  // The mark one of their skills leaves, and which swings can spend it.
  MarkLift mark_lift;
  // The scar their swings leave on a monster, and what it is worth.
  Scar scar;
  // What the enemy's current condition is worth to them.
  EnemyCondition condition;
  // Attack speed stages added to the weapon's own. It changes the time between
  // swings, not the damage per hit; see ComputeCombatParams.
  int attack_speed_bonus = 0;
  // Stages that may go past the soft cap. See AttackSpeedStage.
  int uncapped_attack_speed_bonus = 0;
  // Bonuses the skill book gives to single named skills, keyed by display name.
  // It is a map because each bonus lifts one skill, not every swing.
  std::map<std::string, SkillBonus> skill_bonus;
  // Percent bonus to the character's whole attack. It is applied when totals
  // are summed rather than folded into skill_stats, because it also scales the
  // weapon. There are two fields because potentials grant them separately, as
  // in GMS: a %ATT line on a staff is worth nothing.
  double attack_pct = 0.0;
  double magic_attack_pct = 0.0;
  // Seconds off every skill's cooldown. ReducedCooldown decides what is left of
  // a wait after this is paid, since a short cooldown loses a share instead.
  double cooldown_reduction_seconds = 0.0;
  // How much of each side's damage remains after the map's force requirement.
  // DerivedStatsFor leaves both at 1, and ComputeCombatParams sets them once
  // the map is known. They live here because every damage builder already
  // carries this struct.
  double force_damage_factor = 1.0;
  double force_taken_factor = 1.0;
  // What the passives grant, in the shape of a worn item because they behave
  // like one: add it to equip_stats() and pass the total on. This is how a
  // skill's primary stat reaches the damage formula.
  EquipStats skill_stats;
  // The part of that paid for by potentials. Like symbol_stats, it is kept
  // separate so a caller pricing a different potential can remove the worn one
  // first.
  EquipStats potential_stats;
};

// The most %meso worn equipment can give. It is also the cap on all additive
// sources together.
inline constexpr double kEquipMesoSoftCap = 1.00;
inline constexpr double kMesoHardCap = 3.00;

// The character's %meso: the worn share capped at its soft cap, plus everything
// else, with the total capped at the hard cap. meso_final_mult then multiplies
// the result.
double MesoBonus(const DerivedStats& derived);

// Whether `skill` works with the weapon in hand. True for a skill that names no
// weapon type, which is most of them.
bool SkillAllowsWeapon(const Skill& skill, EquipType weapon);

// Whether the character has the gear `skill` needs: the weapon type it names,
// and a secondary if it asks for one. An unmet skill stays learned; its effect
// stops until the right gear is back on.
bool SkillGearMet(const CharacterInstance& character, const Skill& skill,
                  Activity activity = Activity::kFarming);

// How many levels past master level a granted level can take a skill that
// allows it. This is what Combat Orders gives at its own master level.
inline constexpr int kLevelsPastMasterLevel = 2;

// Levels every learned skill gains from skills that grant levels. `allies` is
// the rest of the party. A character who has their own granting skill ignores
// an ally's, as DerivedStatsFor describes.
int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills,
                     absl::Span<const CharacterInstance> allies = {});

// `learned` plus `bonus`, capped at the master level, or at
// kLevelsPastMasterLevel above it for a skill marked exceeds_master_level. An
// unlearned skill stays unlearned. The granting skill, hyper skills and V nodes
// get no bonus; GMS names all three as exceptions.
//
// This takes a level rather than a character because the skill page asks what
// one more point would give, which is not anyone's current level.
int LevelWithBonus(const Skill& skill, int learned, int bonus);

// The level of `skill` that counts for this character: LevelWithBonus of what
// they learned. Anything that reads a skill's effect wants this. Spending SP
// wants skill_level() instead, because a granted level is not one the player
// bought.
int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus, Activity activity = Activity::kFarming);

// Removes the levers that apply only to the swing that states them. For
// example, Gungnir's Descent ignores 30% defence on its own hit, but Dark
// Impale a moment later does not. The rest of the effect applies to the
// character. The two halves together are the whole effect, with no overlap.
//
// A skill on its own clock is split the same way. For one that hits nothing,
// every lever belongs to the character and SwingLeversOf is not called.
SkillEffect WithoutSwingLevers(const SkillEffect& effect);
SkillEffect SwingLeversOf(const SkillEffect& effect);

// The skills in this book that are currently inactive. A dormant skill keeps
// its level and its place on the page but grants nothing, neither its levers
// nor its swing.
//
// Two things make a skill dormant. A skill that supersedes another includes all
// of what it replaced. And a Vengeance form and its Benevolence skill share one
// row, so whichever the toggle is not showing is dormant.
//
// Skill.exclusive_group is a finer version of the same idea and is not handled
// here: it turns off levers rather than whole skills.
std::set<std::string> DormantSkillNames(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus,
    Activity activity = Activity::kFarming);

// The timed buffs this character can cast, in catalog order. What a buff grants
// is not included: it is only up part of the time, so the fight decides.
std::vector<const Skill*> BuffSkillsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills,
    Activity activity = Activity::kFarming);

// A skill an ally holds over the party, and its level in the ally's book. Its
// effect is read at that level, not the reader's.
struct AllyGrant {
  const Skill* skill = nullptr;
  int level = 0;
  // The ally holding it. Their book sets the level, and their Buff Duration
  // sets how long their buffs last; see BuffDurationPctFor.
  const CharacterInstance* caster = nullptr;
};

// The share Buff Duration adds to every buff this character casts. It is read
// from the caster's book, so one cast lasts the same time on everyone.
double BuffDurationPctFor(const CharacterInstance& character,
                          const std::map<std::string, Skill>& skills);

// The timed buffs the party casts on this character: every ally skill whose
// buff has a party part, filtered by the two rules in DerivedStatsFor. The
// fight tracks each one's uptime, as it does for their own buffs.
std::vector<AllyGrant> AllyBuffsFor(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    absl::Span<const CharacterInstance> allies);

// One buff active on the character, whoever cast it. Their own buffs and
// allies' share one list: both are timed levers, and each needs its own damage
// table.
struct BuffUp {
  const Skill* skill = nullptr;
  // The party member who cast it, and their level. Null and 0 for the
  // character's own buff, which reads Buff.base; an ally's reads ally_base.
  const CharacterInstance* caster = nullptr;
  int caster_level = 0;
  // The caster's total INT and the party size, which Buff.ally_int_lever scales
  // the party part by. They are worked out where the party is known, not inside
  // the fold. See TotalIntFor.
  int caster_int = 0;
  int party_size = 0;
};

// A character's total INT from AP, gear and skills. INT-scaled levers use this.
int TotalIntFor(const CharacterInstance& character,
                const std::map<std::string, Skill>& skills);

// `skills` is the loaded catalog. Every learned passive in it adds its level's
// effect. Attack skills are skipped because their lever is damage.
//
// `buffs_up` are the buffs active now, the character's own and the party's.
// Each is added as its own source. Empty means the character is between casts.
//
// `allies` is the rest of the party. Each ally skill with a party part is added
// at that ally's level, with two rules:
//
//   - An ally's grant only reaches a character who does not have the skill
//     themselves, since their own copy is already counted. So two Clerics each
//     keep their own Bless. See ally_effect_stacks for the exception.
//   - A skill one ally supersedes is lost for the whole party. A Bishop's
//     Advanced Blessing turns off a Cleric's Bless for everyone.
//
// `preset` picks which of the character's setups to read: Hyper Stats, Inner
// Ability and gear alike. `gear` overrides which preset the worn items come
// from. The boss drop roll uses it to read the Drop preset's gear from a
// character who fought in their boss gear.
DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills,
                             absl::Span<const BuffUp> buffs_up = {},
                             absl::Span<const CharacterInstance> allies = {},
                             Activity preset = Activity::kFarming,
                             std::optional<StatPreset> gear = std::nullopt);

// The offensive part of the derived stats, in the shape combat/damage.h wants,
// so only this function has to keep them in step.
PassiveOffense PassiveOffenseFor(const DerivedStats& derived);

// Everything worn plus everything the passives grant. Use this wherever the
// game wants the character's equipment stats: a skill granting LUK is worth the
// same as a ring granting LUK, and nothing downstream should care which. The
// gear is `derived.gear`, the preset the stats were read from.
EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived);

// The flat stats `totals` would give, %stat lines included, using the same math
// as DerivedStatsFor for a potential that is not worn. A %stat line takes a
// share of the whole total, so only this file knows what one is worth. Pricing
// a cube before buying it needs that.
EquipStats PotentialStatGrant(const CharacterInstance& character,
                              const DerivedStats& derived,
                              const PotentialTotals& totals);

// The character's offensive stats with no attack skill: what combat power is
// read from, and where a caller gets the ied to price a stat. `gear` overrides
// the activity's preset, as in DerivedStatsFor.
OffenseStats CharacterOffense(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills,
                              Activity preset = Activity::kFarming,
                              std::optional<StatPreset> gear = std::nullopt);

// The whole stat line as one number, with no attack skill and no target. Combat
// power describes the character, not a swing. The activity picks the monster
// type: Boss counts boss %dmg, Farm counts normal.
int CharacterCombatPower(const CharacterInstance& character,
                         const std::map<std::string, Skill>& skills,
                         Activity preset = Activity::kFarming,
                         std::optional<StatPreset> gear = std::nullopt);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CHARACTER_STATS_H_
