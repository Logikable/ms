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

// Offensive parameters feeding the GMS damage formula. Modifier fields default
// to their identity (no-effect) value; real values graduate in one at a time as
// gear, skills, etc. produce them.
struct OffenseStats {
  int primary = 0;    // total primary stat
  int secondary = 0;  // total secondary stat
  int attack = 0;     // total weapon/gear attack
  int level = 0;      // attacker level, for the level multiplier
  // 0..1; the floor of every line's roll, so it raises min damage without
  // touching max. 1.0 is a swing that never rolls low, not a swing that hits
  // hardest. OffenseStatsFor sets it; see BaseMastery.
  double mastery = 0.20;
  double skill_pct = 1.0;  // skill damage multiplier (1.0 == 100%)
  int lines = 1;           // hits per attack
  // The shadow's hits, kept beside the real ones rather than folded into them.
  // `mirror_lines` is normally a copy of `lines`; each lands `mirror_pct` of
  // what a real line does. Two fields because the pair is what lets a future
  // page break a swing down into its real hits and its copied ones -- the
  // damage would come out the same from one multiplier.
  int mirror_lines = 0;
  double mirror_pct = 0.0;
  double damage_pct = 0.0;  // additive %dmg, as fraction
  double boss_pct = 0.0;    // additive boss %dmg; applies only vs bosses
  // The mirror of boss_pct: additive %dmg against anything that is NOT a
  // boss. The character's own, unlike normal_skill_pct below, so it meets
  // damage_pct in the same sum boss damage does.
  double normal_pct = 0.0;
  // Added to skill_pct against anything that is not a boss, so it is worth its
  // value once per line. Comes from the attack being swung rather than from
  // the character, unlike boss_pct -- see SkillEffect::normal_skill_pct.
  double normal_skill_pct = 0.0;
  // Both sit atop every character's base pair in constants.h, which the
  // formula adds; these carry only what gear and skills bought.
  double crit_rate = 0.0;      // 0..1
  double crit_dmg = 0.0;       // 0..1
  double final_dmg_pct = 0.0;  // final damage, as fraction
  double ied = 0.0;            // ignore enemy defense, 0..1
  double ier = 0.0;            // ignore elemental resistance, 0..1
  // GMS's leading weapon constant; see WeaponConstant. 1.0 is the identity a
  // bare stat line carries, not a value any real weapon has.
  double weapon_constant = 1.0;
  // What the map's Arcane Force requirement leaves of the swing. See
  // PassiveOffense::arcane_pct; it lands last of all, after the level
  // multiplier, because it scales the whole hit rather than any part of it.
  double arcane_pct = 1.0;
};

// The part of a swing that varies from one landing to the next, pulled out of
// an OffenseStats. The expected-value chain folds these in as averages; a live
// swing rolls them per LINE and per enemy, which is what GMS does.
struct SwingRolls {
  int lines = 1;         // the swing's own hits on one enemy
  int mirror_lines = 0;  // the shadow's, each worth mirror_pct of a real line
  double mirror_pct = 0.0;
  // The floor of each line's uniform roll, 0..1. 1.0 is a line that never
  // rolls low, which is what the default is for: a caller that fills in
  // nothing gets no variance at all.
  double mastery = 1.0;
  // Both already carry the base pair every character has, unlike the fields
  // they come from -- what rolls is the whole chance, not the bought share.
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
};

// What `offense` will vary by. Everything else in the chain is settled before
// the swing lands.
SwingRolls RollsFor(const OffenseStats& offense);

// One line of a swing as it landed: the share of the swing's expected damage
// it carried, and whether it was critical. For a caller drawing the lines one
// number apiece rather than only applying their total.
struct LineRoll {
  double share = 0.0;
  bool crit = false;
};

// One landing's share of the mean: the per-line rolls summed, over what they
// average to. Exactly 1.0 when nothing rolls, so damage times this is damage.
//
// A factor rather than a damage because the expected damage is already worked
// out and cached -- multiplying keeps the two agreeing in expectation by
// construction, and every reader that wants the average keeps reading it.
//
// `lines`, where one is given, is emptied and filled with a share per line.
// Those shares sum to what this returns, so what the player is shown adding up
// and what the monster loses cannot disagree.
double RollFactor(const SwingRolls& rolls, std::mt19937& rng,
                  std::vector<LineRoll>* lines = nullptr);

// How two shares of ignored monster DEF meet: in reverse, so what is left of
// the armour is the product of what each share leaves standing. 30% and 40%
// come to 58%, and no pile of sources ever reaches all of it. Every source
// combines this way -- two skills, two items, or a skill and an item.
double CombineIgnoredDefense(double a, double b);

// The first factor of the GMS damage chain, and the game's whole notion of one
// weapon class hitting harder than another. It belongs to the job and the
// weapon together rather than to either alone: a one-handed sword is 1.24 in a
// Paladin's hands and 1.34 in a Hero's.
//
// The published table is keyed by 4th job, which this game does not reach, so
// what is written here is per weapon -- each carrying the constant of the job
// line that owns it -- with an override wherever a line we do have disagrees.
// A weapon no line lists at all is 1.0.
double WeaponConstant(Job job, EquipType weapon);

// The mastery a job line swings at before it learns a mastery skill, which
// GMS sets by what the line fights with: 20% for the warrior's and thief's
// melee weapons, 15% for the bow and crossbow, 25% for wand and staff. The
// skill's grant ADDS to this, so a line ends at 70/65/75 in 2nd job and
// 90/85/95 in 4th -- and no mastery skill can ever make a swing wilder than
// the bare one, which taking the better of the two had to guard against.
double BaseMastery(Job job);

// Whether a skill of this kind carries a damage multiplier of its own -- the
// swings and the things that fire on their own clock. The rest either fold
// into the character's stats or do nothing we model.
bool DealsDamage(SkillKind kind);

// Every lever `base` and `per_level` grant together at `level`, folded over
// the whole message at once: each field is base + per_level x (level - 1),
// which is the ladder every SkillEffect pair in the book is read up.
//
// Ask this rather than spelling the fold out field by field. A caller that
// names its own fields has to be edited again the next time a lever is added,
// and the levers are added often; this one walks whatever the message holds.
//
// Bools have no ladder, so one set on either side stands.
SkillEffect EffectAt(const SkillEffect& base, const SkillEffect& per_level,
                     int level);

// What a ladder's value comes to as a whole number. Every number a SkillEffect
// carries is a double, so that a value climbing every few levels rather than
// every one is stated as a fraction of a level's step -- 0.2 is one more every
// fifth, and GMS's own ceil(L/5) is `base 1, per_level 0.2`. The floor is
// taken here, per source, because a skill grants whole points; the epsilon
// covers the arithmetic that got us to a round figure, since 1 + 0.2 x 5 is
// not exactly 2.
int WholeValue(double value);

// How many times one swing of `skill` strikes each enemy at `level`: its
// `lines`, plus whole lines its `lines_per_level` has bought since level 1.
// Never below 1, so an attack that says nothing still lands once.
//
// Every reader of the line count goes through here -- the damage chain, the
// meso a Chief Bandit's explosion knocks loose, and the skill page -- because
// a skill whose lines climb has to climb everywhere at once.
int SkillLinesAt(const Skill& skill, int level);

// What a skill's empowered form calls itself: the target's display name behind
// "Empowered ". The form is a swing of its own and is keyed by this name
// wherever a boost is looked up, so the fight and the fold have to spell it
// the same way -- see EmpoweredForm and SkillBoost::reaches_empowered_form.
std::string EmpoweredSkillName(const std::string& target);

// How many Combo Orbs `skill` surrounds the character with at `level`: its
// `combo_orbs`, plus whole orbs its `combo_orbs_per_level` has bought since
// level 1. 0 for the skills that hand out none, which is most of them.
//
// Here for the reason SkillLinesAt is: the stat line and the skill page both
// ask, and a ring that grows has to grow in both.
int ComboOrbsAt(const Skill& skill, int level);

// How long `skill` is unavailable for after it is used, at `level`, in GMS
// scale: its `cooldown_seconds` plus the per-level step, never below nothing.
// 0 for the skills that are there every time, which is most of them.
//
// Here for the reason SkillLinesAt is: the fight and the skill page both ask,
// and a wait that shortens as the skill is taught has to shorten in both.
double CooldownAt(const Skill& skill, int level);

// What is left of a `wait` once the seconds a potential takes off it are
// paid. GMS's own rule, which is not a plain subtraction: a wait under 5
// seconds gives up nothing, one of 5 to 10 gives up 5% of itself per second
// offered rather than the second, and the part of a longer wait that would
// fall under 10 seconds is halved on the way down.
double ReducedCooldown(double wait, double reduction_seconds);

// Hits `shield` cancels at `level`: its `hits` plus what `hits_per_level` has
// bought since level 1, floored. 0 for a buff that is not a shell.
//
// Here for the reason SkillLinesAt is: the fight and the skill page both ask,
// and a shell that thickens as the skill is taught has to thicken in both.
int ShieldHitsAt(const Shield& shield, int level);

// What one skill in the character's book hands ONE other skill by name, summed
// across every skill granting it. Only the swing being priced reads its own
// entry, so none of this follows the character onto their next attack --
// see SkillBoost::effect.
struct SkillBonus {
  // Damage per line, added to the target's own skill_pct exactly as GMS states
  // it: percentage POINTS on the named skill, worth their value once per line.
  // Aimed at a passive that carries a Final Attack, it lands on that strike's
  // multiplier instead -- see FinalAttackSource::damage_pct.
  double skill_pct = 0.0;
  // Plain % damage this swing alone collects, summed into the share the
  // character's gear and passives already pay. The other damage a boost can
  // grant, and the one a Hyper Skill's Reinforce is -- see SkillBoost::effect.
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
  // Chance added to the named skill's Final Attack, for a boost aimed at a
  // passive that carries one. What it strengthens is the extra hit the target
  // sets off, so it is folded onto that source rather than read where the
  // swing is built. See FinalAttackSource and SkillBoost::effect.
  double final_attack_chance = 0.0;
  // Points on the named skill's burn tick. The other lever here that never
  // reaches the swing: the burn takes the rest of this struct through the
  // stat line it is priced off, but states its own multiplier, so this one is
  // added where BurnFor writes it. See SkillBoost::dot_skill_pct.
  double dot_skill_pct = 0.0;
  // Seconds added to the named skill's burn, for the same reason as above: the
  // burn's clock is its own. See SkillBoost::dot_duration_seconds.
  double dot_duration_seconds = 0.0;
};

// What a character's learned passives add to every swing, whichever attack
// they end up choosing. Kept together rather than passed one at a time: the
// list grows with each job book, and none of it depends on the target.
// DerivedStatsFor produces the values; see character_stats.h.
struct PassiveOffense {
  double crit_rate = 0.0;  // added chance for a swing to crit (0.40 == 40%)
  double crit_dmg = 0.0;   // added critical damage (0.05 == +5%)
  // What the best mastery skill grants, 0..1, before the job line's own base
  // is added under it. 0 for a character holding no such skill.
  double mastery = 0.0;
  // Plain % damage and final damage, already combined across every passive
  // that grants them -- summed and multiplied respectively, which is where the
  // two differ. See DerivedStats.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of one line a shadow copy of the swing lands, per line the swing
  // already has. See DerivedStats::mirror_line_pct.
  double mirror_line_pct = 0.0;
  // Strikes added to a swing that already lands more than one. See
  // SkillEffect::bonus_attack_lines.
  int bonus_attack_lines = 0;
  // Share added to damage against a boss, summed across the passives granting
  // it. Meets the gear's own by summing too -- both are shares of the same
  // damage, unlike ied.
  double boss_pct = 0.0;
  // The same against everything that is not a boss, out of Hyper Stats, Inner
  // Ability and the passives granting one. No gear carries it.
  double normal_pct = 0.0;
  // Share of the monster's DEF the passives ignore, already combined across
  // them. Meets the gear's share in reverse, the same way they combined.
  double ied = 0.0;
  // What the book hands particular skills, keyed by display name. Only the
  // entry matching the skill being swung is read, and most characters carry
  // none.
  std::map<std::string, SkillBonus> skill_bonus;
  // What the map's Arcane Force requirement leaves of the swing: 1 everywhere
  // outside Arcane River, a tenth against a map the character has no force
  // for, half again against one they have half again over. A multiplier
  // rather than a share, which is why it defaults to 1 and not 0.
  double arcane_pct = 1.0;
};

// Builds OffenseStats from a character's job, level and summed stats. The job
// picks primary/secondary; attack, boss_pct and ied come from gear.
//
// `attack_skill` is the learned attack being swung, at `attack_level`, or null
// for the bare 100% poke. Choosing WHICH attack is the caller's job -- it
// depends on how many mobs are up, which this pure per-mob math cannot see.
//
// `weapon` is what is in hand, which the summed EquipStats no longer says: it
// decides the weapon constant, and nothing else here.
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
// 0 against a monster carrying no defence, and against one whose defence the
// character has already cancelled outright -- neither has anything left to
// ignore. 0 as well while c is still 1 or more, which only defence past 100%
// reaches: the swing sits on its 1-damage floor and a further e does not lift
// it off. See SkillEffect.ied_pct_per_freeze_stack, the one lever that asks.
double DefenseShare(const Mob& mob, double ied);

// One number for "how hard this character hits", for comparing characters
// rather than predicting a swing: the damage chain with everything that
// depends on the target, the skill or the moment stripped out. Build the stats
// with a null attack skill, since skill_pct, lines, ied, ier and level are all
// ignored here.
//
// Unlike GMS, crit is weighted by its rate: a flat `1.35 + crit damage` would
// price critical damage the same whether it lands every swing or never.
//
// `vs_boss` says which monster the number stands for, and decides which of
// boss %dmg and normal %dmg counts -- never both, since no swing ever meets
// both. There is no target here, so the caller names one.
int CombatPower(const OffenseStats& offense, bool vs_boss);

// What a character brings to being hit. The defensive mirror of OffenseStats:
// DerivedStatsFor produces both fields, and the character's own level decides
// how much of the DEF actually counts.
struct DefenseStats {
  int level = 0;
  int def = 0;
  // The share of incoming damage cancelled after the formula below has run
  // (0.10 == 10% less taken).
  double damage_taken_pct = 0.0;
  // Chance the hit misses outright (0.30 == 30%). A miss and a reduction come
  // to the same thing over enough hits, and enough hits is all this function
  // ever reports -- see DerivedStats::dodge_chance.
  double dodge_chance = 0.0;
  // Share taken off the monster's own attack before the formula runs (0.30 ==
  // 30% less), and whether it stands against a boss too. Apart from the
  // reduction above because it lands somewhere else entirely: a weakened
  // monster is one whose attack the character's DEF cancels more of.
  double enemy_attack_pct = 0.0;
  bool enemy_attack_reaches_boss = false;
  // What the map's Arcane Force requirement does to the monster's hit: 1
  // outside Arcane River, up to 2.8 against a map the character has no force
  // for, and 0 against one they have half again over -- which the damage floor
  // turns into GMS's 1 damage.
  double arcane_taken = 1.0;
};

// Expected damage of one hit from `mob` -- min and max rolls averaged, no RNG.
// Never less than 1, as in GMS.
//
// DEF subtracts flatly from the mob's attack, but can never cancel more than
// 80% of it. That cap is the shape of the whole thing: on a map near the
// character's level their DEF clears it and more armour buys nothing, and on
// one far above it the cap never binds and the hit lands close to full.
double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob);

// The GMS level multiplier: 1.1 at the monster's level, rising to 1.2 at +5
// and beyond, and falling to 0 at 40 levels under it.
double LevelMultiplier(int player_level, int mob_level);

// Seconds between swings: base_delay_ms * (20 - stage) / 16, rounded up to
// whole kTickMs units. Stage 1..10, 10 fastest, 4 == base. GMS's own formula.
//
// `base_delay_ms` belongs to the SKILL being swung -- see Skill::base_delay_ms.
// The weapon's say in how fast a character attacks is the stage, and nothing
// else; a spear and a sword swing the same skill at the same speed.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// The stage at which the formula above is the identity, so a skill that
// ignores attack speed swings at exactly its stated delay. Named rather than
// written as a 4, because a bare 4 beside a stage means "average weapon" and
// this means "no scaling at all".
inline constexpr int kUnscaledAttackSpeedStage = 4;

// The fastest stage an ordinary character reaches, however much their weapon,
// their book and their Inner Ability come to. A soft cap: a source that says
// it may pass one adds on top of it -- see AttackSpeedStage.
inline constexpr int kAttackSpeedSoftCap = ATTACK_SPEED_FASTEST_1;

// `base` and `bonus` held to the soft cap, then `uncapped` on top, held to the
// fastest stage the formula models. The two bonuses stay apart because only
// the second may pass the cap, which is what makes it worth a stage to a
// character already sitting on 8.
int AttackSpeedStage(int base, int bonus, int uncapped);

// True when the job attacks with magic attack rather than weapon attack. The
// damage chain treats the two alike; what differs is which field it reads,
// and what the weapon has to say about how fast it swings.
bool SwingsOnMagic(Job job);

// The stage a character's attacks start from, before the passives that speed
// them add on top. Usually the weapon's own, but GMS casts every spell at the
// unscaled stage whatever the magician holds. A staff is Slow and a mage
// casting from one is not.
int BaseAttackSpeedStage(Job job, int weapon_stage);

// What the bare poke swings at, and what a skill naming no delay of its own is
// taken to swing at. 780ms is the commonest 1st/2nd job animation, and the one
// both Brandish and Spear Sweep have.
inline constexpr int kDefaultSwingDelayMs = 780;

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
