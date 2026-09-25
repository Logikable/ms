/* Folds a character's AP-allocated stats, worn equipment, and learned passive
 * skills into the totals they actually carry into play. The defensive
 * counterpart to combat/damage.h's OffenseStatsFor: that one answers what the
 * character deals, this one what the character has. character_stats.cc
 * implements it.
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

// One Final Attack the character carries: what it is worth against each enemy
// the swing reached, and which swings set it off.
struct FinalAttackSource {
  // Apart rather than multiplied: the fight rolls the chance and pays the
  // damage. Their product is what it is worth on an average swing.
  double chance = 0.0;
  double damage_pct = 0.0;
  // Strikes one extra hit lands, each worth damage_pct. Apart rather than
  // summed in because every one rolls its own crit and mastery.
  int lines = 1;
  // The swings this follows. SKILL_TAG_UNSPECIFIED means all of them, which is
  // what a Final Attack gated on the weapon in hand wants.
  SkillTag required_tag = SKILL_TAG_UNSPECIFIED;
  // Whether the roll happens once per LINE rather than once per enemy. A
  // Final Attack is the second; a meso knocked loose is the first, which on a
  // four-line swing is four times the source.
  bool per_line = false;
  // Boss damage this source alone carries, on top of the character's: a
  // thrown meso's, aimed at the skill by Meso Explosion - Guardbreak.
  double boss_pct = 0.0;
  // Ignored defence only these hits carry, meeting the character's the way
  // two sources always do: Guardbreak brands the coins, not the Shadower.
  double ied = 0.0;
  // Plain % damage only these hits collect, summed into what the character
  // already pays. Points on damage_pct are the other half of that bargain and
  // land there. See SkillBoost::effect.
  double damage_bonus_pct = 0.0;
  // The skill that sets this off, so a boost naming that skill can find it.
  // Empty for a source no skill owns -- a thrown meso's.
  std::string skill_name;
  // The skill a damage breakdown files these hits under: skill_name, or Meso
  // Explosion for the thrown meso no skill_name claims.
  std::string credit;
  // Percentage POINTS these hits add against anything that is not a boss, per
  // strike. A thrown meso's alone: GMS states that bargain as points.
  double normal_skill_pct = 0.0;
  // Critical rate and final damage only these hits carry, from a boost aimed
  // at the skill setting them off. Each meets the character's the way it
  // always does: rate sums, final damage multiplies.
  double crit_rate = 0.0;
  double final_dmg_pct = 0.0;
  // Whether that skill is one the character swings. A boost's points land on
  // the SWING where there is one, so only a passive's strike collects them
  // here -- otherwise one grant would be read twice.
  bool owner_swings = false;
  // Enemies this lands on where the swing rolls it once, or 0 for one that
  // follows the swing onto each enemy. See final_attack_max_enemies.
  int max_enemies = 0;
  // Whether an attack on its own clock sets this off as readily as a swing.
  // See Skill.follows_own_clock.
  bool follows_own_clock = false;
};

// A burn the character leaves on everything they swing at, from a PASSIVE
// rather than the swing -- the poison on a rogue's claw. The level rides with
// it: what a tick is worth waits on the mobs of the map.
struct CharacterDot {
  Dot dot;
  int level = 1;
  // The skill carrying it, which a damage breakdown files its ticks under.
  std::string skill_name;
};

// One chance the character carries for a swing to land harder on one enemy.
// Rolled once per swing -- see the Proc message.
struct SwingProc {
  double chance = 0.0;
  // Share ADDED to what that one enemy was already taking, so a harder swing
  // carries a harder proc.
  double damage_pct = 0.0;
  double hp_recover_pct = 0.0;
};

// The Freezing Crush ladder: what one Freeze Stack is worth and how many can
// be held. A cap of 0 holds none.
struct FreezeStacks {
  double crit_dmg_per_stack = 0.0;
  double final_dmg_pct_per_stack = 0.0;
  // Shatter's: enemy defence one held stack lets any swing ignore.
  double ied_pct_per_stack = 0.0;
  int cap = 0;
  // Glacial Fury's magic attack per held stack, to an ICE swing.
  int matt_per_stack = 0;
};

// The stun one of the character's skills leaves, and which swings it lifts.
// NAMED rather than counted because the skill leaving a stun never collects
// its own -- GMS says so of Jupiter Thunder outright.
struct StunLift {
  SkillTag lifted_tag = SKILL_TAG_UNSPECIFIED;
  std::string from_skill;
};

// The mark one of the character's skills leaves, and which swings can spend
// one. No `from_skill`, unlike the stun: what keeps the angel off its own mark
// is carrying no element to spend one with.
struct MarkLift {
  SkillTag lifted_tag = SKILL_TAG_UNSPECIFIED;
};

// What the ENEMY's condition is worth to the character reading it: whether a
// monster is afflicted at all, and how many burns stand on the group. Both are
// the group's business rather than one skill's.
struct EnemyCondition {
  // Storm Magic's and Burning Magic's: final damage on every line landed on a
  // monster under any status the game keeps -- frozen or burning. Binary.
  double final_dmg_pct_when_afflicted = 0.0;
  // Final damage per burn alight on the group, up to `dot_count_cap`.
  double final_dmg_pct_per_dot = 0.0;
  int dot_count_cap = 0;
};

// What a SCAR is worth to the character who leaves it: how often their swings
// leave one, how long it stands, and the two things it is then read for.
struct Scar {
  // Chance one LINE of a swing scars the enemy it landed on.
  double chance = 0.0;
  double seconds = 0.0;
  // Final damage against a scarred monster, and the share taken off that
  // monster's attack on top of the barrier already carried.
  double final_dmg_pct = 0.0;
  double enemy_attack_pct = 0.0;
};

// One fountain's pour: the share of the pool and the gap between pulses. Per
// skill rather than summed, two clocks not sharing one.
struct RegenPulse {
  double pct = 0.0;
  // The flat half of the same pulse, poured beside the share. See
  // SkillEffect::regen_hp.
  int hp = 0;
  double interval_seconds = 0.0;
};

// The heal a character gets for nearly dying: it pours `pct` of the pool a
// second for `seconds` once they drop under `threshold` of it, and then waits
// out `cooldown_seconds`. `pct` at nothing is a character who has none.
struct EmergencyHeal {
  double pct = 0.0;
  double seconds = 0.0;
  double threshold = 0.0;
  double cooldown_seconds = 0.0;
};

struct DerivedStats {
  // What the character was doing when these were read. Carried so a later
  // fold reads the same activity.
  Activity activity = Activity::kFarming;
  // The gear preset these were read off. Its own field rather than something
  // a later fold re-derives from the activity: a caller may name a preset the
  // activity does not, and both halves have to land on the same one.
  StatPreset gear = StatPreset::kFirst;
  int max_hp = 0;
  int max_mp = 0;
  // DEF from the stats alone: 1.5 per STR, 0.4 per DEX and LUK. Split out
  // because the stats page shows the pair.
  int base_def = 0;
  // base_def plus everything worn and granted, percentages included. Worth
  // less than it looks: DEF stops paying at a share of the monster's attack,
  // and a levelled character is past that cap already.
  int def = 0;
  // The share of incoming damage cancelled, combined by multiplying. Magic
  // Guard counts: what it sends to MP is damage never taken, nothing here
  // tracking MP.
  double damage_taken_pct = 0.0;
  // Chance an incoming hit misses outright, combined by multiplying what gets
  // through: two 50% dodges leave a quarter landing, not none.
  double dodge_chance = 0.0;
  // Share off the attack of whatever is hitting, summed and held at one. It
  // weakens the MONSTER before the hit is rolled, so armour then cancels a
  // larger share of what is left.
  double enemy_attack_pct = 0.0;
  // Whether that barrier stands against a boss as well as an ordinary monster.
  bool enemy_attack_reaches_boss = false;
  // Share of a hit that goes back into whatever landed it. Summed: two
  // reflections both fire.
  double damage_reflect_pct = 0.0;
  // Added chance for a swing to crit (0.40 == 40%). Feeds OffenseStatsFor,
  // since what it modifies is damage rather than the character's own bulk.
  double crit_rate = 0.0;
  // Added critical damage, summed, and worth only the share of swings that
  // crit at all.
  double crit_dmg = 0.0;
  // Vicious Shot's bargain, folded into crit_dmg above once nothing more will
  // be added to crit_rate. Never read after that fold.
  double crit_dmg_per_crit_rate = 0.0;
  // Expected share of the HP pool a landed swing puts back. Costs no swing,
  // unlike a healing cast -- the fight adds it after the hit lands.
  double hp_recover_pct = 0.0;
  // Seconds between revivals, for a character whose passives revive them: a
  // hit that would kill fills the pool instead.
  double revive_cooldown_seconds = 0.0;
  // The heal that answers nearly dying, where a revival answers dying. Two
  // sources do not stack -- the shorter wait stands, as a pact's does.
  EmergencyHeal emergency_heal;
  // Extra EXP every kill yields, summed. Unlike everything else here it is
  // read outside a fight -- see AwardCombatRewards.
  double exp_pct = 0.0;
  // The fountains carried, one per skill granting one, each on its own clock.
  // Costs no swing and needs no hit, unlike hp_recover_pct.
  std::vector<RegenPulse> regen_pulses;
  // Resistance to abnormal statuses and to elemental damage. Nothing inflicts
  // either, so these reach the stats page and go no further.
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // % damage summed over the passives, and final damage MULTIPLIED: two 10%
  // sources come to 21%. Each leaves here as one number.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of one line's damage a shadow copy lands, per line the swing
  // already has.
  double mirror_line_pct = 0.0;
  // Strikes added to every multi-line swing. 0 for a character with no Bolt
  // Surplus, which is every character but a Marksman.
  int bonus_attack_lines = 0;
  // Share added to the meso a kill yields (0.20 == +20%), from everything but
  // equipment: the passives, the Hyper Stats, the Inner Ability, a potion.
  double meso_pct = 0.0;
  // The same from what is WORN, apart because it alone has a soft cap. Read
  // the pair through MesoBonus.
  double equip_meso_pct = 0.0;
  // What multiplies the meso a kill yields once every share above is summed.
  // Nothing caps it, and only a consumable moves it off 1.
  double meso_final_mult = 1.0;
  // Share added to how often a kill drops anything, meso included. Passives
  // plus gear, read outside the fight like exp_pct.
  double item_drop_pct = 0.0;
  // Share added to how long every buff raised stays up. Summed, and the wait
  // for the next cast is untouched.
  double buff_duration_pct = 0.0;
  // Share added to damage against a boss and nothing else. Summed, and summed
  // again with the equipment's own -- see OffenseStatsFor.
  double boss_pct = 0.0;
  // The same against every monster that is not a boss. Only a Hyper Stat
  // grants it.
  double normal_pct = 0.0;
  // Share of the monster's DEF ignored, combined in reverse. Gear grants it
  // too, and the two meet the same way.
  double ied = 0.0;
  // Share of a BOSS's elemental resistance ignored, summed. Half of every
  // boss hit sits behind it, and this plus the base in constants.h is all that
  // reaches it -- no gear grants any.
  double ier = 0.0;
  // The BEST weapon mastery the passives grant, not their sum: two masteries
  // are not twice as steady a swing. The line's base goes under it.
  double mastery = 0.0;
  // The Final Attacks the passives grant, one entry apiece. Kept apart even
  // where two follow the same swings: they roll independently, and merging
  // them would need a chance and a damage no single source has.
  std::vector<FinalAttackSource> final_attacks;
  // The burns the character's passives leave on every swing they choose.
  // Empty for everyone holding no such skill, which is everyone but a rogue.
  std::vector<CharacterDot> dots;
  // The chances their passives give every swing to land harder on one enemy.
  // Empty for everyone but a Sniper.
  std::vector<SwingProc> procs;
  // What a Freeze Stack buys them, and how many they hold.
  FreezeStacks freeze;
  // The stun one of their skills leaves, and what it lifts.
  StunLift stun_lift;
  // The mark one of their skills leaves, and what can spend one.
  MarkLift mark_lift;
  // What their swings leave behind on a monster, and what it is worth.
  Scar scar;
  // What the condition the enemy is already in is worth to them.
  EnemyCondition condition;
  // Faster-swing stages added on top of the weapon's own attack speed. Feeds
  // the swing interval, not the per-hit damage -- see ComputeCombatParams.
  int attack_speed_bonus = 0;
  // Stages that may pass the soft cap. See AttackSpeedStage.
  int uncapped_attack_speed_bonus = 0;
  // What the book hands one named skill apiece, keyed by display name. A map
  // rather than folded in because it lifts ONE swing, not all of them.
  std::map<std::string, SkillBonus> skill_bonus;
  // Percentage over the character's whole attack, applied where the totals are
  // summed rather than folded into skill_stats: what it scales includes the
  // weapon in hand. TWO fields because a potential grants them apart, as GMS
  // does -- a %ATT line on a staff is worth nothing.
  double attack_pct = 0.0;
  double magic_attack_pct = 0.0;
  // Seconds off every skill's cooldown. What is LEFT of a wait once they are
  // paid is ReducedCooldown's business, a short wait giving up a share.
  double cooldown_reduction_seconds = 0.0;
  // What the map's force requirement leaves of each side's damage. NOT
  // derived from the character: DerivedStatsFor leaves both at the identity
  // and ComputeCombatParams writes them once the map is known. Here because
  // this is the struct every damage builder already carries.
  double force_damage_factor = 1.0;
  double force_taken_factor = 1.0;
  // What the passives grant, shaped like a worn item because that is how they
  // behave: sum it with equip_stats() and pass the total on. The only way a
  // skill's primary stat reaches the damage chain.
  EquipStats skill_stats;
  // The share of that the potentials paid. Apart for the reason symbol_stats
  // is: pricing a potential not worn means taking the worn one off first.
  EquipStats potential_stats;
};

// The most %meso worn equipment is worth, and the ceiling over every additive
// source together. Nothing passes the second.
inline constexpr double kEquipMesoSoftCap = 1.00;
inline constexpr double kMesoHardCap = 3.00;

// The character's %meso: the worn share held to its soft cap plus everything
// else, the sum held to the hard cap. meso_final_mult multiplies on top,
// untouched by either.
double MesoBonus(const DerivedStats& derived);

// Whether the weapon in hand is one `skill` will work with. True for a skill
// that names no weapon type, which is most of them.
bool SkillAllowsWeapon(const Skill& skill, EquipType weapon);

// Whether the character carries what `skill` demands: the weapon type it
// names, and a secondary if it asks. A skill whose demand is unmet stays
// LEARNED -- the effect lapses and comes back with the right gear on.
bool SkillGearMet(const CharacterInstance& character, const Skill& skill,
                  Activity activity = Activity::kFarming);

// How far past its master level a granted level can carry a skill that allows
// it: what Combat Orders hands out at its own master level.
inline constexpr int kLevelsPastMasterLevel = 2;

// Levels every learned skill gains from a skill that grants them. `allies` is
// the rest of the party -- a character holding their own ignores an ally's, by
// the rule in DerivedStatsFor.
int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills,
                     absl::Span<const CharacterInstance> allies = {});

// `learned` lifted by `bonus`, held to the master level, or to
// kLevelsPastMasterLevel above it for a skill marked exceeds_master_level. An
// unlearned skill stays unlearned, and neither the granting skill, a hyper
// skill nor a V node receives it -- GMS names all three as exceptions.
//
// For a caller holding a LEVEL rather than a character: the skill page asks
// what one more point would buy, which is nobody's current level.
int LevelWithBonus(const Skill& skill, int learned, int bonus);

// What `skill` is worth to this character: LevelWithBonus of what they
// learned. The level everything that READS a skill wants. Spending SP wants
// skill_level() instead -- a granted level is not one the player bought.
int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus, Activity activity = Activity::kFarming);

// An attack's effect, split in two: a few levers on a skill that DEALS DAMAGE
// are true only for the swing stating them -- Gungnir's Descent ignores 30%
// when it lands and Dark Impale a moment later does not -- while the rest
// follows the character. The halves are the whole effect and do not overlap.
//
// A skill on its own clock is split the same way. On one that swings at
// nothing every lever is the character's, and SwingLeversOf is not asked.
SkillEffect WithoutSwingLevers(const SkillEffect& effect);
SkillEffect SwingLeversOf(const SkillEffect& effect);

// The skills in this book that are not paying. A dormant skill keeps its level
// and its page and grants nothing -- its levers, and the swing it offered.
//
// Two things put one to sleep: a skill that SUPERSEDES another states the
// whole of what it replaced, and a Vengeance form and its Benevolence skill
// are one row, so whichever the toggle is not showing sleeps.
//
// Skill.exclusive_group is the same idea one step finer and is NOT answered
// here: it sleeps levers rather than skills.
std::set<std::string> DormantSkillNames(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus,
    Activity activity = Activity::kFarming);

// The timed buffs this character can put up, in catalog order. What one GRANTS
// is not folded in: it is up only some of the time, so the fight decides.
std::vector<const Skill*> BuffSkillsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills,
    Activity activity = Activity::kFarming);

// One skill an ally is holding over the party, and the level their book has it
// at. What it grants is read at that level, not at the reader's.
struct AllyGrant {
  const Skill* skill = nullptr;
  int level = 0;
  // Whoever is holding it. Their book sets the level above, and their Buff
  // Duration sets how long a buff of theirs stands -- see BuffDurationPctFor.
  const CharacterInstance* caster = nullptr;
};

// The share Buff Duration adds to every buff this character raises. Read off
// the CASTER's book: one cast stands the same length over everybody.
double BuffDurationPctFor(const CharacterInstance& character,
                          const std::map<std::string, Skill>& skills);

// The timed buffs the party puts up over this character: every ally skill
// whose BUFF carries a party half, thinned by the two rules DerivedStatsFor
// states. The fight runs a window for each, as it does for their own.
std::vector<AllyGrant> AllyBuffsFor(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    absl::Span<const CharacterInstance> allies);

// One buff standing over the character, whoever raised it. Their own and an
// ally's are one list: both are levers held for a while, and both want a
// damage table of their own.
struct BuffUp {
  const Skill* skill = nullptr;
  // The party member holding it up, and their level. Null and 0 for the
  // character's own, read off Buff.base; an ally's is read off ally_base.
  const CharacterInstance* caster = nullptr;
  int caster_level = 0;
  // The caster's whole INT and the party's size -- what Buff.ally_int_lever
  // grows the party half on. Worked out where the party is in hand rather than
  // inside the fold. See TotalIntFor.
  int caster_int = 0;
  int party_size = 0;
};

// A character's whole INT: AP, gear and book together. What an INT-scaled
// lever is charged against.
int TotalIntFor(const CharacterInstance& character,
                const std::map<std::string, Skill>& skills);

// `skills` is the loaded catalog; every learned passive in it contributes its
// level's effect. Attack skills are ignored -- their lever is damage.
//
// `buffs_up` are the buffs standing now, the character's own and the party's.
// Each folds in as a source of its own. Empty is the character between casts.
//
// `allies` is the rest of the party, each of their skills carrying an ally
// half folding in at that ally's level. Two rules keep it honest:
//
//   - An ally's grant reaches only a character who does NOT carry the skill
//     themselves: their own copy is folded in already, so two Clerics each
//     keep their own Bless. See ally_effect_stacks for the exception.
//   - What one ally supersedes, the whole party loses -- a Bishop's Advanced
//     Blessing puts out a Cleric's Bless for everyone.
//
// `preset` picks which of the character's setups to read: their Hyper Stats,
// their Inner Ability and their gear alike. `gear` overrides the preset the
// worn items come from, for the boss drop roll -- which reads the Drop
// preset's gear off a character who fought in their boss gear.
DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills,
                             absl::Span<const BuffUp> buffs_up = {},
                             absl::Span<const CharacterInstance> allies = {},
                             Activity preset = Activity::kFarming,
                             std::optional<StatPreset> gear = std::nullopt);

// The offensive half of the derived stats, in the shape combat/damage.h asks
// for. One place to keep in step, rather than every caller.
PassiveOffense PassiveOffenseFor(const DerivedStats& derived);

// Everything worn plus everything the passives grant. READ THIS wherever the
// game wants "the character's equipment stats": a skill granting LUK is worth
// what a ring granting LUK is, and nothing downstream should know which. The
// gear is `derived.gear`, the preset the stats were read off.
EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived);

// The flat stat `totals` would pay, %stat lines included: the fold
// DerivedStatsFor applies, asked of a potential not being worn. A %stat line
// takes a share of the whole pile, so only this file knows what one is worth
// -- which is what pricing a cube before buying it needs.
EquipStats PotentialStatGrant(const CharacterInstance& character,
                              const DerivedStats& derived,
                              const PotentialTotals& totals);

// The character's offensive line with no attack skill behind it: what combat
// power is read off, and where a caller finds the ied to price a stat with.
// `gear` overrides the preset the activity would name, as DerivedStatsFor's
// does.
OffenseStats CharacterOffense(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills,
                              Activity preset = Activity::kFarming,
                              std::optional<StatPreset> gear = std::nullopt);

// The whole stat line as one number, with no attack skill and no target:
// combat power stands for the CHARACTER, not a swing. The activity names the
// monster -- Boss counts boss %dmg, Farm counts normal.
int CharacterCombatPower(const CharacterInstance& character,
                         const std::map<std::string, Skill>& skills,
                         Activity preset = Activity::kFarming,
                         std::optional<StatPreset> gear = std::nullopt);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CHARACTER_STATS_H_
