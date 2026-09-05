/* What the player is fighting: the current map's mobs, how hard each one is
 * hit, and how fast the swings and respawns come.
 *
 * ComputeCombatParams() reads all of that off a GameState once, into a plain
 * CombatParams. The fight then steps purely from those params (see fight.h), so
 * it needs no game state of its own and cannot quietly disagree with the
 * encounter it is meant to be playing out. All durations are in game-scaled
 * seconds: real ones stretched by the character's GameSpeedFactor, which grows
 * with their level.
 */
#ifndef MS_SRC_COMBAT_ENCOUNTER_H_
#define MS_SRC_COMBAT_ENCOUNTER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/character/character_stats.h"
#include "src/character/stat_preset.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// One targetable mob type in the current encounter. `mob` is the single source
// of truth (name, HP, EXP, drops); it is owned by GameState and outlives the
// step it is used in.
struct CombatType {
  const Mob* mob = nullptr;
  int simultaneous = 0;  // how many spawn at once (spawn_count / type count)
  // Where each of them stands, for a boss arena that drew them a place. Empty
  // on a map. Carried here rather than read back off the phase because a
  // spawn whose mob the catalog does not hold never becomes a type, and an
  // index into the phase would slide every part after it onto the wrong cell.
  std::vector<ArenaSpot> spots;
  // Seconds between one of these stepping along its row, from the spawn that
  // named it. 0 for everything that stands still. See Spawn.
  int move_interval_seconds = 0;
  // Expected damage one hit from this mob does to the player, already through
  // their DEF. Held per type rather than per mob because every member of a
  // type hits alike.
  double damage_to_player = 0.0;
  // The same once this mob is SCARRED, which weakens its attack further. Equal
  // to the line above for every character who scars nothing.
  double damage_to_player_scarred = 0.0;
};

// One block of lines inside a swing, and what makes it vary. A swing is one of
// these plus one per extra hit the skill lands: the parts differ in line count
// and in how often they crit, so each rolls on its own.
struct HitGroup {
  std::vector<double> damage;  // per target type, parallel to CombatParams
  SwingRolls rolls;
};

// A swing that is HELD: the clock its pulses fall on, how many of them a full
// hold is worth, and the floor under the shortest one. The damage is in the
// swing's own groups -- the first is one pulse, the rest are the strike the
// hold ends on -- since a hold is that pulse landed over and over.
//
// `pulses` is 0 for every attack that is simply swung, which is all of them
// but Lightning Orb. See Channel.
struct ChannelHold {
  int pulses = 0;
  // The fewest a cast is committed to: the pulses that fit inside the floor
  // below, which is the shortest the animation can run.
  int min_pulses = 0;
  double pulse_seconds = 0.0;
  double finish_seconds = 0.0;
  double min_seconds = 0.0;
  // Share of every hit the player takes that the hold cancels while it runs.
  double damage_taken_pct = 0.0;
};

// One burn a swing leaves on the enemies it reaches: what one tick is worth
// per target type, the clock it burns on, and how it takes hold. A swing
// carries one of these per source that marks what it hits. Game-scaled, like
// every other duration here.
struct DotApplication {
  std::vector<double> damage;  // per target type, one tick's worth
  SwingRolls rolls;
  double interval_seconds = 0.0;
  double duration_seconds = 0.0;
  // Chance it takes hold on each enemy reached. 1 for a burn a swing simply
  // leaves, which is all of them bar the poison on a rogue's claw.
  double chance = 1.0;
  // Helpings one monster can carry at once, each ticking for the whole damage.
  int max_stacks = 1;
  // Which mark this is, so two burns on one monster do not overwrite each
  // other: an index into the slots every mob carries. Assigned per SOURCE, so
  // the poison a character keeps on their claw writes one slot whichever swing
  // applied it -- and the numbering is the same in every buffed set, so a slot
  // the fight is holding means the same thing however the buffs come and go.
  int slot = -1;
  // Whether the CHARACTER carries this burn rather than the attack stating it.
  // The poison on a rogue's claw rides their own swings and nothing else; a
  // burn a skill states is that skill's own, and a summon leaves it.
  bool carried = false;
};

// One Final Attack following a swing: a chance, rolled once per enemy the
// swing reached, that one more hit lands on that enemy. `count` is above 1
// only for a source that rides the LINES rather than the swing -- the meso a
// Chief Bandit knocks loose is rolled once per line.
struct FinalAttackRoll {
  double chance = 0.0;
  int count = 1;
  std::vector<double> damage;  // per target type, one hit's worth
  SwingRolls rolls;            // how that hit itself varies
};

// One chance a swing has to land harder on one of the enemies it reached, and
// what firing it hands the character back. Rolled once for the whole swing --
// see the Proc message.
struct ProcRoll {
  double chance = 0.0;
  double damage_pct = 0.0;  // share added to what that one enemy takes
  double hp_recover_pct = 0.0;
};

// One thing the character could spend a swing on: the bare poke, a learned
// attack skill, or a cast that does something else with the swing entirely.
// Which one is best depends on how many mobs are actually in front of the
// player, so the choice is made per swing by the fight rather than fixed here
// -- a wide skill that does less per target wins on a crowd and loses on the
// last mob standing.
struct AttackOption {
  std::string name = "Attack";  // shown on the charge bar
  int max_enemies = 1;          // front-of-queue mobs one swing reaches
  // Strikes one swing of it lands on one enemy. Read by the things that count
  // hits rather than swings -- a buff charged by landing them, and the Freeze
  // Stacks an elemental swing leaves or spends.
  int lines = 1;
  // What this swing gains for every enemy it has already gone through, as a
  // fraction -- compounding, so the k'th enemy it reaches takes (1 + this)^k.
  // 0 for a swing that hits everything it reaches alike, which is all of them
  // but Piercing Arrow. See Skill::pierce_gain_pct.
  double pierce_gain_pct = 0.0;
  // Expected damage per target, parallel to CombatParams::types.
  std::vector<double> damage_per_hit;
  // The same swing broken into the blocks that roll. Their expected damages
  // sum to damage_per_hit, so a reader after the average never looks in here.
  // Empty lands the average itself, which is what a caller building an attack
  // by hand wants.
  std::vector<HitGroup> groups;
  // Seconds one swing of this attack takes, game-scaled. Per attack rather
  // than per encounter because the delay belongs to the skill: a slower
  // animation is the price a harder-hitting skill pays. 0 for an attack that
  // fires on its own clock instead -- see interval_seconds.
  double swing_seconds = 0.0;
  // Seconds between casts, for an attack that fires on its own clock rather
  // than on the character's swing. 0 for the swings themselves, which are
  // paced by swing_seconds.
  double interval_seconds = 0.0;
  // Landed swings between casts, for an attack clocked by the character's
  // attacking rather than by the clock. 0 for everything else. An attack
  // carries one of the three clocks, never two.
  int attacks_per_cast = 0;
  // Enemies defeated between casts, for an attack clocked by the dying rather
  // than by either of the above. 0 for everything else.
  int kills_per_cast = 0;
  // What one landed swing of this attack counts toward the field above, on
  // whichever attacks are so clocked. 1 for an ordinary swing; less for one
  // that lands several times a second.
  double count_weight = 1.0;
  // Seconds this attack cannot be swung for after it lands, game-scaled. 0 for
  // one that is there every time, which is most of them.
  double cooldown_seconds = 0.0;
  // Expected damage of the opening hit, per target type. It lands on ONE of
  // the mobs the swing reached -- the healthiest of them, because a hit this
  // big is worth least where it overkills. Empty for a swing that lands once,
  // which is most of them.
  std::vector<double> lead_damage;
  // How many of the reached mobs the opening hit lands on, healthiest first.
  // 1 for the Rogue's shape, which strikes one and spreads; more for a swing
  // whose second half simply reaches fewer enemies than its first.
  int lead_enemies = 1;
  // What the opening hit varies by. Its own, because it lands on its own line
  // count rather than the swing's.
  SwingRolls lead_rolls;
  // Strikes this swing scatters over the enemies it reached, spreading before
  // it doubles up. 0 for a swing that lands on each of them once, which is all
  // of them but Megiddo Flame -- see Skill::scatter.
  int scatter_hits = 0;
  // What a strike keeps when it lands on an enemy an earlier strike of the
  // same cast already reached (0.45 == GMS's "Final Damage -55%"). Read only
  // where scatter_hits is set.
  double scatter_repeat_kept = 1.0;
  // Expected Final Attack damage per target type, landing on every mob the
  // swing reached: it rolls separately for each of them. Empty for a character
  // with no Final Attack, for a swing none of theirs follows, and for the
  // skills that fire on their own clock -- those are not the character's swing.
  std::vector<double> final_attack_damage;
  // The same, one entry per source, as what actually rolls. Empty lands the
  // average above -- what a caller building an attack by hand wants.
  std::vector<FinalAttackRoll> final_attack_rolls;
  // Blizzard's shape: the pair above, for the sources that roll ONCE for the
  // whole swing and land on one of the enemies it reached. Kept apart rather
  // than flagged inside the vectors above, because the difference is where the
  // damage is added -- once per swing here, once per enemy there.
  std::vector<double> single_final_attack_damage;
  std::vector<FinalAttackRoll> single_final_attack_rolls;
  // The burns this swing leaves on the enemies it reaches. Empty for every
  // swing that leaves none, which is most of them.
  std::vector<DotApplication> dots;
  // Share of the player's HP pool this option puts back instead of dealing
  // damage (1.00 == all of it). 0 for every attack, which is all of them bar
  // the Cleric's Heal. An option carrying this deals no damage at all, and the
  // fight picks it by need rather than by rate -- see CombatSim::HealToCast.
  double heal_fraction = 0.0;
  // Share of the pool a landed swing of THIS attack puts back, on top of
  // whatever the character's passives recover on any swing. 0 for all of them
  // bar Angel Ray, which heals the Bishop as it lands. Costs no swing, unlike
  // heal_fraction above -- the damage still goes out.
  double hp_recover_pct = 0.0;
  // The bigger swing that takes the PLACE of every empowered_every'th swing of
  // this one, and how often that is. Null and 0 for every attack but the one
  // the Sniper's Empowered Arrows names. Shared rather than owned outright
  // because an AttackOption is copied freely, and the form never changes.
  std::shared_ptr<const AttackOption> empowered;
  int empowered_every = 0;
  // Whether the count runs per ENEMY rather than per swing. Set, nothing is
  // replaced: the swing marks each mob it reaches, and the form lands on top
  // of the ordinary strike for whichever of them came due. Only Divine
  // Judgment sets it.
  bool brands_enemies = false;
  // The second attack this swing sets off, on the wait its own
  // cooldown_seconds states. Null for every attack but Showdown, whose
  // shuriken goes out once in five seconds with whatever swing lit it. Shared
  // rather than owned outright for the reason `empowered` is: an AttackOption
  // is copied freely and the strike never changes.
  std::shared_ptr<const AttackOption> side;
  // The chances this swing has to land harder on one of the enemies it
  // reached. Empty for every character but a Sniper, and stripped from
  // anything on a clock of its own -- what GMS rolls is the character
  // attacking.
  std::vector<ProcRoll> procs;
  // What this swing does with the character's Freeze Stacks: an ice swing
  // leaves `freeze_build` of them, a lightning swing spends one per line and
  // takes `freeze_fd_per_stack` of final damage for each it went in holding.
  // Both are 0 for every swing of every other character.
  int freeze_build = 0;
  bool freeze_spends = false;
  double freeze_fd_per_stack = 0.0;
  // What one HELD stack adds to this swing's damage as a share, through the
  // critical damage Freezing Crush grants. Linear in the stacks held, since
  // what a stack really adds is critical damage rather than damage.
  double freeze_crit_gain = 0.0;
  // The same for the magic attack Glacial Fury pays per stack, which only an
  // ICE swing collects. 0 for every other swing and for a character without
  // the buff up.
  double freeze_matt_gain = 0.0;
  // Game-scaled seconds this swing leaves the enemies it reached frozen. 0 for
  // every swing
  // that freezes nothing, which is all of them but the I/L's ice -- and not
  // even all of those: Frozen Orb slows what it touches and freezes none of
  // it. A summon carries it like any other swing, since Elquines freezing what
  // it touches is the whole reason the pair below stays lit.
  double freeze_seconds = 0.0;
  // Shatter's, as the share one held stack adds to this swing against each mob
  // type -- parallel to damage_per_hit, and per type because what ignoring a
  // little more defence is worth is that mob's own. Empty for a character
  // whose stacks ignore none, and worth nothing where the defence is already
  // cancelled or was never there.
  std::vector<double> freeze_ied_gain;
  // The scar this swing leaves and what it collects from one already there:
  // each line has `scar_chance` of scarring the mob it lands on for
  // `scar_seconds`, and every line landed on a scarred mob takes `scar_fd` of
  // final damage. All three are 0 for every character but a Crusader's line.
  //
  // The chance and the seconds are the character's own swings' alone -- a
  // summon's pulse and a Final Attack scar nothing -- where the final damage
  // rides anything that lands on a scarred monster.
  double scar_chance = 0.0;
  double scar_seconds = 0.0;
  double scar_fd = 0.0;
  // What the condition the enemy is ALREADY in adds to this swing. The first
  // is Storm Magic's and Burning Magic's, taken whole on a monster under any
  // status the fight keeps -- frozen or burning -- and nothing extra for a
  // second one over the first. The rest are Elemental Drain's: final damage
  // for each burn alight on the group, counted up to `dot_count_cap`.
  //
  // All three are 0 for every character but an I/L and an F/P magician.
  double fd_when_afflicted = 0.0;
  double fd_per_dot = 0.0;
  int dot_count_cap = 0;
  // Which of the character's buffs has to be standing for this to fire at all,
  // as an index into CombatParams::buffs, or -1 for a clock that runs on its
  // own. Puncture's wound is the case it exists for: what ticks is the wound,
  // so it ticks only where one was left. Off-clock attacks only -- a swing is
  // chosen rather than fired.
  int needs_buff = -1;
  // Strikes one due tick fires, each landing in full on its own. 1 for every
  // clock but Cry Valhalla's, whose three sword strikes fall together.
  int strikes_per_pulse = 1;
  // Ticks one raising of the gating buff is worth, after which this falls
  // silent until the buff comes round again. 0 for a clock that never runs
  // out, which is every other one. See BuffPulse.max_pulses.
  int max_pulses = 0;
  // The hold this swing is, for the one skill that is held. Its damage_per_hit
  // above is a FULL hold, so an attack weighed without asking is weighed at
  // what holding it to the end is worth.
  ChannelHold channel;
};

// Seconds a hold of `pulses` takes: the pulses on their own clock and the
// strike it ends on, never shorter than the animation's own floor.
double HoldSeconds(const ChannelHold& hold, int pulses);

// Everything the character can attack with, as it stands under one particular
// set of buffs. The same attacks in the same order in every set -- what
// differs is the damage -- so an index the fight is holding stays good however
// the buffs come and go.
struct AttackSet {
  std::vector<AttackOption> attacks;
  std::vector<AttackOption> auto_attacks;
  std::vector<AttackOption> triggered_attacks;
  // Freeze Stacks the character can hold under these buffs. Here rather than
  // on the params alone because Glacial Fury deepens the pile only while it
  // stands, and a buff's whole effect is a set of its own.
  int freeze_cap = 0;
};

// What one combination of buffs needs to have its attack set built: the same
// inputs AddAttacks was handed, kept so a window can be built the first time
// the fight asks for it rather than all of them up front.
//
// Borrowed, not owned -- exactly as CombatType::mob is. The params are read
// while the state they were computed from stands, and a change to that state
// is what recomputes them.
struct BuffedSetSource {
  const GameState* state = nullptr;
  const EquipPrototype* weapon = nullptr;
  // The buffs in CombatParams::buffs order, so bit i of a mask is skill i.
  std::vector<const Skill*> buff_skills;
  double speed_factor = 1.0;
  StatPreset preset = StatPreset::kFarming;
  // Whether a window's reach is halved on the way out, which is what a boss
  // fight does to every list a swing can be picked from.
  bool halve_reach = false;
};

// A buff the character puts up for a while, on a wait of its own. What it
// GRANTS is not here: it is already folded into the buffed attack sets,
// because a lever like ignored defence cannot be applied to a damage number
// after that number has been worked out.
struct BuffOption {
  std::string name;
  // All game-scaled, like every other duration here.
  double duration_seconds = 0.0;
  double cooldown_seconds = 0.0;
  // Seconds a landed swing takes off the wait for the next cast.
  double cooldown_reduction_seconds = 0.0;
  // Share of every hit the player takes that this buff cancels while it
  // stands (0.10 == a tenth of it). Smokescreen alone, and the first thing in
  // the game to buff a defence rather than an attack. Multiplies with what the
  // character already cancels, as every reduction does.
  double damage_taken_pct = 0.0;
  // Share of the pool the cast puts back at once (1.00 == all of it).
  double heal_fraction = 0.0;
  // Hits this buff cancels outright while it stands, after which it falls
  // whatever is left of its clock. 0 for a buff that is not a shell, which is
  // every one but Holy Magic Shell. See Shield.
  int shield_hits = 0;
  // Share off a hit the shell cannot block -- a boss's -- taken instead of
  // blocking it. Read only where shield_hits is set.
  double boss_damage_taken_pct = 0.0;
  // Seconds raising this costs the character: the animation is time they are
  // not swinging in, taken off the swing they were charging. 0 for a buff with
  // no cast of its own -- one a swing lays, and one a passive charges by
  // landing lines.
  //
  // Not scaled by the weapon's attack speed. A booster hurries a swing, not
  // the arm-raise of a buff, which is the same line every other clock in the
  // game is on the far side of.
  double cast_seconds = 0.0;
  // Lines the character has to land before this goes up, instead of a wait in
  // seconds. 0 for a buff on a clock, which is every other one. See
  // Buff::charge_lines.
  int charge_lines = 0;
  // Index into AttackSet::attacks of the swing that lays this buff, or -1 for
  // one the character raises on its own wait. A buff hanging off an ATTACK is
  // inseparable from the swing that delivers it -- Puncture's wound is left by
  // puncturing something -- so the fight has to spend a swing to put it up.
  // Always -1 for a party buff: what lays that is an ally's cast, which this
  // fight never sees.
  //
  // An index rather than a name, because the attacks are the same in the same
  // order in every buffed set: one index stays good however the buffs come and
  // go.
  int laid_by_attack = -1;
};

// A snapshot of the current encounter's combat parameters.
struct CombatParams {
  bool active = false;  // false when not farming (no map/weapon/mobs)
  // What these params describe: a map's name while farming, and one phase of
  // a boss fight otherwise. The fight watches it to know when it is playing
  // out a different encounter than the one it holds a roster for, so a phase
  // turning over rebuilds the roster exactly as walking to another map does.
  std::string encounter;
  // Time between full-roster respawn beats. 0 for an encounter that never
  // refills -- a boss fight is the roster it opened with.
  double respawn_seconds = 0.0;
  // Time between mob hits on the player. 0 for an encounter whose monsters do
  // not hit back, which is every boss fight for now.
  double hit_seconds = 0.0;
  int max_player_hp = 0;  // what a full heal fills the player back to
  // The character's level. The fight watches it for the level-up fill, which
  // cannot be read off max_player_hp: a skill point, a scroll or a swapped hat
  // all widen the pool too, and none of them is a reason to be healed.
  int player_level = 0;
  // Share of that pool the player gets back on every respawn beat, whether or
  // not they cleared the map (0.10 == a tenth of it). What lets a map be
  // survived by outlasting it rather than only by emptying it.
  double beat_heal_fraction = 0.0;
  // Share of every hit the player takes that goes back into the mob that
  // landed it (1.20 == 120% of the damage taken). 0 for a character with no
  // reflection, which is all of them until Spirit Blade is learned.
  double damage_reflect_pct = 0.0;
  // Share of the pool a landed swing puts back, from a passive that heals on
  // attack. Costs no swing, so it stacks with the beat heal rather than
  // replacing it, and pays nothing on an empty map -- there is nothing to hit.
  double hp_recover_pct = 0.0;
  // Extra EXP every kill yields, as a fraction of what the mob was worth. Read
  // by AwardCombatRewards rather than by the fight -- Holy Symbol is the one
  // skill whose payment is not made in the fight it was cast in.
  double exp_pct = 0.0;
  // Share added to the meso every kill yields: everything the character wears
  // and everything they are granted, already capped -- see MesoBonus.
  double meso_pct = 0.0;
  // What multiplies that meso afterwards. 1 for everyone holding no potion,
  // and nothing caps it.
  double meso_final_mult = 1.0;
  // Share added to how often a kill drops anything, from what the character
  // wears and what their passives grant. Read by AwardCombatRewards, like
  // exp_pct above: it raises the chance of a drop rather than the size of one.
  double item_drop_pct = 0.0;
  // The fountains the character carries, their intervals already stretched by
  // the pacing band. Each runs on its own clock, whether or not the character
  // is swinging.
  std::vector<RegenPulse> regen_pulses;
  // Seconds between one revival and the next, game-scaled. Above 0 for a
  // character whose passives revive them: the hit that would have killed them
  // fills the pool instead, and the wait starts over. 0 for everyone else.
  double revive_cooldown_seconds = 0.0;
  // How many distinct burns the character can leave, and so how many slots a
  // monster needs. 0 for everyone who leaves none.
  int dot_count = 0;
  // Freeze Stacks the character can hold at once. 0 for everyone who holds
  // none, which switches the whole mechanism off.
  int freeze_cap = 0;
  // Whether a swing picks the healthiest of the roster rather than whatever
  // stands at the front of the queue. On for a boss, whose parts differ in HP
  // and none of which respawns: a swing too narrow to reach the whole of it
  // spends itself on the parts that would otherwise outlast the fight, and
  // every part stays standing to take a hit rather than the reach idling once
  // the small ones are dead. Off on a map, where a roster of one kind of
  // monster is refilled on the beat.
  bool focus_healthiest = false;
  // Whether the fight should record every line it lands, for a caller drawing
  // the damage as numbers. The boss screen asks for it; the map does not, and
  // neither do the sims, which step the fight millions of times.
  bool record_damage_lines = false;
  std::vector<CombatType> types;  // in map order
  // Every attack available, the bare poke first. Never empty while active.
  std::vector<AttackOption> attacks;
  // Attacks that fire on their own clock beside whatever is being swung --
  // summons and cooldown skills. Not candidates for the swing, so a wide one
  // never crowds out the character's own attack; they simply also happen.
  std::vector<AttackOption> auto_attacks;
  // The same, but clocked by something counted rather than by seconds passed:
  // swings landed, or enemies defeated. Held apart from auto_attacks because
  // the fight has to count something for these and nothing for those, and one
  // list with an empty field on half its entries would hide which half.
  //
  // A cast of one of these does not itself count toward any swing count: what
  // the player is being paid for is their own attacking. What it kills does
  // count, since a defeat is a defeat however it was dealt.
  std::vector<AttackOption> triggered_attacks;
  // The timed buffs this character can put up. Empty for everyone but a Dark
  // Knight; the fight runs their clocks and asks for the matching attacks.
  std::vector<BuffOption> buffs;
  // One slot per combination of those buffs, indexed by the bitmask of which
  // are up, less one -- the three lists above are the set for none of them.
  // Empty when nothing grants a buff.
  //
  // A slot is filled the first time the fight asks for it, off buffed_source
  // below, and most of them never are: the count doubles with each buff, while
  // the combinations a fight actually stands in do not. Mutable for that
  // reason, which makes the four readers below unsafe to call on one
  // CombatParams from two threads at once. Nothing does; a sim gives every
  // worker its own.
  mutable std::vector<std::optional<AttackSet>> buffed;
  // What those slots are built from. Empty for params nothing can build --
  // a hand-built one in a test, where every slot is filled up front.
  BuffedSetSource buffed_source;
  // The buffs the REST OF THE PARTY puts up over this character, on their
  // casters' clocks. Kept apart from `buffs` above because that vector's index
  // is the bitmask into `buffed`, and a party buff has no attack set of its
  // own: all it can grant is a share off what a hit costs, which the fight
  // takes off the hit rather than folding into a damage table.
  //
  // Empty outside a party fight, which is everywhere but a boss fought
  // together. See AllyBuffsFor.
  std::vector<BuffOption> ally_buffs;

  // The window `mask` names, built the first time it is asked for. Null for a
  // mask out of range, which is what a fight one step behind a change in what
  // the character has learned holds.
  const AttackSet* Window(int mask) const;
  // The three lists above as they stand with `mask`'s buffs up. Out of range
  // reads as none of them, so a fight one step behind a change in what the
  // character has learned swings unbuffed rather than off the end.
  const std::vector<AttackOption>& Attacks(int mask) const;
  const std::vector<AttackOption>& AutoAttacks(int mask) const;
  const std::vector<AttackOption>& TriggeredAttacks(int mask) const;
  // How deep the pile of Freeze Stacks goes with `mask`'s buffs up. Out of
  // range reads as none of them, exactly as the three above do.
  int FreezeCap(int mask) const;
};

// The weapon the character is holding, or null for one holding nothing. A
// fight needs one -- without it there is no swing and every encounter comes
// back inactive -- so a screen that offers a fight asks this before it starts
// one.
const EquipPrototype* EquippedWeapon(const GameState& state);

// Reads `state`'s current map/character into a CombatParams. active is false
// (and types empty) when there is no current map, no equipped weapon, or no
// loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

// The same for one phase of one boss difficulty. Nothing respawns and nothing
// hits back, and the whole fight runs in real time rather than at the pacing
// band's stretch: a boss is watched, not left alone. active is false for a
// phase out of range, a character holding no weapon, or spawns the mob catalog
// does not know.
CombatParams ComputeBossParams(const GameState& state,
                               const std::string& boss_key,
                               const BossDifficulty& difficulty, int phase);

// What CombatParams::encounter holds for one phase of a boss fight. Distinct
// per phase, which is what makes the fight rebuild its roster when one turns
// over, and distinct from every map name.
std::string BossEncounterKey(const std::string& boss,
                             const std::string& difficulty, int phase);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ENCOUNTER_H_
