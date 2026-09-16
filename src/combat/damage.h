/* How hard, and how often, the player hits, and how hard a mob hits back: the
 * GMS damage formulas and the attack-speed timing that feeds them. Pure math
 * over a character's stats and a mob -- no game state, no notion of a fight in
 * progress.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_H_
#define MS_SRC_COMBAT_DAMAGE_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Offensive parameters feeding the GMS damage formula. Every modifier defaults
// to its identity value.
struct OffenseStats {
  int primary = 0;    // total primary stat
  int secondary = 0;  // total secondary stat
  int attack = 0;     // total weapon/gear attack
  int level = 0;      // attacker level, for the level multiplier
  // The FLOOR of every line's roll, so it raises min damage and leaves max
  // alone. 1.0 never rolls low; it does not hit harder. See BaseMastery.
  double mastery = 0.20;
  double skill_pct = 1.0;  // skill damage multiplier (1.0 == 100%)
  int lines = 1;           // hits per attack
  // The shadow's hits, beside the real ones rather than folded in: normally
  // a copy of `lines`, each landing `mirror_pct` of what a real one does. A
  // pair, so a page can tell a swing's real hits from its copies.
  int mirror_lines = 0;
  double mirror_pct = 0.0;
  double damage_pct = 0.0;  // additive %dmg, as fraction
  double boss_pct = 0.0;    // additive boss %dmg; applies only vs bosses
  // The mirror of boss_pct: %damage against anything that is NOT a boss. The
  // character's own, so it meets damage_pct in the sum boss damage does.
  double normal_pct = 0.0;
  // Added to skill_pct against anything that is not a boss, so it is worth
  // its value once per LINE. From the attack rather than the character.
  double normal_skill_pct = 0.0;
  // Both sit atop every character's base pair in constants.h, which the
  // formula adds; these carry only what gear and skills bought.
  double crit_rate = 0.0;      // 0..1
  double crit_dmg = 0.0;       // 0..1
  double final_dmg_pct = 0.0;  // final damage, as fraction
  double ied = 0.0;            // ignore enemy defense, 0..1
  // What the character ignores of a boss's elemental resistance, above the
  // base in constants.h. Half of a boss hit is behind it, so 1.0 is a swing
  // the halving never touched.
  double ier = 0.0;
  // GMS's leading weapon constant; see WeaponConstant. 1.0 is the identity a
  // bare stat line carries, not a value any real weapon has.
  double weapon_constant = 1.0;
  // What the map's Arcane Force requirement leaves of the swing. Lands LAST,
  // after the level multiplier: it scales the whole hit.
  double arcane_pct = 1.0;
};

// What varies from one landing to the next. The expected-value chain folds
// these in as averages; a live swing rolls them per LINE and per enemy.
struct SwingRolls {
  int lines = 1;         // the swing's own hits on one enemy
  int mirror_lines = 0;  // the shadow's, each worth mirror_pct of a real line
  double mirror_pct = 0.0;
  // The floor of each line's uniform roll. 1.0 never rolls low, so a caller
  // filling in nothing gets no variance.
  double mastery = 1.0;
  // Both already carry the base pair every character has, unlike the fields
  // they come from -- what rolls is the whole chance, not the bought share.
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
};

// What `offense` will vary by. Everything else in the chain is settled before
// the swing lands.
SwingRolls RollsFor(const OffenseStats& offense);

// One landed line: its share of the swing's expected damage, and whether it
// was critical. For a caller drawing the lines one number apiece.
struct LineRoll {
  double share = 0.0;
  bool crit = false;
};

// One landing's share of the mean: the per-line rolls summed over what they
// average to, and exactly 1.0 when nothing rolls. A FACTOR because the
// expected damage is already cached, so the two agree by construction.
//
// `lines`, where given, is filled with a share per line. They sum to what this
// returns, so what the player adds up and what the monster loses agree.
double RollFactor(const SwingRolls& rolls, std::mt19937& rng,
                  std::vector<LineRoll>* lines = nullptr);

// Two shares of ignored DEF meet in reverse: what is left of the armour is
// the product of what each leaves. 30% and 40% come to 58%, and no pile ever
// reaches all of it. Every source combines this way.
double CombineIgnoredDefense(double a, double b);

// The first factor of the GMS damage chain: the game's whole notion of one
// weapon class hitting harder than another. It belongs to the job and the
// weapon together -- a one-handed sword is 1.24 for a Paladin and 1.34 for a
// Hero. Written per weapon, each carrying its owning line's constant, with an
// override where a line disagrees; a weapon no line lists is 1.0.
double WeaponConstant(Job job, EquipType weapon);

// The mastery a line swings at before learning a mastery skill: 20% melee,
// 15% bow and crossbow, 25% wand and staff. The skill's grant ADDS to this --
// 70/65/75 in 2nd job, 90/85/95 in 4th -- so no mastery skill can make a swing
// wilder than the bare one.
double BaseMastery(Job job);

// Whether a skill of this kind carries a damage multiplier of its own: the
// swings and the things firing on their own clock.
bool DealsDamage(SkillKind kind);

// Every lever at `level`: base + per_level x (level - 1), folded over the
// whole message. ASK THIS rather than spelling the fold out field by field --
// this walks whatever the message holds, so a new lever needs no edit. Bools
// have no ladder, so one set on either side stands.
SkillEffect EffectAt(const SkillEffect& base, const SkillEffect& per_level,
                     int level);

// A buff's party half once the caster's INT has grown it: each ally_int_lever
// adds its effect per whole `caster_int` step, held to the ceiling it names or
// else to the caster's OWN value divided by `party_size`.
//
// `half` is the party half at the caster's level, `own` is theirs, and
// `party_size` counts the caster. Rounded to whole percentage points, the unit
// GMS states these in. See AllyIntLever.
SkillEffect GrownByCasterInt(const Buff& buff, const SkillEffect& half,
                             const SkillEffect& own, int caster_int,
                             int party_size);

// A ladder's value as a whole number. Every SkillEffect number is a double so
// a value climbing every few levels is a fraction of a step: GMS's ceil(L/5)
// is `base 1, per_level 0.2`. Floored per source, a skill granting whole
// points, with an epsilon because 1 + 0.2 x 5 is not exactly 2.
int WholeValue(double value);

// Strikes per enemy at `level`: `lines` plus whole lines `lines_per_level`
// has bought since level 1, never below 1. EVERY reader goes through here, so
// a skill whose lines climb climbs everywhere at once.
int SkillLinesAt(const Skill& skill, int level);

// Strikes one swing lands, each of SkillLinesAt lines. No ladder: GMS states
// a slash count that holds at every level. See Skill::casts.
int SkillCasts(const Skill& skill);

// Strikes one extra hit lands, read exactly as SkillCasts is. See
// SwingHit::casts.
int SwingHitCasts(const SwingHit& hit);

// What an empowered form calls itself: the target's name behind "Empowered ".
// A boost is keyed by it, so the fight and the fold must spell it alike.
std::string EmpoweredSkillName(const std::string& target);

// The names `boost` is filed under -- its target's, its empowered form's, or
// both, as BoostReach says. Every reader goes through here, so the fight, the
// fold and the skill page agree on who a grant reaches.
std::vector<std::string> BoostTargetNames(const SkillBoost& boost);

// Combo Orbs at `level`: `combo_orbs` plus whole orbs bought since level 1.
// Here for the reason SkillLinesAt is -- the stat line and the skill page both
// ask, and a ring that grows must grow in both.
int ComboOrbsAt(const Skill& skill, int level);

// The wait after a use at `level`, in GMS scale: `cooldown_seconds` plus the
// per-level step. Here for the reason SkillLinesAt is.
double CooldownAt(const Skill& skill, int level);

// Whether `pulse` bleeds at all: on its own clock, or on the swings of the
// skill it rides. A pulse riding a swing states no clock, so neither caller
// can settle it by reading cast_interval_seconds.
bool Pulses(const BuffPulse& pulse);

// The longest `buff` can stand at level 1: its own length, or the longest form
// it can be raised in. A buff with forms states no length of its own, so ASK
// HERE rather than reading duration_seconds.
double LongestBuffDuration(const Buff& buff);

// What is left of a `wait` once a potential's seconds are paid. GMS's rule,
// not a plain subtraction: under 5 seconds gives up nothing, 5 to 10 gives up
// 5% of itself per second offered, and the part of a longer wait falling under
// 10 seconds is halved on the way down.
double ReducedCooldown(double wait, double reduction_seconds);

// Hits `shield` cancels at `level`, floored. Here for the reason SkillLinesAt
// is: the fight and the skill page both ask.
int ShieldHitsAt(const Shield& shield, int level);

// What the book hands ONE skill by name, summed over every skill granting it.
// Only the swing being priced reads its entry, so none of it follows the
// character onto their next attack. See SkillBoost::effect.
struct SkillBonus {
  // Percentage POINTS on the named skill, worth their value once per LINE.
  // Aimed at a passive carrying a Final Attack, it lands on that strike's
  // multiplier instead.
  double skill_pct = 0.0;
  // Plain % damage this swing alone collects, summed into what the gear and
  // passives already pay. What a Hyper Skill's Reinforce grants.
  double damage_pct = 0.0;
  // Boss damage, summed with the target's own and the character's -- all three
  // are shares of the same damage.
  double boss_pct = 0.0;
  // The same against everything that is not a boss.
  double normal_pct = 0.0;
  // Ignored defence, combined in reverse with the target's own rather than
  // summed: two sources of 20% leave 64% of the monster's DEF, not 60%.
  double ied = 0.0;
  double crit_rate = 0.0;
  // Final damage, which multiplies into the target's own.
  double final_dmg_pct = 0.0;
  // Chance added to the named skill's Final Attack. It strengthens the extra
  // hit the target sets off, so it folds onto that source rather than where
  // the swing is built.
  double final_attack_chance = 0.0;
  // What that chance is multiplied by once every additive source is in.
  double final_attack_chance_mult = 1.0;
  // Points on the named skill's burn tick. Never reaches the swing: the burn
  // states its own multiplier, so this is added where BurnFor writes it.
  double dot_skill_pct = 0.0;
  // Seconds added to the named skill's burn, for the same reason as above: the
  // burn's clock is its own. See SkillBoost::dot_duration_seconds.
  double dot_duration_seconds = 0.0;
  // Hits handed to the named skill's swing, each already read at the granting
  // level -- so each states its whole damage in `base` and carries no ladder.
  // See SkillBoost::extra_hit.
  std::vector<SwingHit> extra_hit;
  // The wound the named skill leaves, handed to it by the skill that STATES
  // the wound: Trickblade names Assassinate and Sonic Blow, and neither knows
  // anything about it. See Wound.
  int wound_stacks = 0;
  int wound_max_stacks = 0;
  double wound_seconds = 0.0;
};

// What a character's passives add to every swing, whichever attack they
// choose. None of it depends on the target. See character_stats.h.
struct PassiveOffense {
  double crit_rate = 0.0;  // added chance for a swing to crit (0.40 == 40%)
  double crit_dmg = 0.0;   // added critical damage (0.05 == +5%)
  // What the best mastery skill grants, 0..1, before the job line's own base
  // is added under it. 0 for a character holding no such skill.
  double mastery = 0.0;
  // Already combined across every passive granting them: summed and
  // multiplied respectively, which is where the two differ.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of one line a shadow copy of the swing lands, per line the swing
  // already has. See DerivedStats::mirror_line_pct.
  double mirror_line_pct = 0.0;
  // Strikes added to a swing that already lands more than one. See
  // SkillEffect::bonus_attack_lines.
  int bonus_attack_lines = 0;
  // Added to damage against a boss, and it SUMS with the gear's: both are
  // shares of the same damage, unlike ied.
  double boss_pct = 0.0;
  // The same against everything that is not a boss, out of Hyper Stats, Inner
  // Ability and the passives granting one. No gear carries it.
  double normal_pct = 0.0;
  // Share of the monster's DEF the passives ignore, already combined across
  // them. Meets the gear's share in reverse, the same way they combined.
  double ied = 0.0;
  // Its elemental twin, summed across them. No gear grants any, so this is
  // the whole of what a character bought. See OffenseStats::ier.
  double ier = 0.0;
  // What the book hands particular skills, keyed by display name. Only the
  // swung skill's entry is read.
  std::map<std::string, SkillBonus> skill_bonus;
  // What the map's Arcane Force requirement leaves of the swing: 1 outside
  // Arcane River, a tenth with no force for the map, half again with half
  // again over it. A multiplier, hence the default of 1.
  double arcane_pct = 1.0;
};

// OffenseStats from a character's job, level and summed stats. `attack_skill`
// is the attack being swung at `attack_level`, or null for the bare poke;
// choosing WHICH is the caller's job, depending on a crowd this per-mob math
// cannot see. `weapon` decides the weapon constant and nothing else.
OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped, EquipType weapon,
                             const Skill* attack_skill, int attack_level,
                             const PassiveOffense& passives = {});

// Expected damage of one full attack against `mob` (crit averaged over its
// rate, no RNG). The GMS damage chain; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// What ignoring one more point of `mob`'s defence is worth to a character who
// already ignores `ied` of it, as a share of the damage they were dealing.
// Its defence leaves (1 - c) of the swing; ignoring a further e of that leaves
// (1 - c(1 - e)), so the swing gains e * c/(1 - c) and this returns the factor.
//
// 0 against a monster with no defence, and against one already cancelled
// outright. 0 too while c is 1 or more, which only defence past 100% reaches:
// the swing sits on its 1-damage floor and a further e does not lift it.
double DefenseShare(const Mob& mob, double ied);

// One number for "how hard this character hits", for comparing CHARACTERS
// rather than predicting a swing: the damage chain with everything depending
// on the target, the skill or the moment stripped out. Build the stats with a
// null attack skill.
//
// Unlike GMS, crit is weighted by its rate, or critical damage would price the
// same whether it landed every swing or never. `vs_boss` picks between boss
// and normal %damage -- never both, no swing meeting both.
int CombatPower(const OffenseStats& offense, bool vs_boss);

// What a character brings to being hit: the defensive mirror of OffenseStats.
// Their own level decides how much of the DEF counts.
struct DefenseStats {
  int level = 0;
  int def = 0;
  // The share of incoming damage cancelled after the formula below has run
  // (0.10 == 10% less taken).
  double damage_taken_pct = 0.0;
  // Chance the hit misses outright. A miss and a reduction come to the same
  // thing over enough hits, which is all this ever reports.
  double dodge_chance = 0.0;
  // Share off the MONSTER's attack before the formula runs, and whether it
  // stands against a boss. Apart from the reduction above because it lands
  // elsewhere: a weakened monster is one whose attack the DEF cancels more of.
  double enemy_attack_pct = 0.0;
  bool enemy_attack_reaches_boss = false;
  // What the map's Arcane Force requirement does to the hit: 1 outside Arcane
  // River, up to 2.8 with no force for the map, 0 with half again over it --
  // which the damage floor turns into GMS's 1 damage.
  double arcane_taken = 1.0;
};

// Expected damage of one hit from `mob`: min and max averaged, never below 1
// as in GMS. DEF subtracts flatly but can never cancel more than 80%, which is
// the shape of the whole thing -- near the character's level their DEF clears
// the cap and more armour buys nothing; far above it the cap never binds.
double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob);

// The GMS level multiplier: 1.1 at the monster's level, rising to 1.2 at +5
// and beyond, and falling to 0 at 40 levels under it.
double LevelMultiplier(int player_level, int mob_level);

// GMS's own formula: base_delay_ms * (20 - stage) / 16, rounded up to whole
// kTickMs units. Stage 1..10, 10 fastest, 4 == base. `base_delay_ms` belongs
// to the SKILL -- the weapon's only say is the stage, so a spear and a sword
// swing the same skill at the same speed.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// The stage the formula above is the identity at, so a skill ignoring attack
// speed swings at its stated delay. Named because a bare 4 means "average
// weapon" where this means "no scaling at all".
inline constexpr int kUnscaledAttackSpeedStage = 4;

// The fastest stage an ordinary character reaches. A SOFT cap: a source that
// says it may pass one adds on top -- see AttackSpeedStage.
inline constexpr int kAttackSpeedSoftCap = ATTACK_SPEED_FASTEST_1;

// `base` and `bonus` held to the soft cap, then `uncapped` on top. The two
// stay apart because only the second may pass the cap, which is what makes it
// worth a stage to a character already on 8.
int AttackSpeedStage(int base, int bonus, int uncapped);

// True when the job attacks with magic attack. The chain treats the two alike;
// what differs is the field read and the weapon's say in the swing speed.
bool SwingsOnMagic(Job job);

// The stage attacks start from, before the passives add. The weapon's own,
// except that GMS casts every spell at the unscaled stage: a staff is Slow and
// a mage casting from one is not.
int BaseAttackSpeedStage(Job job, int weapon_stage);

// What the bare poke, and a skill naming no delay, swings at. 780ms is the
// commonest 1st/2nd job animation.
inline constexpr int kDefaultSwingDelayMs = 780;

// What a cast costs when its animation never plays. GMS paces a skill sequence
// at a flat 120ms and waives the cast action outright on the attacks it lets a
// player throw mid-swing. Unscaled by attack speed: there is no animation left
// to shorten.
inline constexpr int kWaivedCastMs = 120;

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
