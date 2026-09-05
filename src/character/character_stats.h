/* Folds a character's AP-allocated stats, worn equipment, and learned passive
 * skills into the totals they actually carry into play. The defensive
 * counterpart to combat/damage.h's OffenseStatsFor: that one answers what the
 * character deals, this one what the character has. character_stats.cc
 * implements it.
 */
#ifndef MS_SRC_CHARACTER_CHARACTER_STATS_H_
#define MS_SRC_CHARACTER_CHARACTER_STATS_H_

#include <map>
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
  // Kept apart rather than multiplied together, because the fight rolls the
  // chance and pays the damage: a 40% chance of an extra 160% hit is 0.40 and
  // 1.60. Their product is what it is worth on an average swing.
  double chance = 0.0;
  double damage_pct = 0.0;
  // Strikes one of those extra hits lands, each worth damage_pct. Told apart
  // rather than summed into the percent because every one rolls its own crit
  // and its own mastery.
  int lines = 1;
  // The swings this follows. SKILL_TAG_UNSPECIFIED means all of them, which is
  // what a Final Attack gated on the weapon in hand wants.
  SkillTag required_tag = SKILL_TAG_UNSPECIFIED;
  // Whether the roll above happens once per LINE the swing lands rather than
  // once per enemy it reaches. A Final Attack is the second; a meso knocked
  // loose by Pick Pocket is the first, and on a four-line swing that is four
  // times the source.
  bool per_line = false;
  // Boss damage this source alone carries, on top of the character's. Only a
  // thrown meso has any: see SkillEffect.boosted_boss_pct.
  double boss_pct = 0.0;
  // Ignored defence only these extra hits carry, meeting the character's the
  // way two sources of it always do. A thrown meso's alone -- Meso Explosion -
  // Guardbreak brands the coins, the Shadower's own swing being another skill.
  double ied = 0.0;
  // Plain % damage only these extra hits collect, summed into the share the
  // character already pays -- a boost aimed at the skill that sets the Final
  // Attack off, exactly as boss_pct above is. Points on damage_pct above are
  // the other half of that bargain, and land there rather than here. See
  // SkillBoost::effect.
  double damage_bonus_pct = 0.0;
  // The skill that sets this off, so a boost naming that skill can find it.
  // Empty for a source no skill owns -- a thrown meso's.
  std::string skill_name;
  // Whether that skill is one the character swings. A boost's points on a
  // multiplier land on the SWING where there is one, so only a passive's
  // strike collects them here -- otherwise one grant would be read twice.
  bool owner_swings = false;
  // Whether the whole swing rolls this ONCE and lands it on a single enemy.
  // Blizzard's passive alone; every other source follows the swing onto every
  // enemy it reached. Never set with per_line -- one counts enemies down to
  // one, the other counts lines up.
  bool single_enemy = false;
};

// A burn the character leaves on everything they swing at, from a passive
// rather than from the swing itself -- the poison a rogue keeps on their claw.
// The level rides with it because what a tick is worth cannot be settled until
// the mobs on the map are known.
struct CharacterDot {
  Dot dot;
  int level = 1;
};

// One chance the character carries for a swing to land harder on a single
// enemy, from a passive rather than from the swing. Rolled once per swing --
// see the Proc message.
struct SwingProc {
  double chance = 0.0;
  // Share ADDED to what that one enemy was already taking, so a harder swing
  // carries a harder proc.
  double damage_pct = 0.0;
  double hp_recover_pct = 0.0;
};

// The Freezing Crush ladder: what one Freeze Stack is worth, and how many the
// character can hold. A cap of 0 says they hold none, which is everyone but an
// I/L magician.
struct FreezeStacks {
  double crit_dmg_per_stack = 0.0;
  double final_dmg_pct_per_stack = 0.0;
  // Shatter's: enemy defence one held stack lets any swing ignore.
  double ied_pct_per_stack = 0.0;
  int cap = 0;
  // Magic attack one held stack is worth to an ice swing, which is Glacial
  // Fury's. Left at 0 for a character holding the mechanism and nothing that
  // pays for it, and folded away entirely for one holding no cap at all --
  // there are no stacks to be paid for.
  int matt_per_stack = 0;
};

// What the ENEMY's condition is worth to the character reading it. Two
// readings, and both are the whole group's business rather than one skill's:
// whether a monster is afflicted at all, and how many burns stand on the
// monsters in front of the character.
//
// A character whose book grants neither leaves both at 0, which is everyone
// but an I/L and an F/P magician.
struct EnemyCondition {
  // Storm Magic's and Burning Magic's: final damage on every line landed on a
  // monster under any status the game keeps -- frozen or burning. Binary.
  double final_dmg_pct_when_afflicted = 0.0;
  // Elemental Drain's and Fervent Drain's: final damage for each burn alight
  // on the group, counted up to `dot_count_cap` of them. A cap of 0 says the
  // character counts none.
  double final_dmg_pct_per_dot = 0.0;
  int dot_count_cap = 0;
};

// What a SCAR is worth to the character who leaves it: how often their swings
// leave one and how long it stands, then the two things being scarred is read
// for. A character whose book grants none of this leaves none.
struct Scar {
  // Chance one LINE of a swing scars the enemy it landed on.
  double chance = 0.0;
  double seconds = 0.0;
  // Chance Attack's final damage against a scarred monster, and the share
  // Scarring Sword takes off that monster's attack on top of the barrier the
  // character already carries.
  double final_dmg_pct = 0.0;
  double enemy_attack_pct = 0.0;
};

// One fountain's pour: the share of the HP pool it puts back, and how far
// apart its pulses fall. Held per skill rather than summed, because two
// pouring on different clocks cannot share one.
struct RegenPulse {
  double pct = 0.0;
  // The flat half of the same pulse, poured beside the share. See
  // SkillEffect::regen_hp.
  int hp = 0;
  double interval_seconds = 0.0;
};

struct DerivedStats {
  int max_hp = 0;
  int max_mp = 0;
  // What the character's stats alone are worth in DEF: 1.5 per STR and 0.4 per
  // DEX and LUK. So a character in rags has DEF, and AP spent on STR buys
  // some. Split out from the total because the stats page shows the pair.
  int base_def = 0;
  // base_def plus everything worn and granted, with any percentage over the
  // whole of it. Worth less than it looks: DEF stops paying once it reaches a
  // share of the attacking monster's attack, and a levelled character sits
  // past that cap already -- see DefenseReduction.
  int def = 0;
  // The share of incoming damage cancelled (0.10 == 10% less taken), combined
  // across every source by multiplying. Magic Guard counts here: the damage it
  // sends to MP is damage the character never takes, since nothing tracks MP.
  double damage_taken_pct = 0.0;
  // Chance an incoming hit misses outright (0.30 == 30%), combined across
  // sources by multiplying what gets through -- two 50% dodges leave a quarter
  // of the hits landing, not none of them.
  double dodge_chance = 0.0;
  // Share taken off the attack of whatever is hitting the character (0.30 ==
  // 30% less), summed across its sources and held at one. Weakens the monster
  // before its hit is rolled, so armour then cancels a larger share of what is
  // left. See SkillEffect::enemy_attack_pct.
  double enemy_attack_pct = 0.0;
  // Whether that barrier stands against a boss as well as an ordinary monster.
  bool enemy_attack_reaches_boss = false;
  // Share of a hit taken that goes straight back into whatever landed it
  // (1.20 == 120% of what the character actually lost). Summed, since two
  // reflections both fire.
  double damage_reflect_pct = 0.0;
  // Added chance for a swing to crit (0.40 == 40%). Feeds OffenseStatsFor,
  // since what it modifies is damage rather than the character's own bulk.
  double crit_rate = 0.0;
  // Added critical damage (0.05 == +5%), summed, and worth only the share of
  // swings that crit at all -- so it is the crit_rate skills that make it
  // worth anything.
  double crit_dmg = 0.0;
  // Vicious Shot's bargain, folded into crit_dmg above once nothing more will
  // be added to crit_rate. Never read after that fold.
  double crit_dmg_per_crit_rate = 0.0;
  // Expected share of the HP pool a landed swing puts back. Costs no swing,
  // unlike a healing cast -- the fight adds it after the hit lands.
  double hp_recover_pct = 0.0;
  // Seconds between one revival and the next, for a character whose passives
  // revive them at all -- 0 for everyone else, which is everyone but a Dark
  // Knight. A hit that would kill them fills the pool instead.
  double revive_cooldown_seconds = 0.0;
  // Extra EXP every kill yields, summed. Unlike everything else here it is
  // read outside a fight -- see AwardCombatRewards.
  double exp_pct = 0.0;
  // The fountains the character carries, one per skill granting one. Each
  // pours on its own clock. Costs no swing and needs no hit, unlike
  // hp_recover_pct above.
  std::vector<RegenPulse> regen_pulses;
  // Resistance to abnormal statuses and to elemental damage. Nothing inflicts
  // either, so these reach the stats page and go no further.
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // Plain % damage, summed over every passive granting it, and final damage,
  // combined by multiplying: two 10% sources come to 21%. Both are one number
  // by the time they leave here, because that is all the damage chain takes.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of one line's damage a shadow copy of the swing lands, per line the
  // swing already has. 0 for a character with no Shadow Partner, which is
  // every character but a Hermit.
  double mirror_line_pct = 0.0;
  // Strikes added to every multi-line swing. 0 for a character with no Bolt
  // Surplus, which is every character but a Marksman.
  int bonus_attack_lines = 0;
  // Share added to the meso a kill yields (0.20 == +20%), from everything but
  // equipment: the passives, the Hyper Stats, the Inner Ability, a potion.
  double meso_pct = 0.0;
  // The same from what is worn, held apart because it alone has a soft cap.
  // Read the pair through MesoBonus rather than either half -- the cap is the
  // whole reason there are two.
  double equip_meso_pct = 0.0;
  // What multiplies the meso a kill yields once every share above is summed.
  // Nothing caps it, and only a consumable moves it off 1.
  double meso_final_mult = 1.0;
  // Share added to how often a kill drops anything (0.20 == a fifth more
  // often), meso included. What the passives grant plus what is worn, and read
  // outside a fight like exp_pct -- see AwardCombatRewards.
  double item_drop_pct = 0.0;
  // Share added to how long every buff the character raises stays up (0.50 ==
  // half again). Summed, and read only by AddBuffs -- the wait for the next
  // cast is untouched.
  double buff_duration_pct = 0.0;
  // Share added to damage against a boss and nothing else. Summed, and summed
  // again with the equipment's own -- see OffenseStatsFor. Nothing in the game
  // is a boss yet, so nothing reads it.
  double boss_pct = 0.0;
  // The same against every monster that is not a boss. Only a Hyper Stat
  // grants it.
  double normal_pct = 0.0;
  // Share of the monster's DEF the character's attacks ignore, combined in
  // reverse across the passives granting it. Gear grants it too, and the two
  // meet the same way -- see OffenseStatsFor.
  double ied = 0.0;
  // The best weapon mastery the passives grant, 0..1 -- the better of two
  // rather than their sum, since two masteries are not twice as steady a
  // swing. The job line's own base is added under it; see BaseMastery.
  double mastery = 0.0;
  // The Final Attacks the character's passives grant, one entry apiece and in
  // catalog order. Kept apart even where two follow the same swings: they are
  // independent rolls, and merging them into one would have to settle on a
  // chance and a damage that no single source has.
  std::vector<FinalAttackSource> final_attacks;
  // The burns the character's passives leave on every swing they choose.
  // Empty for everyone holding no such skill, which is everyone but a rogue.
  std::vector<CharacterDot> dots;
  // The chances their passives give every swing to land harder on one enemy.
  // Empty for everyone but a Sniper.
  std::vector<SwingProc> procs;
  // What a Freeze Stack buys them, and how many they hold.
  FreezeStacks freeze;
  // What their swings leave behind on a monster, and what it is worth.
  Scar scar;
  // What the condition the enemy is already in is worth to them.
  EnemyCondition condition;
  // Faster-swing stages added on top of the weapon's own attack speed. Feeds
  // the swing interval, not the per-hit damage -- see ComputeCombatParams.
  int attack_speed_bonus = 0;
  // Stages that may pass the soft cap: Decent Speed Infusion's, which stand
  // always, and the Extreme Green Potion's, which are the bossing preset's
  // alone -- that being the fight it works in. See AttackSpeedStage.
  int uncapped_attack_speed_bonus = 0;
  // What the book hands one named skill apiece, keyed by that skill's display
  // name. Kept as a map rather than folded into the character's own levers
  // because what it lifts is one swing rather than all of them -- see
  // SkillBoost::effect. Empty for most characters.
  std::map<std::string, SkillBonus> skill_bonus;
  // Percentage over the character's whole attack, worn and granted alike.
  // Applied where the totals are summed rather than folded into skill_stats,
  // because what it scales includes the weapon in their hand -- see
  // TotalEquipStats.
  //
  // Two fields rather than one because a potential grants the two apart, as
  // GMS does -- a %ATT line on a staff is worth nothing. A skill granting
  // attack_pct writes both, which is what it has always meant.
  double attack_pct = 0.0;
  double magic_attack_pct = 0.0;
  // Seconds off every skill's cooldown, from the two lines a hat can carry.
  // What is left of a wait once they are paid is ReducedCooldown's business,
  // since a short wait gives up a share rather than the seconds.
  double cooldown_reduction_seconds = 0.0;
  // What the map's Arcane Force requirement leaves of the character's damage,
  // and what it does to the monster's. Not derived from the character at all
  // -- DerivedStatsFor leaves both at the identity, and ComputeCombatParams
  // writes them once the map is known. They live here so that the one struct
  // every damage builder already carries is the one that says it.
  double arcane_damage_factor = 1.0;
  double arcane_taken_factor = 1.0;
  // What the passives grant, in the shape of a worn item because that is how
  // they behave: sum it with equip_stats() and hand the total wherever
  // equipment stats go. It is the only way a skill's primary stat reaches the
  // damage chain. Its DEF is only the passives' share, unlike `def` above.
  EquipStats skill_stats;
  // The share of that the potentials paid, their %stat lines included. Held
  // apart for the reason symbol_stats is: a caller pricing a potential the
  // character is not wearing has to take the worn one back off first. See
  // PotentialStatGrant.
  EquipStats potential_stats;
};

// The most %meso worn equipment is worth, however many pieces of it grant
// some, and the ceiling over every additive source together. Nothing passes
// the second; a skill, a Hyper Stat or a potion passes the first.
inline constexpr double kEquipMesoSoftCap = 1.00;
inline constexpr double kMesoHardCap = 3.00;

// What the character's %meso comes to: the worn share held to its soft cap,
// plus everything else, the sum held to the hard cap. What multiplies on top
// of it is meso_final_mult, which neither cap touches.
double MesoBonus(const DerivedStats& derived);

// Whether the weapon in hand is one `skill` will work with. True for a skill
// that names no weapon type, which is most of them.
bool SkillAllowsWeapon(const Skill& skill, EquipType weapon);

// Whether the character is carrying everything `skill` demands: the weapon
// type it names, and something in the secondary slot if it asks for one. A
// skill whose demand is unmet stays learned -- it is the effect that lapses,
// and it comes back with the right gear on.
bool SkillGearMet(const CharacterInstance& character, const Skill& skill);

// How far past its master level a granted level can carry a skill that allows
// it. Two, which is what Combat Orders hands out at its own master level, so a
// 4th job skill maxing at 30 can reach 32 and no further.
inline constexpr int kLevelsPastMasterLevel = 2;

// Levels every skill the character has learned gains from a skill that grants
// them -- Combat Orders, and nothing else so far. 0 for a character without
// one and without an ally holding one.
//
// `allies` is the rest of the party; see DerivedStatsFor. A White Knight
// ignores an ally's Combat Orders and keeps their own, by the rule there.
int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills,
                     absl::Span<const CharacterInstance> allies = {});

// `learned` lifted by `bonus`, held to the master level -- or to
// kLevelsPastMasterLevel above it for a skill marked exceeds_master_level,
// which is how a 4th job skill reaches the two levels its page never
// describes. An unlearned skill stays unlearned, and the skill granting the
// bonus does not receive it.
//
// For a caller holding a level rather than a character: the skill page asks
// what one more point would buy, which is nobody's current level.
int LevelWithBonus(const Skill& skill, int learned, int bonus);

// What `skill` is actually worth to this character: LevelWithBonus of the
// level they learned.
//
// This is the level everything that READS a skill wants -- its stats, its
// damage, the level shown beside it. Spending SP wants skill_level() instead:
// a granted level is not one the player bought, and must not stop them buying
// the real one.
int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus);

// An attack's effect, split in two. A few levers on a skill that DEALS DAMAGE
// are true only for the swing that states them -- Gungnir's Descent ignores
// 30% of a monster's defence when it lands, and Dark Impale a moment later
// does not -- while everything else it grants follows the character for good.
// The two halves together are the whole effect, and neither overlaps the
// other.
//
// A skill on its own clock keeps them the same way: Radiant Evil's ignored
// defence is the eye's, not the Dark Knight's. On a skill that swings at
// nothing every lever is the character's, and SwingLeversOf is not asked.
SkillEffect WithoutSwingLevers(const SkillEffect& effect);
SkillEffect SwingLeversOf(const SkillEffect& effect);

// The skills in this character's book that are not paying, by display name. A
// dormant skill keeps its level and its page and stops granting -- both its
// levers and, where it is an attack, the swing it offered.
//
// Two things put one to sleep. A skill that supersedes another states the
// whole of what it replaced, so both paying would pay twice
// (Skill.supersedes_skill_name); and a Vengeance form and the Benevolence
// skill it stands in for are one row of the book, so whichever the toggle is
// not showing lies dormant (Skill.replaces_skill_name).
//
// Skill.exclusive_group is the third and finest form of the same idea, and is
// not answered here: it puts levers to sleep rather than skills, so a member
// of a group goes on paying whatever the rest of the group does not.
std::set<std::string> DormantSkillNames(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus);

// The timed buffs this character can put up: the skills they have learned that
// carry one, in catalog order. What a buff grants is not folded in here -- it
// is up only some of the time, so the fight decides when. See Skill.buff.
std::vector<const Skill*> BuffSkillsFor(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills);

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
// the CASTER's book rather than the reader's: GMS times a party buff by
// whoever cast it, so one cast stands the same length over everybody under it.
double BuffDurationPctFor(const CharacterInstance& character,
                          const std::map<std::string, Skill>& skills);

// The timed buffs the rest of the party puts up over this character: every
// ally skill whose BUFF carries a party half (Buff.ally_base), thinned by the
// two rules DerivedStatsFor states below. What it grants is not folded in here
// either -- an ally's buff is up only while their clock says so, so the fight
// runs a window of its own for each one. See CombatParams::ally_buffs.
std::vector<AllyGrant> AllyBuffsFor(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    absl::Span<const CharacterInstance> allies);

// `skills` is the loaded skill catalog; every passive in it the character has
// learned contributes its level's effect. Attack skills are ignored -- their
// lever is damage, which OffenseStatsFor handles.
//
// `buffs_up` are the timed buffs standing at this moment, out of the list
// BuffSkillsFor gives: each one's levers fold in as a source of its own, so
// they meet the character's permanent ones exactly as another skill's would.
// Empty is the character as they are between casts.
//
// `allies` is everybody else in the party. Each of their skills carrying an
// ally half (see Skill.ally_base) folds in through the same door, read at that
// ally's own level. Two rules keep it honest:
//
//   - An ally's grant reaches only a character who does not carry the skill
//     themselves. A buff does not stack with itself, and their own copy is
//     folded in already -- so two Clerics in a party each keep their own
//     Bless rather than taking the better of the two. Blessed Ensemble is
//     exempt, being a bonus for the company kept; see ally_effect_stacks.
//   - What one ally supersedes, the whole party loses. A Bishop's Advanced
//     Blessing puts out a Cleric's Bless for everyone, which is what GMS
//     means by the two not stacking.
//
// Empty is a character playing alone, which is every character outside a
// party fight.
//
// `preset` picks which of the character's two setups to read -- their Hyper
// Stat allocation and their Inner Ability alike. Farming unless the caller is
// a boss fight or a screen showing the other one.
DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills,
                             absl::Span<const Skill* const> buffs_up = {},
                             absl::Span<const CharacterInstance> allies = {},
                             StatPreset preset = StatPreset::kFarming);

// The offensive half of the derived stats, in the shape combat/damage.h asks
// for them. One place to keep in step with DerivedStats, rather than every
// caller that builds an OffenseStats knowing which fields cross over.
PassiveOffense PassiveOffenseFor(const DerivedStats& derived);

// Everything the character wears plus everything their passives grant, summed.
// This is what the rest of the game should read wherever it wants "the
// character's equipment stats": a skill that grants LUK is worth exactly as
// much as a ring that grants LUK, and nothing downstream should have to know
// which one it came from. `derived` is the result of DerivedStatsFor above.
EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived);

// The flat stat `totals` would pay `character`, its %stat lines included --
// the fold DerivedStatsFor applies, asked of a potential they are not wearing.
// A %stat line takes a share of the whole stat pile, so only this file knows
// what one is worth; pricing a cube before paying for it needs exactly that.
//
// `derived` is the character's own DerivedStatsFor result, which is where the
// pile is read from. Pass their current totals and it answers
// `derived.potential_stats`.
EquipStats PotentialStatGrant(const CharacterInstance& character,
                              const DerivedStats& derived,
                              const PotentialTotals& totals);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CHARACTER_STATS_H_
