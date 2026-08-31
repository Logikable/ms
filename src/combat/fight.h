/* The live fight: the player auto-attacking a map's mobs, clearing the queue,
 * then idling until the next respawn beat refills it -- while the mob at the
 * front of that queue hits back.
 *
 * A beat tops the queue back up to a full roster, leaving whatever is still
 * standing alone, so a fight that outlasts a beat keeps its progress. One
 * swing hits the front mobs at once, as many as the chosen attack reaches.
 *
 * The player's HP lives here and nowhere else, because it never outlives a
 * fight: a slice of the pool comes back on every beat, and the whole of it
 * whenever the map is cleared or changed or the character levels. So a map far
 * above the player is dangerous when its mobs take more between beats than a
 * beat gives back, and one at their level can be held all day.
 *
 * A character holding a healing cast has a third way: below a quarter of their
 * pool they spend a swing on it instead of attacking, which trades kill rate
 * for staying alive on a map that would otherwise be out of reach.
 *
 * This is the single engine behind both halves of combat: the kills it reports
 * each step are what the reward layer pays out for, and the same step drives
 * the panel's animation -- so what the player watches and what they are paid
 * for cannot drift apart.
 */
#ifndef MS_SRC_COMBAT_FIGHT_H_
#define MS_SRC_COMBAT_FIGHT_H_

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/combat/encounter.h"

namespace ms {

// One mob still standing, for a caller that draws them one bar apiece rather
// than merged -- the boss screen, which pins each of Zakum's arms to its own
// panel. `id` is handed out when the mob arrives and never reused, so a slot
// keeps the same monster while the ones beside it die.
struct MobStatus {
  int id = 0;
  int type = 0;  // index into CombatParams::types
  std::string name;
  double hp_fraction = 0.0;
};

// What did the damage, for a caller drawing it. The character's own SWING is
// one source however many skills they swing, so a new swing takes the place of
// the last whatever it was. Everything else is a source apiece, held apart so
// a summon's numbers never take the place of a burn's.
enum class DamageOrigin {
  kSwing,
  kOwnClock,    // a summon, or a skill on a clock of its own
  kSwingClock,  // a skill fired by swings landed rather than by seconds
  kSideStrike,  // the strike a swing sets off beside itself
  kBurn,
};

struct DamageSource {
  DamageOrigin origin = DamageOrigin::kSwing;
  // Which one, where the origin has more than one: which summon, which burn's
  // slot. 0 for the swing, which is one source.
  int index = 0;
};

// Two lines from the same source on the same monster belong to the same stack
// of numbers.
inline bool operator==(const DamageSource& a, const DamageSource& b) {
  return a.origin == b.origin && a.index == b.index;
}

// One line of damage as it landed on one monster, for a caller drawing the
// fight rather than only stepping it. `event` is shared by every line one
// attack put on that monster, so an eight-line swing reads as one stack of
// eight rather than eight stacks of one.
struct DamageLine {
  int mob_id = 0;
  int event = 0;
  DamageSource source;
  double damage = 0.0;
  bool crit = false;
};

// Where a landing is being filed and what scales it, for that record: the
// monster it fell on, the event it belongs to, what did it, and whatever
// multiplies it after the roll -- the Freeze Stacks the swing spent, what an
// arrow gained as it travelled. Passed even by a fight that is not recording,
// which files nothing whatever it is handed.
struct Landing {
  int mob_id = 0;
  int event = 0;
  DamageSource source;
  double scale = 1.0;
};

// One HP bar for the combat panel: a mob type in the engaged window (the front
// mobs the next swing hits), with its members merged into an average HP
// fraction and a count.
struct EngagedGroup {
  std::string name;
  int level = 0;
  int count = 0;
  double hp_fraction = 0.0;
};

class CombatSim {
 public:
  // Advances the fight by elapsed_seconds of real time under `params`. Larger
  // gaps are clamped to one swing, so a long stall costs progress rather than
  // paying out a burst of kills the player never watched.
  void Advance(const CombatParams& params, double elapsed_seconds);

  // Takes the monsters `hp_by_id` names down to the share of their pool it
  // says they have left, and clears away whatever that killed. Downward only:
  // this is for a fight whose roster is kept somewhere else and hit by more
  // than one player, where a local copy may run ahead of the shared one but
  // never behind it. A monster it does not name is left alone.
  void ClampRoster(const CombatParams& params,
                   const std::map<int, double>& hp_by_id);

  // True while a valid encounter is being fought.
  bool active() const {
    return active_;
  }
  // True when the whole roster is dead and the sim is idling until the next
  // respawn beat.
  bool respawning() const {
    return respawning_;
  }
  // The current target's name (empty while respawning or inactive).
  const std::string& target_name() const {
    return target_name_;
  }
  // The current target's level (0 while respawning or inactive).
  int target_level() const {
    return target_level_;
  }
  // The current target's remaining HP as a fraction in [0, 1].
  double target_hp_fraction() const {
    return target_hp_fraction_;
  }
  // Progress toward the next auto-attack as a fraction in [0, 1].
  double attack_fraction() const {
    return attack_fraction_;
  }
  // The name of the swing being charged (the attack skill's, or "Attack" for
  // the bare poke). Empty while inactive or respawning -- with nothing up,
  // there is no swing coming to name.
  const std::string& attack_name() const {
    return attack_name_;
  }
  // The player's remaining HP, rounded up so a sliver still reads as 1 rather
  // than as death. 0 while inactive.
  int player_hp() const;
  // What that HP tops out at, carried through from the params so a caller
  // drawing the pair need not resolve the character's stats again.
  int player_max_hp() const {
    return player_max_hp_;
  }
  // That HP as a fraction in [0, 1] of what the params say it tops out at.
  double player_hp_fraction() const {
    return player_hp_fraction_;
  }
  // True on the one step a hit took the player to 0. Reported rather than
  // acted on, exactly as kills are: what dying costs is the reward layer's
  // business, not the fight's.
  bool died_this_step() const {
    return died_this_step_;
  }
  // Kills recorded during the most recent Advance, indexed to match the
  // params.types passed to that call.
  const std::vector<int64_t>& kills_this_step() const {
    return kills_this_step_;
  }
  // Damage the character dealt during the most recent Advance. Every point a
  // line rolled counts, overkill included: this is what the player would have
  // watched fly off the monsters, not what the monsters had left to give.
  double damage_this_step() const {
    return damage_this_step_;
  }
  // Progress toward the next respawn beat as a fraction in [0, 1]. Stays 0
  // for an encounter that never respawns -- see respawns().
  double respawn_fraction() const {
    return respawn_fraction_;
  }
  // Whether the encounter has a respawn beat at all. A boss does not.
  bool respawns() const {
    return respawns_;
  }
  // True on the step a respawn beat came round, whether or not it had anything
  // to put on the map. The one clock in the fight a watcher can align to.
  bool respawned_this_step() const {
    return respawned_this_step_;
  }
  // The engaged window as HP bars, one per distinct type the next swing will
  // hit, in the order they appear in the queue. Empty while
  // respawning/inactive.
  const std::vector<EngagedGroup>& engaged_groups() const {
    return engaged_groups_;
  }
  // Every mob still standing, in queue order -- the whole roster rather than
  // the engaged window. Refreshed each Advance, like the window is.
  const std::vector<MobStatus>& roster() const {
    return roster_;
  }
  // Every line landed during the most recent Advance, in the order they
  // landed. Empty unless the params asked for the record -- see
  // CombatParams::record_damage_lines.
  const std::vector<DamageLine>& damage_lines_this_step() const {
    return damage_lines_this_step_;
  }

 private:
  // One burn on one monster: how long it has left, how far into the current
  // tick it is, and what a tick of it is worth. The damage is settled when the
  // burn lands and never asked again, which is what makes it a snapshot -- see
  // the Dot message.
  struct MobDot {
    double left_seconds = 0.0;
    double phase = 0.0;
    double interval_seconds = 0.0;
    double damage = 0.0;
    // Helpings of it the monster is carrying, each ticking for the whole
    // damage. 1 for every burn but a Night Lord's poison, and 0 while nothing
    // is burning at all.
    int stacks = 0;
    SwingRolls rolls;
  };

  // A mob waiting in or being fought in the queue: its type (an index into
  // params.types) and its remaining HP.
  struct QueuedMob {
    int type = 0;
    double hp = 0.0;
    // Which monster this is, for a caller holding a bar per mob. Never reused
    // within one encounter, so a bar cannot be handed the mob that replaced
    // the one it was drawing.
    int id = 0;
    // Strikes this mob has taken from a swing that marks what it hits, since
    // the last mark went off on it. A mark rides the mob rather than the
    // swing, so one that dies partway there takes its count to the grave and
    // whatever replaces it starts at nothing.
    int brand = 0;
    // The burns on it, one slot per source the character can leave. Sized only
    // for a character who burns anything, which is the F/P Arch Mage alone.
    std::vector<MobDot> dots;
    // Seconds this monster stays frozen. Set by the ice swings that reach it
    // and counted down between them; 0 for everything an I/L magician has not
    // touched, which is every monster in the game facing anyone else. What
    // being frozen is worth is the character's -- see BoostForStacks.
    double frozen_left_seconds = 0.0;
    // The scar Scarring Sword leaves: how long it stands, and the odds it is
    // there at all. A chance kept as odds rather than rolled, because every
    // other chance in this fight is paid as an expectation -- so a monster
    // half-likely to be scarred takes half of what a scar is worth.
    //
    // Both are 0 for every monster nobody has scarred, which is every monster
    // in the game facing anyone but the Crusader's line.
    double scarred_left_seconds = 0.0;
    double scar_odds = 0.0;
  };

  // Opens the landing about to happen: gives every one of the front `hit` mobs
  // its own event, so the lines one attack puts on one monster group together
  // however many ways the swing reaches it, and remembers what is doing the
  // damage. Nothing for a fight that is not recording.
  void OpenLandings(int hit, DamageSource source);
  // Where the landing on the mob at queue index `index` is filed, scaled by
  // `scale`. The event is the one OpenLandings gave that mob.
  Landing LandingAt(int index, double scale) const;
  // Files one line of `damage`, already scaled, against `landing`.
  void RecordLine(const Landing& landing, double damage, bool crit);
  // Files what the last RollFactor put in `line_rolls_` as a landing of
  // `damage`, each line taking its own share of it.
  void RecordRolls(const Landing& landing, double damage);
  // Where a roll should write its per-line shares: the scratch buffer, or
  // nowhere at all when nobody is reading the record.
  std::vector<LineRoll>* LineSink();

  // Brings out the dead: counts every mob the queue is holding at or below no
  // HP and drops it. Shared by the swing and the burn, since a burn kills the
  // same way a swing does and the two must be counted alike.
  void Reap();
  // Marks everything `attack` just reached with each burn it leaves, where the
  // burn takes hold at all. A mob already burning has its clock restarted and
  // its damage taken fresh from the character as they stand; whether that
  // piles another helping on top depends on what the burn allows.
  void ApplyDots(const AttackOption& attack, int hit);
  // Runs every burn on the queue forward by dt, landing whatever ticks come
  // due. Only the mobs actually burning cost anything here.
  void RunDots(double dt);

  // Runs every fountain forward by dt, pouring whatever pulses come due. Each
  // fills to the pool and no further, and none of them needs a mob on the map.
  void RunRegen(const CombatParams& params, double dt);

  // Brings the queue back up to a full roster, adding only what each type is
  // missing: a respawn puts new monsters on the map, it does not heal the one
  // being fought. Leaves the swing clock alone, since whether a top-up should
  // interrupt the swing depends on why it happened.
  void TopUp(const CombatParams& params);
  // The share of the player's pool a landed `attack` puts back through the
  // chances it rolled. Returned rather than paid here because a strike knows
  // nothing about the pool -- see Proc.
  double RollProcs(const AttackOption& attack, int hit);
  // What a pile `stacks` deep multiplies `attack` by against a mob of `type`
  // that is or is not `frozen`. Two questions, not one: the pile says how much
  // a stack is worth, and the freeze says whether it is collected at all.
  //
  // Riding the pile alone: the final damage a lightning swing takes for the
  // stacks it spends, and the magic attack Glacial Fury pays an ice one.
  // Needing the freeze as well: Freezing Crush's critical damage and the
  // defence Shatter ignores, both per stack, and Storm Magic's final damage,
  // which asks only that the enemy be frozen. `type` is asked for because
  // Shatter's worth is that monster's own defence.
  double BoostForStacks(const AttackOption& attack, int stacks, int type,
                        bool frozen) const;
  // The same against a monster in the queue, read as it stands right now.
  double FreezeBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // What the scar on this monster multiplies the swing by. A monster already
  // scarred pays the whole of Chance Attack's final damage; a fresh one pays
  // the share of the swing's lines that land after the scar is left, since the
  // line that leaves it collects nothing.
  double ScarBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Whether the monster is under any status the fight keeps on it -- the ice
  // a swing left, or a burn.
  bool Afflicted(const QueuedMob& mob) const;
  // What the enemy's own condition adds: whether this one is afflicted, and
  // how many burns stand on the group.
  double ConditionBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // The same, with the affliction and the count answered rather than read off
  // the queue, so a credit can ask what afflicting one more would be worth.
  double ConditionBoostFor(const AttackOption& attack, bool afflicted,
                           int alight) const;
  // Burns standing across the group, which is what the drains count.
  int BurnsAlight() const;
  double BurnLeftOn(const QueuedMob& mob, int slot) const;
  double BurningRate(const CombatParams& params, const QueuedMob& mob,
                     int alight) const;
  // What lighting this swing's burns is worth to the swings after it, beside
  // what BurnCredit already pays for their ticks.
  double BurnStateCredit(const CombatParams& params,
                         const AttackOption& attack) const;
  // Both states in one factor: every reader of either wants both.
  double StateBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Leaves this swing's scar on every one of the front `hit` mobs, after the
  // strike as the burns and the freeze are. Odds rather than a flag: n lines
  // at `scar_chance` apiece leave one with probability 1 - (1 - chance)^n.
  void ApplyScar(const AttackOption& attack, int hit);
  // Counts every scar down, and clears the odds with the clock.
  void RunScar(double dt);
  // Leaves this swing's freeze on every one of the front `hit` mobs. After the
  // strike, as the burns are, so the swing itself is paid for the state the
  // monsters went into it with.
  void ApplyFreeze(const AttackOption& attack, int hit);
  // Counts every frozen monster's seconds down. The monsters thaw; the pile
  // the character holds is spent rather than timed, and is not touched here.
  void RunFreeze(double dt);
  // What the freeze this swing would leave is worth, on top of the damage it
  // lands: the seconds it adds on each monster it reaches, at what a second of
  // being frozen is worth to the swings that will be spent on it. Priced the
  // way a relit burn is -- see BurnCredit -- and worth nothing against a
  // monster already frozen for longer than this swing could come round again.
  double FrozenCredit(const CombatParams& params,
                      const AttackOption& attack) const;
  // What one second of one frozen monster of `type` is worth: what the best
  // swing on offer gains against it per second by its being frozen.
  double FrozenRate(const CombatParams& params, const QueuedMob& mob) const;
  // The mob at the front of the queue, for a reader with no particular enemy
  // in mind. A bare unfrozen mob of type 0 with nothing standing.
  const QueuedMob& FrontMob() const;
  // What the stacks `attack` would LEAVE are worth: everything a deeper pile
  // buys the best swing on offer. Priced into the rate the way a side strike's
  // damage is, because a chooser reading only this swing would never build
  // anything -- an ice swing is worth less than the lightning one it makes
  // room for, right up until the pile is spent.
  double FreezeCredit(const CombatParams& params,
                      const AttackOption& attack) const;
  // Moves the pile on for a landed attack: an ice one leaves a stack per line,
  // a lightning one spends a stack per line. After the strike, so a swing is
  // paid for the stacks it went in holding.
  void CreditFreeze(const CombatParams& params, const AttackOption& attack);
  // Lands one attack on the front of the queue: the first max_enemies mobs
  // each take their own type's damage, one of them also takes the opening hit,
  // and the dead are counted and leave. A swing and a skill on its own clock
  // are the same thing here.
  //
  // Returns the share of the player's pool the chances it rolled put back, 0
  // for every attack that rolls none. Reported rather than paid, because the
  // pool is the caller's business -- exactly as the kills are.
  // `pulses` is how long a HELD swing was held, in pulses; -1 lets the strike
  // decide against the queue in front of it, which is what an attack on its
  // own clock wants. Ignored by every swing that is not held.
  double Strike(const AttackOption& attack, DamageSource source,
                int pulses = -1);
  // How long the orb is worth holding against the queue as it stands: pulses
  // enough to bring every enemy it has locked onto within reach of the strike
  // the hold ends on, and no more. It never re-targets, so a pulse landing
  // after they are dead buys nothing. 0 for a swing that is not held.
  int ChannelPulses(const AttackOption& attack, int hit) const;
  // Seconds one swing of `attack` takes against the queue as it stands: its
  // own for an ordinary swing, and for a held one only as long as it is worth
  // holding.
  double SwingSecondsAgainst(const AttackOption& attack) const;
  // The same for the swing being wound up now, whose length was settled when
  // it was aimed. A hold already running is not re-timed under the player.
  double HeldSeconds(const AttackOption& attack) const;
  // What one pulse of a held swing is worth against `type`, which is the
  // swing's own first block of lines: a hold is that pulse over and over.
  double PulseDamage(const AttackOption& attack, int type) const;
  // What the strike a hold ends on is worth against `type`: everything past
  // the swing's first block of lines, which is that strike and nothing else.
  double FinishDamage(const AttackOption& attack, int type) const;
  // What a hold of `pulses` lands on one mob of `type`: every pulse rolled on
  // its own, and the strike it ends on landed once.
  double ChannelDamage(const AttackOption& attack, int type, int pulses,
                       const Landing& landing);
  // The order a swing that gains as it travels goes through the `hit` mobs it
  // reached, drawn fresh each swing: nothing here has a position, so which
  // enemy an arrow meets first is arbitrary and drawing it keeps the gain from
  // always falling on the same end of the queue. Empty for every other swing,
  // whose order cannot be seen.
  int Reached(const AttackOption& attack) const;
  std::vector<double> ScatterShares(const AttackOption& attack, int hit) const;
  std::vector<int> PierceOrder(const AttackOption& attack, int hit);
  // Indices into the queue of the mobs `attack`'s opening hit picks, empty when
  // it has none. The healthiest of the `hit` mobs the swing reaches, as many of
  // them as lead_enemies: a hit that big is worth least where it overkills, and
  // GMS aims the same shape at the highest-HP target for the same reason.
  std::vector<int> LeadTargets(const AttackOption& attack, int hit) const;
  // What `attack`'s own strikes land on the first `hit` mobs of the queue: its
  // lines, the gain an arrow makes as it pierces, its opening hit and both
  // Final Attack banks.
  double StrikeDamage(const AttackOption& attack, int hit) const;
  // What relighting `burn` on `mob` buys over the next `cadence` seconds, on
  // top of the burning that mob had coming anyway. 0 where it is already
  // carrying a full pile with longer left than the window.
  double BurnCredit(const DotApplication& burn, const QueuedMob& mob,
                    double cadence) const;
  // What the burns `attack` lights are worth, each charged at what relighting
  // it buys rather than in full.
  double BurnDamage(const AttackOption& attack, int hit) const;
  // What the strike `attack` sets off is worth per swing, spread over the
  // swings that go out while it waits. 0 for an attack that sets none off.
  double SideStrikeDamage(const AttackOption& attack) const;
  // What one swing of `attack` would land on the queue as it stands, the
  // opening hit and Final Attack included. An attack with an empowered form is
  // averaged over the run of swings that form takes its place in.
  double SwingDamage(const AttackOption& attack) const;
  // What `attack` really lands this time: its empowered form when `count` has
  // come round, and itself otherwise. Advancing that count is the point of the
  // call, so it is made once per landed attack and nowhere else. Serves both
  // clocks -- the character's swings and a summon's pulses -- with a counter
  // apiece.
  const AttackOption& FormToLand(int& count, const AttackOption& attack);
  // What `attack` lands on the queued mob at `index`: its ordinary damage
  // rolled, and its empowered form on top when that mob's mark has come round.
  // Only that form is the mark's business -- everything else is answered
  // before the swing lands, by FormToLand. Advances the mark, so it is called
  // once per mob per landed swing.
  double DamageToMob(const AttackOption& attack, int index,
                     const Landing& landing);
  // What `attack` lands on one mob of `type` this time: each of its hit blocks
  // at its own roll. The plain expected damage for an attack carrying no
  // blocks, which is every one built by hand rather than by the encounter.
  double RolledDamage(const AttackOption& attack, int type,
                      const Landing& landing);
  // What a bank of Final Attack sources lands on one mob of `type` this time:
  // a roll per source, per line where the source rides them. `expected` is the
  // plain expected damage, landed where there are no sources to roll -- which
  // is every attack built by hand rather than by the encounter.
  double RolledFinalAttack(const std::vector<FinalAttackRoll>& sources,
                           const std::vector<double>& expected, int type,
                           const Landing& landing);
  // Whether the attack at `index` is still winding back up.
  bool Recharging(int index) const;
  // Index into params.attacks of the healing cast to spend this swing on, or
  // -1 for none: the player is not low enough, has nothing to fight, or holds
  // no such skill. A cleared map heals on the beat for free, so a cast there
  // would buy nothing.
  int HealToCast(const CombatParams& params) const;
  // Index into params.attacks of the attack landing the most damage per SECOND
  // on the queue as it stands, or -1 with nothing to hit. Per second and per
  // queue, so a slow animation has to hit proportionally harder, and a wide
  // skill loses its reach bonus once the map thins out. An index, not a
  // pointer: the cooldown it starts is held per attack.
  int BestAttack(const CombatParams& params) const;
  // What this step swings with: the skill already winding up, or a fresh pick
  // from BestAttack. A skill mid-animation is committed to and finishes, so a
  // better one coming free waits its turn -- except the bare poke, which is
  // never committed to, or the fallback would cost the skill it fell back
  // from. A healing cast outranks every attack, but only from the next swing.
  int ChooseAttack(const CombatParams& params) const;
  // The swing that lays a buff nobody is holding, or -1 when every one of them
  // is standing. Outranks BestAttack and is outranked by the heal -- see the
  // note on the definition for why it never asks whether the buff pays.
  int BuffToLay(const CombatParams& params) const;

  // The steps of one Advance, in the order it runs them.
  //
  // Clears every display value and stops the fight, for a step with no
  // encounter to advance.
  void GoIdle();
  // Fills the queue and starts the clocks, on the first step and on a move to
  // another map.
  void BeginMapIfChanged(const CombatParams& params);
  // Tops the roster back up on the beat, and hands back the HP a beat is
  // worth.
  void RespawnBeat(const CombatParams& params, double dt);
  // Lets the engaged mob hit the player, on its own clock.
  void TakeMobHit(const CombatParams& params, double dt);
  // What is left of a hit once the buffs standing have taken their share.
  double BuffDamageTakenFactor(const CombatParams& params) const;
  // Spends one block off whatever shell is standing, if any is, and drops that
  // shell when its last block goes. True when the hit was cancelled whole.
  bool BlockHit(const CombatParams& params);
  // Whether a shell is worth raising right now: on a boss, the moment it
  // comes round; on a map, only once the pool is low enough for its heal to
  // land. True for every buff that is not a shell. See RunBuffs.
  bool ShieldWanted(const CombatParams& params, const BuffOption& buff) const;
  // Whether a passive brings the player back from the hit that just emptied
  // them. Asked only of a player who has hit 0: true when they hold such a
  // skill and its wait has run out, in which case the pool is full again by
  // the time this returns and the wait starts over.
  bool Revive(const CombatParams& params);
  // Puts the reflected share of a hit back into the mob that landed it, and
  // counts it dead if that finishes it. Nothing without a reflection skill.
  void Reflect(const CombatParams& params, double damage_taken);
  // Runs the timed buffs: winds each one's clocks down, puts up any that has
  // come round, and works out which are standing this step. Before the
  // attacks, so a buff that goes up now is one this step's swing has.
  void RunBuffs(const CombatParams& params, double dt);
  // Runs the buffs the party puts up over the character, on their casters'
  // clocks. Apart from RunBuffs because none of these has an attack set: what
  // a party buff grants is taken off the hit, so nothing here touches the
  // damage-table mask, and nothing here costs the character a swing.
  void RunAllyBuffs(const CombatParams& params, double dt);
  // Takes what a landed swing is worth off the wait for each buff's next
  // cast. `weight` is what that swing counted for, the same share
  // CreditSwing uses -- a rapid swing must not pay a whole attack's worth.
  // `lines` is what it landed, for the buffs charged by hits rather than by
  // seconds -- and those count nothing while they are standing.
  void CreditBuffs(const CombatParams& params, double weight, int lines);
  // Puts up every buff the swing at index `swung` lays. Nothing for the
  // swings that lay none, which is all of them bar Puncture.
  void LayBuffs(const CombatParams& params, int swung);
  // The attacks as they stand under the buffs currently up. Every set holds
  // the same attacks in the same order, so an index survives a buff going up
  // or lapsing under it.
  const std::vector<AttackOption>& Attacks(const CombatParams& params) const;
  const std::vector<AttackOption>& AutoAttacks(
      const CombatParams& params) const;
  const std::vector<AttackOption>& TriggeredAttacks(
      const CombatParams& params) const;
  // How many Freeze Stacks the character can hold under those same buffs: the
  // pile is deeper while Glacial Fury stands.
  int FreezeCap(const CombatParams& params) const;
  // Fires the skills that attack on their own clock, before the swing is
  // aimed, so it is aimed at what they leave standing.
  void RunAutoCasts(const CombatParams& params, double dt);
  // Credits a landed swing to the skills clocked by swings rather than by
  // seconds, and fires any whose count has come round. `weight` is what that
  // swing was worth -- a seventh for one that lands seven times as often.
  //
  // The count carries its remainder rather than resetting: a swing worth a
  // seventh must not have six sevenths of it thrown away, or a rapid attack
  // would never set the skill off at all.
  void CreditSwing(const CombatParams& params, double weight);
  // Winds every recharging swing down by dt, before the swing is aimed, so one
  // that comes back this step is available to it.
  void RunCooldowns(const CombatParams& params, double dt);
  // Points the next swing at the queue as it stands, naming it for the charge
  // bar. Returns what it picked, or null with nothing to hit.
  const AttackOption* AimSwing(const CombatParams& params);
  // Charges the swing and lands every one the step comes round for.
  void RunSwing(const CombatParams& params, double dt);
  // One swing landing: the strike and everything that rides on it. Leaves the
  // next swing aimed.
  void LandSwing(const CombatParams& params, const AttackOption& attack);
  // Takes `damage` off `mob` and counts it toward the step's total. Every way
  // the character does damage goes through here.
  void Hurt(QueuedMob& mob, double damage);
  // Refreshes the target and the engaged window for the panel to draw.
  void PublishTarget(const CombatParams& params);
  void MergeEngagedWindow(const CombatParams& params);
  void PublishRoster(const CombatParams& params);

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  // The encounter the queue was filled from. Its type indices only mean
  // anything for that one, so a change here invalidates them.
  std::string encounter_;
  std::vector<QueuedMob> queue_;  // remaining mobs this cycle, front = engaged
  int next_mob_id_ = 0;           // stamped onto each arrival; see MobStatus
  double attack_phase_ = 0.0;     // seconds into the current swing
  double respawn_phase_ = 0.0;    // seconds into the current respawn cycle
  double player_hp_ = 0.0;        // remaining player HP, topped up on a beat
  double hit_phase_ = 0.0;        // seconds into the engaged mob's next hit
  // One buff's clocks, one entry per buff in params.buffs. They keep running
  // across a change of map, unlike the fight's own: a buff belongs to the
  // character rather than to the mobs in front of them.
  struct BuffClock {
    double left = 0.0;           // seconds it still stands
    double cooldown_left = 0.0;  // seconds until it can go up again
    // Lines still to land before a buff charged by hits goes up. Held at its
    // full count for every buff on a clock, which never reads it.
    double charge_left = 0.0;
    // Hits the shell still has in it. Set when the buff goes up and spent a
    // hit at a time; a shell emptied falls at once, whatever is left of its
    // clock. 0 for every buff that is not a shell.
    int blocks_left = 0;
  };
  std::vector<BuffClock> buffs_;
  // Which buffs are standing, as the bitmask CombatParams indexes its damage
  // tables by. Worked out once a step, at the top.
  int buff_mask_ = 0;
  // The same again for the party's buffs, parallel to params.ally_buffs. No
  // mask beside them: a party buff has no damage table, so what is standing is
  // read straight off the seconds left.
  std::vector<BuffClock> ally_buffs_;
  // Seconds left before a passive will revive the player again. Counts down
  // wherever the character is, since what it measures is the pact rather than
  // the fight, and stays at 0 for everyone who holds no such skill.
  double revive_left_ = 0.0;
  // Where one skill on its own clock stands, one entry per cast in
  // params.auto_attacks.
  struct AutoClock {
    // Seconds into its next cast. Runs only while there is something to hit.
    double phase = 0.0;
    // Ticks it has already spent of what one raising of its gating buff is
    // worth. Zeroed while that buff is down, so the count is per window;
    // untouched for a clock with no cap.
    int pulses = 0;
    // Pulses since its last empowered one. The F/P Mage's Creeping Toxin is
    // the one that has any.
    int empowered_count = 0;
  };
  std::vector<AutoClock> auto_clocks_;
  // Seconds into each fountain's next pulse, parallel to params.regen_pulses.
  // Starts at 0, so the first pulse falls one interval in rather than free on
  // the step the fight opened.
  std::vector<double> regen_phase_;
  // Swings credited toward each triggered attack's next cast, parallel to
  // params.triggered_attacks. Fractional, since a swing can be worth less than
  // a whole one.
  std::vector<double> trigger_count_;
  // Where one swing stands, one entry per attack in params.attacks.
  struct AttackClock {
    // Seconds before it can be chosen again. 0 for a swing that is ready,
    // which is all of them for a character holding no cooldown skill.
    double cooldown_left = 0.0;
    // The same for the strike the swing sets off beside itself. Held apart
    // because the swing is still there while its strike is waiting -- a Night
    // Lord keeps throwing Showdown between shurikens.
    double side_cooldown_left = 0.0;
    // Swings landed since its last empowered one. Stays at 0 for every attack
    // that has no empowered form, which is all of them but the Sniper's
    // Piercing Arrow.
    int empowered_count = 0;
  };
  std::vector<AttackClock> attack_clocks_;

  // Freeze Stacks the character is holding. Belongs to them rather than to the
  // map, like the buff clocks and unlike the queue, so it survives walking
  // somewhere else. 0 for everyone who holds none.
  int freeze_stacks_ = 0;

  // Shuffles each batch of arriving mobs so they are fought in mixed order
  // rather than one whole type at a time (see TopUp). Default-seeded, so a sim
  // plays out the same way every run -- which keeps tests reproducible.
  std::mt19937 rng_;

  // Cached render values, refreshed each Advance so accessors need no params.
  std::string target_name_;
  int target_level_ = 0;
  double target_hp_fraction_ = 0.0;
  double attack_fraction_ = 0.0;
  double player_hp_fraction_ = 0.0;
  int player_max_hp_ = 0;
  int player_level_ = 0;
  std::string attack_name_;
  // Reach of the attack the next swing will use -- also the width of the
  // engaged window the UI draws.
  int reach_ = 1;
  // How long that attack's swing takes, for the charge bar to fill against.
  double swing_seconds_ = 0.0;
  // Which attack the aimed swing is, so landing it can start that attack's
  // cooldown -- and, while it is charging, the swing that is committed to.
  // -1 with nothing aimed.
  int aimed_ = -1;
  // Pulses the aimed swing will be held for, settled when it was aimed and
  // kept until it lands: the orb the player is already holding is not re-timed
  // under them as the queue moves. 0 whenever the aimed swing is not held.
  int held_pulses_ = 0;
  std::vector<int64_t> kills_this_step_;
  double damage_this_step_ = 0.0;
  bool respawned_this_step_ = false;
  // The respawn clock as the panel reads it: how far into the cycle, and
  // whether there is a cycle to be into.
  double respawn_fraction_ = 0.0;
  bool respawns_ = false;
  // Whether the lines are being recorded at all, from the params. Off for the
  // sims, which step the fight millions of times and draw none of it.
  bool record_lines_ = false;
  // Stamped onto each landing and never reused within a step, which is as long
  // as anything holds one.
  int next_damage_event_ = 0;
  // The event each queued mob's lines are filed under for the landing being
  // worked out, parallel to the queue, and what is doing the damage.
  std::vector<int> landing_event_;
  DamageSource landing_source_;
  std::vector<DamageLine> damage_lines_this_step_;
  // Where RollFactor writes its per-line shares, reused every roll so a
  // recording fight allocates once rather than once a line.
  std::vector<LineRoll> line_rolls_;
  bool died_this_step_ = false;
  std::vector<EngagedGroup> engaged_groups_;
  std::vector<MobStatus> roster_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
