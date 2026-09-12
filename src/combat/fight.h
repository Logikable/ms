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

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/combat/damage_ledger.h"
#include "src/combat/encounter.h"
#include "src/combat/fight_view.h"

namespace ms {

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

  // What the last Advance published: what that step did, and what the fight
  // looks like now. Refreshed whole each step.
  const FightView& view() const {
    return view_;
  }

  // True while a valid encounter is being fought.
  bool active() const {
    return active_;
  }
  // True when the whole roster is dead and the sim is idling until the next
  // respawn beat.
  bool respawning() const {
    return respawning_;
  }
  // Every line landed during the most recent Advance, in the order they
  // landed. Empty unless the params asked for the record -- see
  // CombatParams::record_damage_lines.
  const std::vector<DamageLine>& damage_lines_this_step() const {
    return ledger_.lines_this_step();
  }
  // The I/L's pile of Freeze Stacks as it stands. 0 for every character who
  // does not hold Freezing Crush.
  int freeze_stacks() const {
    return freeze_stacks_;
  }
  // Seconds before the attack at `index` in params.attacks can be chosen
  // again. 0 for one that is ready, and for an index nothing has swung.
  double cooldown_left(int index) const {
    return index >= 0 && index < static_cast<int>(attack_clocks_.size())
               ? attack_clocks_[index].cooldown_left
               : 0.0;
  }

  // Seconds until the next thing that can change what a swing is worth: the
  // one being wound up landing, or a buff going up or coming down. Nothing
  // else needs a boundary of its own -- a burn is paid pro rata over whatever
  // step it is handed, a summon pulses in a loop, and a recharging attack is
  // only ever read where a swing lands.
  //
  // For a caller stepping the fight as fast as it can be stepped. A step
  // wider than this is still correct; it just prices the swing it covers off
  // the wrong buffs. Infinite when nothing will move on its own.
  double SecondsToNextEvent(const CombatParams& params) const;

  // What the fight has dealt since it began, told apart by the attack that
  // dealt it. Parallel to params.attacks, with a burn credited to the swing
  // that lit it. Damage from a clock of its own -- a summon, a triggered
  // release, a side strike -- is in own_clock_damage() instead, so the two
  // together come to everything that landed.
  const std::vector<double>& damage_by_attack() const {
    return damage_by_attack_;
  }
  double own_clock_damage() const {
    return own_clock_damage_;
  }
  // Swings of each attack the character has landed, parallel to the same list.
  const std::vector<int>& swings_by_attack() const {
    return swings_by_attack_;
  }
  // Which of params.buffs are standing, as the bitmask CombatParams indexes
  // its attack tables by. Refreshed at the top of every step.
  int buff_mask() const {
    return buff_mask_;
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
    // is burning at all. Fractional only while measuring, where a burn that
    // takes hold half the time is half a helping instead of a coin toss.
    double stacks = 0.0;
    SwingRolls rolls;
    // The attack that last lit it, so its ticks are credited home. -1 for a
    // burn nothing lit, which is every slot on an unburned monster.
    int lit_by = -1;
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
    // The stun on it and what carrying it hands the swings that collect. Both
    // 0 for every monster nobody has stunned, which is every monster in the
    // game facing anyone but an I/L holding Jupiter Thunder.
    double stunned_left_seconds = 0.0;
    double stun_lift_pct = 0.0;
    // The angel's mark on it, and what the line that spends it takes. Both 0
    // for every monster nobody has marked, which is every monster in the game
    // facing anyone but a Bishop holding Angel of Balance.
    double marked_left_seconds = 0.0;
    double mark_lift_pct = 0.0;
  };

  // Where the landing on the mob at queue index `index` is filed, scaled by
  // `scale`. The one place the queue and the ledger meet: the ledger knows
  // nothing about the roster, so the monster's id is read off it here.
  Landing LandingAt(int index, double scale) const {
    return ledger_.LandingAt(queue_[index].id, index, scale);
  }

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
  // Puts the healthiest of the roster at the front, so a swing too narrow to
  // reach all of it spends itself on the monsters that will outlast it.
  // Nothing at all on a map -- see CombatParams::focus_healthiest.
  void AimAtHealthiest(const CombatParams& params);
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
  // The same taken in stacks, which is what a scattered swing widens on.
  int BurnStacksAlight() const;
  // Seconds one raising of this buff stands, the burns alight included.
  double BuffWindowSeconds(const BuffOption& buff) const;
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
  // Stacks one strike of `attack` leaves, which is its own count against a
  // lone enemy where the skill states one. See AttackOption::freeze_build.
  int FreezeBuilt(const AttackOption& attack) const;
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
  double HeldPulseDamage(const AttackOption& attack, int type,
                         int pulses) const;
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
  // What a stun somebody left on this monster multiplies the swing by: its
  // lift where the swing collects, and 1 everywhere else.
  double StunBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Leaves this swing's stun on every one of the front `hit` mobs, and counts
  // every stun down. Read exactly as ApplyFreeze and RunFreeze are.
  void ApplyStun(const AttackOption& attack, int hit);
  void RunStun(double dt);
  // What a mark on this monster multiplies the swing by, and it SPENDS the
  // mark: one line of the swing takes the lift and the mark is gone, so a
  // swing of ten lines collects a tenth of it. 1 for a swing that cannot
  // spend one, and for a monster carrying none.
  double SpendMark(const AttackOption& attack, QueuedMob& mob);
  // Leaves this swing's mark on every one of the front `hit` mobs, and counts
  // every mark down. Read exactly as ApplyStun and RunStun are.
  void ApplyMark(const AttackOption& attack, int hit);
  void RunMark(double dt);
  int Reached(const AttackOption& attack) const;
  // Fires whichever strikes of a running barrage have come due, and hands back
  // the wait for each that found nothing standing.
  void RunBarrage(const CombatParams& params, double dt);
  // Enemies the wide half of a swing finds, and what one roll of it is worth.
  // See AttackOption::wide_hit_damage.
  int WideHitTargets(const AttackOption& attack, int hit) const;
  double RolledGroups(const std::vector<HitGroup>& groups,
                      const std::vector<double>& expected, int type,
                      const Landing& landing);
  // How many enemies the once-per-swing Final Attack bank falls on, given the
  // `hit` the swing itself reached. 0 where the swing landed on nothing or the
  // character carries no such source; otherwise the bank's own reach, held to
  // the roster in front of them -- Split Shot's ten behind a Snipe that
  // reached one.
  int PerSwingFinalAttackTargets(const AttackOption& attack, int hit) const;
  // Lines `attack` adds because the character's own swing is on a crowd:
  // lines_per_extra_enemy for every enemy that swing reaches past the first,
  // capped at max_extra_lines. 0 for every attack but Storm of Arrows' rain.
  int ExtraLines(const AttackOption& attack) const;
  // Strikes a scattered swing throws, which the burns already laid may widen.
  int ScatterHits(const AttackOption& attack) const;
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
  // What the load riding `attack` is worth this swing: the whole of it while a
  // charge stands and nothing once it is spent, since one press takes the lot.
  // 0 for every swing no magazine names.
  double LoadedDamage(const AttackOption& attack) const;
  // Strikes of a told-apart swing the press is credited with: all of them
  // where the beat runs out inside the press, and the sustained share where it
  // overhangs. 1 for every swing whose strikes fall together.
  double BarrageStrikes(const AttackOption& attack) const;
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
  // What this landing of `rolls` multiplies its expected damage by. The rolls
  // themselves in a fight being played, and exactly 1 in one being measured:
  // the mean of the roll is 1 by construction, so a measurement that lands it
  // ranks two builds by what separates them rather than by the dice.
  double Roll(const SwingRolls& rolls);
  // How much of something that happens `chance` of the time happened: 1 or 0
  // rolled, and `chance` itself while measuring. The one place the fight's
  // coin tosses are paid as expectations instead.
  double Chance(double chance);
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
  // Whether the attack at `index` has a charge to spend, for one a buff loads.
  // True for every attack no buff loads -- see AttackOption::charges.
  bool Loaded(const CombatParams& params, int index) const;

  // Whether a hold bought out of a bank has a charge in hand. True for every
  // attack that keeps no bank, which is all of them but Divine Punishment.
  bool Charged(const CombatParams& params, int index) const;

  // Pulses the bank will pay for at `index`, or the whole hold where the
  // attack keeps no bank. A press spends a whole charge for a part of one, as
  // GMS spends a light for every second the key is held.
  int ChargedPulses(const CombatParams& params, int index) const;
  // Index into params.attacks of the healing cast to spend this swing on, or
  // -1 for none: the player is not low enough, has nothing to fight, or holds
  // no such skill. A cleared map heals on the beat for free, so a cast there
  // would buy nothing.
  int HealToCast(const CombatParams& params) const;
  // What one swing of `attack` is worth per SECOND on the queue as it stands:
  // its damage and the states it leaves over the seconds it takes. The one
  // measure every pick in here is made on. Takes the option by reference so a
  // swing can be priced under a mask the fight is not standing in yet.
  double SwingRate(const CombatParams& params,
                   const AttackOption& attack) const;

  // A buff window the fight can see coming: how long until it opens, and which
  // buffs would be standing once it had. seconds is infinite when nothing is
  // on its way, and mask is then the mask standing now.
  struct ComingWindow {
    double seconds = 0.0;
    int mask = 0;
  };
  // The next window to open, off the buff clocks. Only buffs on a clock of
  // their own are visible here: one a swing lays or lines charge moves when
  // the fight moves it, and a shell is raised by need rather than by its
  // cooldown, so neither is something to wait for.
  ComingWindow NextWindow(const CombatParams& params) const;
  // Whether waiting for `window` would save the attack at `index` a press it
  // would otherwise not have -- a cooldown that outlasts the wait, or a bank
  // that would not overflow while it sat. False where the press is had either
  // way, which is every swing without a clock of its own.
  bool HoldSaves(const CombatParams& params, int index,
                 const ComingWindow& window) const;
  // Whether saving it pays: what the press gains by landing inside `window`
  // rather than now, against what pushing the whole train of presses back
  // costs. `filler` is the swing that would go out in its place, which is what
  // makes this a comparison rather than a wish.
  bool HoldPays(const CombatParams& params, int index, int filler,
                const ComingWindow& window) const;
  // The best ready swing, skipping every index `held` has set aside. -1 when
  // nothing is on offer.
  int TopAttack(const CombatParams& params,
                const std::vector<bool>& held) const;
  // Index into params.attacks of the attack landing the most damage per SECOND
  // on the queue as it stands, or -1 with nothing to hit. Per second and per
  // queue, so a slow animation has to hit proportionally harder, and a wide
  // skill loses its reach bonus once the map thins out. An index, not a
  // pointer: the cooldown it starts is held per attack.
  //
  // A big move ready just before a buff window is saved for it rather than
  // spent now -- see NextWindow and the note on the definition.
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
  // The form of `buff` worth the most over what is left of this fight, as an
  // index into its stances, or -1 for a buff with one form. See SecondsLeft.
  int StanceToRaise(const CombatParams& params, const BuffOption& buff) const;
  // Seconds this encounter is expected to last, from the HP still standing and
  // the rate the fight has been dealing at. Infinite where the roster refills,
  // a map being an encounter that does not end.
  double SecondsLeft(const CombatParams& params) const;
  // Takes what a landed swing is worth off the wait for each buff's next
  // cast. `weight` is what that swing counted for, the same share
  // CreditSwing uses -- a rapid swing must not pay a whole attack's worth.
  // `lines` is what it landed, for the buffs charged by hits rather than by
  // seconds -- and those count nothing while they are standing.
  void CreditBuffs(const CombatParams& params, double weight, int lines);
  // Puts up every buff the swing at index `swung` lays. Nothing for the
  // swings that lay none, which is all of them bar Puncture.
  // Puts up the buffs the swing at `swung` lays, taking those raised at the
  // cast or those laid by the landing as `on_cast` says. True where any went
  // up, which is the caller's cue to re-read the swing under the new mask.
  bool LayBuffs(const CombatParams& params, int swung, bool on_cast);
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
  // Puts `share` of the HP pool back, clamped at full.
  void RecoverHp(const CombatParams& params, double share);
  // Credits a landed swing to the skills clocked by swings rather than by
  // seconds, and fires any whose count has come round. `weight` is what that
  // swing was worth -- a seventh for one that lands seven times as often.
  //
  // The count carries its remainder rather than resetting: a swing worth a
  // seventh must not have six sevenths of it thrown away, or a rapid attack
  // would never set the skill off at all.
  void CreditSwing(const CombatParams& params, double weight);
  // Credits the enemies defeated since it last ran to the skills clocked by
  // dying, and fires any whose count has come round. Runs beside the other
  // own-clock casts, off a count the reaping keeps, so a defeat is credited
  // however it was dealt -- a swing, a summon, a burn or a reflected blow.
  //
  // What one of these kills charges the next release rather than this one: the
  // pending count is taken before anything strikes, so a wide cast on a dying
  // crowd cannot set itself off again and again in the one step.
  void CreditKills(const CombatParams& params);
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
  // Refreshes the player's pool and the respawn clock for the panel to draw.
  void PublishPlayer(const CombatParams& params);
  // Refreshes the target and the engaged window for the panel to draw.
  void PublishTarget(const CombatParams& params);
  // Puts the buff the character is casting on the charge bar and fills it
  // over that animation, the swing clock being in debt for it. Drops the
  // casts already played, and says whether one took the bar.
  bool PublishCast();
  void MergeEngagedWindow(const CombatParams& params);
  void PublishRoster(const CombatParams& params);

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  // Whether this fight is being measured rather than played, taken off the
  // params at the top of every step. See CombatParams::measuring.
  bool measuring_ = false;
  // The encounter the queue was filled from. Its type indices only mean
  // anything for that one, so a change here invalidates them.
  std::string encounter_;
  std::vector<QueuedMob> queue_;  // remaining mobs this cycle, front = engaged
  int next_mob_id_ = 0;           // stamped onto each arrival; see MobStatus
  double attack_phase_ = 0.0;     // seconds into the current swing
  // One buff animation the character owes. Raising a buff takes its cast off
  // the swing clock, so a handful going up at once leaves that clock in debt
  // and the character casting rather than swinging for as long as it takes.
  // These say what they are casting while it lasts.
  struct OwedCast {
    std::string name;
    // The swing phase this animation is finished at: the clock climbing back
    // to it is what ends the cast.
    double done_at = 0.0;
    double seconds = 0.0;
  };
  // Newest last, which is also the order they are played out in: each one is
  // finished by the swing clock climbing back to the mark it was raised at,
  // and the last raised reaches its mark first. Cleared with the clock
  // itself, the casts being owed against it.
  std::vector<OwedCast> owed_casts_;
  double respawn_phase_ = 0.0;  // seconds into the current respawn cycle
  double player_hp_ = 0.0;      // remaining player HP, topped up on a beat
  double hit_phase_ = 0.0;      // seconds into the engaged mob's next hit
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
    // Which form went up, as an index into BuffOption::stances, or -1 for a
    // buff with one form. Chosen at the cast and left alone while it stands:
    // GMS took away the key that swapped a summoned sword between its forms.
    int stance = -1;
    // Seconds into the duty cycle of a buff that grants in bursts, counted
    // from the raise and wrapping at its interval. Held at 0 for every buff
    // that grants for the whole of its window, which never reads it.
    double duty_phase = 0.0;
  };
  std::vector<BuffClock> buffs_;
  // Which buffs are STANDING, and which of those are granting right now. The
  // two differ only while a duty-cycled buff is in the gap between grants: the
  // angel goes on striking there, so its pulse is gated on the first mask
  // while the damage tables are picked with the second. Worked out once a
  // step, at the top. See BuffOption::duty_seconds.
  int buff_mask_ = 0;
  int lever_mask_ = 0;
  // What the character has dealt this encounter and how long they have been
  // dealing it, for the rate SecondsLeft divides remaining HP by. Map-scoped:
  // another encounter's damage says nothing about how long this one has left.
  double damage_dealt_ = 0.0;
  double fight_seconds_ = 0.0;
  // What each attack has dealt and how often it has been swung, parallel to
  // params.attacks, and what everything on a clock of its own has dealt. These
  // run for the life of the fight rather than per encounter: what reads them
  // is a measurement, which fights one. See damage_by_attack().
  std::vector<double> damage_by_attack_;
  std::vector<int> swings_by_attack_;
  double own_clock_damage_ = 0.0;
  // Which attack the damage now landing belongs to, or -1 for damage on a
  // clock of its own. Set around each strike, which is the only place that
  // knows.
  int attributing_ = -1;
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
  // Defeats credited toward each triggered attack's next cast, parallel to the
  // same list. Whole, since a monster either died or did not.
  std::vector<int> kill_count_;
  // Defeats since CreditKills last ran, counted by Reap and Reflect wherever
  // they clear a body. Held rather than read off view_.kills_this_step because
  // that is zeroed at the top of every step, and the count belongs to the
  // skill's clock rather than to the step.
  int kills_pending_ = 0;
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
    // Swings of it the buff that loads it has left to pay for. Stays at 0 for
    // every attack no buff loads -- and reads 0 for a loaded one whose buff is
    // down, which is what keeps it off the list of swings on offer.
    int charges_left = 0;
    // Seconds accrued toward the next charge a load prepares for itself. Held
    // at 0 while the bank is already at its own cap, so a raising of the buff
    // never has its clock running under it. 0 for every other attack.
    double load_phase = 0.0;
    // Charges banked for a hold bought out of a bank rather than out of a
    // cooldown, fractional while the next one fills. Starts FULL, the way a
    // cooldown starts ready: a player walks into the fight with what the wait
    // before it prepared. Stays at 0 for every other attack.
    double hold_charges = 0.0;
  };
  std::vector<AttackClock> attack_clocks_;
  // What this step is worth in seconds, for the one reader that has to line
  // the swing's phase up against a clock already wound down -- see HoldSaves.
  double step_seconds_ = 0.0;

  // Freeze Stacks the character is holding. Belongs to them rather than to the
  // map, like the buff clocks and unlike the queue, so it survives walking
  // somewhere else. 0 for everyone who holds none.
  int freeze_stacks_ = 0;
  // A swing told apart into strikes that land on a beat rather than together:
  // which attack it is, how many of its strikes are still to come, and how long
  // until the next. The player is free while it runs, as GMS frees them the
  // moment the orb is loosed -- what the cast bought is the barrage, not the
  // time it takes.
  //
  // One at a time: a second cast of the same skill is a cooldown away.
  struct Barrage {
    int attack = -1;
    int strikes_left = 0;
    double next_seconds = 0.0;
  };
  Barrage barrage_;

  // Shuffles each batch of arriving mobs so they are fought in mixed order
  // rather than one whole type at a time (see TopUp). Default-seeded, so a sim
  // plays out the same way every run -- which keeps tests reproducible.
  std::mt19937 rng_;

  // What the swing being charged is, refreshed each Advance. The panel reads
  // its name and its progress off the view; these are the fight's own working
  // copies, which the aim and the strike both need.
  //
  // Reach of that attack -- also the width of the engaged window the UI draws.
  int reach_ = 1;
  // How long its swing takes, for the charge bar to fill against.
  double swing_seconds_ = 0.0;
  // Which attack the aimed swing is, so landing it can start that attack's
  // cooldown -- and, while it is charging, the swing that is committed to.
  // -1 with nothing aimed.
  int aimed_ = -1;
  // Enemies the character's own swing is reaching, measured at the top of the
  // step -- so a rain on its own clock and the swing that set it off agree
  // about the crowd. The aim is a step old, the swing being chosen at the end
  // of the last one, which is near enough for a count that moves with the
  // queue. 0 with nothing aimed. See BuffPulse::lines_per_extra_enemy.
  int swing_enemies_ = 0;
  // Pulses the aimed swing will be held for, settled when it was aimed and
  // kept until it lands: the orb the player is already holding is not re-timed
  // under them as the queue moves. 0 whenever the aimed swing is not held.
  int held_pulses_ = 0;
  // The level the last step ran at, so the next one can catch a level-up --
  // see Advance.
  int player_level_ = 0;
  // Where every line this fight lands is filed. See DamageLedger.
  DamageLedger ledger_;
  // What the fight publishes for a reader. See FightView.
  FightView view_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
