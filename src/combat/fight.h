/* The live fight: the player auto-attacking a map's mobs while the front mob
 * hits back. A respawn beat tops the roster up, leaving what still stands.
 *
 * The player's HP lives here because it never outlives a fight. One engine
 * behind both halves of combat: the kills it reports are what the reward
 * layer pays for, and the same step drives the panel.
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
  // Advances the fight by elapsed_seconds. A gap larger than one swing is
  // clamped, so a stall costs progress rather than paying out a burst.
  void Advance(const CombatParams& params, double elapsed_seconds);

  // Takes the monsters `hp_by_id` names down to the share it gives and reaps
  // what that killed. Downward only, and a monster it does not name is left
  // alone: a local copy of a shared roster may run ahead, never behind.
  void ClampRoster(const CombatParams& params,
                   const std::map<int, double>& hp_by_id);

  // What the last Advance published. Refreshed whole each step.
  const FightView& view() const {
    return view_;
  }

  // True while a valid encounter is being fought.
  bool active() const {
    return active_;
  }
  // True while the roster is dead and the sim idles until the next beat.
  bool respawning() const {
    return respawning_;
  }
  // Every line landed during the last Advance, in order. Empty unless
  // CombatParams::record_damage_lines asked for the record.
  const std::vector<DamageLine>& damage_lines_this_step() const {
    return ledger_.lines_this_step();
  }
  // The skill a line's `credit` names.
  const std::string& damage_credit_name(int credit) const {
    return ledger_.credit_name(credit);
  }
  // The I/L's pile of Freeze Stacks; 0 without Freezing Crush.
  int freeze_stacks() const {
    return freeze_stacks_;
  }
  // Seconds before the attack at `index` can be chosen again; 0 when ready.
  double cooldown_left(int index) const {
    return index >= 0 && index < static_cast<int>(attack_clocks_.size())
               ? attack_clocks_[index].cooldown_left
               : 0.0;
  }

  // Seconds until the next thing that can change what a swing is worth: the
  // swing landing, or a buff going up or coming down. A wider step is still
  // correct, it just prices its swing off the wrong buffs. Infinite when
  // nothing will move on its own.
  double SecondsToNextEvent(const CombatParams& params) const;

  // What each of params.attacks came to since the fight began, a burn credited
  // to the swing that lit it. Damage on a clock of its own is in
  // own_clock_damage() instead; the two together are everything that landed.
  const std::vector<AttackTally>& by_attack() const {
    return by_attack_;
  }
  double own_clock_damage() const {
    return own_clock_damage_;
  }
  // own_clock_damage() told apart by source. An index is looked up in the
  // list its origin belongs to: kOwnClock in params.auto_attacks, kSwingClock
  // and kKillClock in params.triggered_attacks, the rest in params.attacks.
  const std::map<DamageSource, double>& own_clock_by_source() const {
    return own_clock_by_source_;
  }
  // Which of params.buffs stand, as the mask CombatParams indexes its attack
  // tables by. Refreshed at the top of every step.
  int buff_mask() const {
    return buff_mask_;
  }

 private:
  // One burn on one monster. The damage is settled when the burn lands and
  // never asked again -- see the Dot message.
  struct MobDot {
    double left_seconds = 0.0;
    double phase = 0.0;
    double interval_seconds = 0.0;
    double damage = 0.0;
    // Helpings carried, each ticking for the whole damage. 1 for every burn
    // but a Night Lord's poison. Fractional only while measuring.
    double stacks = 0.0;
    SwingRolls rolls;
    // The attack that last lit it, so its ticks are credited home; -1 for
    // an unlit slot.
    int lit_by = -1;
    // What a damage breakdown files its ticks under. See DamageLedger::Credit.
    int credit = -1;
  };

  // A mob in the queue: its type (an index into params.types) and its HP.
  struct QueuedMob {
    int type = 0;
    double hp = 0.0;
    // Carried because GMS names a wound's target by MAX HP: a boss part worn
    // down is still the biggest thing on the map. See ApplyWound.
    double max_hp = 0.0;
    // Never reused within an encounter, so a caller's per-mob bar cannot be
    // handed the mob that replaced the one it was drawing.
    int id = 0;
    // Strikes taken from a marking swing since its last mark went off. The
    // mark rides the mob, so one that dies takes its count to the grave.
    int brand = 0;
    // One slot per burn source the character can leave; empty for everyone
    // but the F/P Arch Mage.
    std::vector<MobDot> dots;
    // Seconds it stays frozen, set by the ice swings that reach it. What
    // being frozen is worth is the character's -- see BoostForStacks.
    double frozen_left_seconds = 0.0;
    // The scar Scarring Sword leaves: how long it stands, and the odds it is
    // there at all. Kept as odds rather than rolled, as every other chance in
    // this fight is -- a monster half-likely to be scarred takes half.
    double scarred_left_seconds = 0.0;
    double scar_odds = 0.0;
    // The stun on it and what carrying it hands the swings that collect.
    double stunned_left_seconds = 0.0;
    double stun_lift_pct = 0.0;
    // The angel's mark on it, and what the line that spends it takes.
    double marked_left_seconds = 0.0;
    double mark_lift_pct = 0.0;
  };

  // The one wound the fight keeps, held by mob id. One at a time because
  // that IS the rule: GMS's wound moves to whoever was hit last.
  struct WoundState {
    int mob_id = -1;
    int stacks = 0;
    double left_seconds = 0.0;
  };

  // Where the landing on the mob at `index` is filed, scaled by `scale`. The
  // one place the queue and the ledger meet.
  Landing LandingAt(int index, double scale) const {
    return ledger_.LandingAt(queue_[index].id, index, scale);
  }

  // Counts and drops every mob at or below no HP. Shared by the swing and
  // the burn, which kill alike.
  void Reap();
  // Leaves each of `attack`'s burns on everything it reached. A mob already
  // burning has its clock restarted and its damage taken fresh; whether a
  // helping piles on top is the burn's own business.
  void ApplyDots(const AttackOption& attack, int hit);
  // Runs every burn forward by dt, landing the ticks that come due.
  void RunDots(double dt);

  // Runs every fountain forward by dt, pouring the pulses that come due.
  // Each fills to the pool and no further, with or without mobs standing.
  void RunRegen(const CombatParams& params, double dt);
  // The heal that answers nearly dying: arms itself under the threshold,
  // pours for its window and then waits out its cooldown. See EmergencyHeal.
  void RunEmergencyHeal(const CombatParams& params, double dt);

  // Adds what each type is missing: a respawn puts new monsters on the map,
  // it does not heal the one being fought. Leaves the swing clock alone.
  void TopUp(const CombatParams& params);
  // Puts the healthiest at the front, so a narrow swing spends itself on
  // what will outlast it. Nothing on a map -- see focus_healthiest.
  void AimAtHealthiest(const CombatParams& params);
  // The share of the pool a landed `attack` puts back through its rolled
  // chances. Returned rather than paid: a strike knows nothing about the
  // pool. See Proc.
  double RollProcs(const AttackOption& attack, int hit);
  // What a pile `stacks` deep multiplies `attack` by against a mob of `type`
  // that is or is not `frozen`. Two questions: the pile says what a stack is
  // worth, the freeze says whether it is collected at all. `type` is asked
  // because Shatter's worth is that monster's own defence.
  double BoostForStacks(const AttackOption& attack, int stacks, int type,
                        bool frozen) const;
  // The same against a monster in the queue, read as it stands right now.
  double FreezeBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // What the scar on this monster multiplies the swing by. An already
  // scarred mob pays the whole of Chance Attack; a fresh one pays only the
  // share of lines landing after the scar, the line that leaves it earning
  // nothing.
  double ScarBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Whether any status the fight keeps stands on it -- ice or a burn.
  bool Afflicted(const QueuedMob& mob) const;
  // What the enemy's condition adds: afflicted or not, and the burns alight
  // across the group.
  double ConditionBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // The same with both answered rather than read, so a credit can ask what
  // afflicting one more would be worth.
  double ConditionBoostFor(const AttackOption& attack, bool afflicted,
                           int alight) const;
  // Burns standing across the group, which is what the drains count.
  int BurnsAlight() const;
  // The same in stacks, which is what a scattered swing widens on.
  int BurnStacksAlight() const;
  // Seconds one raising of this buff stands, the burns alight included.
  double BuffWindowSeconds(const BuffOption& buff) const;
  // Rolls for every buff a landed swing can raise -- see
  // BuffOption::raise_chance. `afflicted` is whether the swing found an
  // enemy already carrying a status, which one of them asks for.
  void RaiseRolledBuffs(const CombatParams& params, bool afflicted);
  // One such buff, whose windows are params.buffs[first, end): fills the
  // first free one on a successful roll and keeps the group ordered.
  void RaiseOneWindow(const CombatParams& params, int first, int end);
  // Orders every rolled buff's windows longest-lived first, so the ones
  // standing are always the first of their group.
  void CompactRolledWindows(const CombatParams& params);
  double BurnLeftOn(const QueuedMob& mob, int slot) const;
  double BurningRate(const CombatParams& params, const QueuedMob& mob,
                     int alight) const;
  // What lighting this swing's burns is worth to the swings after it, beside
  // the ticks BurnCredit already pays for.
  double BurnStateCredit(const CombatParams& params,
                         const AttackOption& attack) const;
  // Both states in one factor: every reader of either wants both.
  double StateBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Leaves this swing's scar on the front `hit` mobs, after the strike as
  // the burns are. Odds, not a flag: n lines leave one with probability
  // 1 - (1 - chance)^n.
  void ApplyScar(const AttackOption& attack, int hit);
  // Counts every scar down, and clears the odds with the clock.
  void RunScar(double dt);
  // Leaves this swing's freeze on the front `hit` mobs. After the strike, so
  // the swing is paid for the state the monsters went into it with.
  void ApplyFreeze(const AttackOption& attack, int hit);
  // Thaws the monsters. The character's own pile is spent, not timed, and is
  // untouched here.
  void RunFreeze(double dt);
  // What the freeze this swing would leave is worth on top of its damage:
  // the seconds it adds, at FrozenRate. Priced as a relit burn is (see
  // BurnCredit), and worth nothing against a mob already frozen that long.
  double FrozenCredit(const CombatParams& params,
                      const AttackOption& attack) const;
  // What the best swing on offer gains per second against `mob` by its being
  // frozen.
  double FrozenRate(const CombatParams& params, const QueuedMob& mob) const;
  // The front of the queue, for a reader with no particular enemy in mind.
  // A bare unfrozen mob of type 0 when nothing stands.
  const QueuedMob& FrontMob() const;
  // Stacks one strike of `attack` leaves, which is its own count against a
  // lone enemy where the skill states one. See AttackOption::freeze_build.
  int FreezeBuilt(const AttackOption& attack) const;
  // What the stacks `attack` would LEAVE are worth to the best swing on
  // offer. Priced into the rate as a side strike's damage is, or a chooser
  // reading only this swing would never build a pile at all.
  double FreezeCredit(const CombatParams& params,
                      const AttackOption& attack) const;
  // Moves the pile on: ice leaves a stack per line, lightning spends one.
  // After the strike, so a swing is paid for the stacks it went in holding.
  void CreditFreeze(const CombatParams& params, const AttackOption& attack);
  // Lands one attack on the front of the queue and reaps what it killed. A
  // swing and a skill on its own clock are the same thing here.
  //
  // Returns the share of the pool its rolled chances put back, reported
  // rather than paid as the kills are. `pulses` is how long a HELD swing was
  // held; -1 lets the strike decide against the queue in front of it.
  // The banks that ride a strike rather than being it: the lead hit, both
  // final-attack banks and the wide hit. Rolled after the strike proper, so
  // they find the marks it spent.
  void StrikeRiders(const AttackOption& attack, int hit,
                    const std::vector<int>& lead);
  // Writes every state the strike leaves on what it reached. Before the dead
  // are cleared, so the indices are still the ones the marks are written to.
  void ApplyStates(const AttackOption& attack, int hit);
  double Strike(const AttackOption& attack, DamageSource source,
                int pulses = -1);
  // Pulses worth holding for: enough to bring every enemy it locked onto
  // within reach of the closing strike, no more. It never re-targets, so a
  // pulse landing after they are dead buys nothing. 0 when not held.
  int ChannelPulses(const AttackOption& attack, int hit) const;
  // Seconds one swing takes against the queue: its own, or for a held swing
  // only as long as it is worth holding.
  double SwingSecondsAgainst(const AttackOption& attack) const;
  // The same for the swing winding up now, settled when it was aimed: a hold
  // already running is not re-timed under the player.
  double HeldSeconds(const AttackOption& attack) const;
  // One pulse of a held swing against `type` -- the swing's first block of
  // lines, which a hold repeats.
  double PulseDamage(const AttackOption& attack, int type) const;
  // The strike a hold ends on: everything past the first block of lines.
  double HeldPulseDamage(const AttackOption& attack, int type,
                         int pulses) const;
  double FinishDamage(const AttackOption& attack, int type) const;
  // A hold of `pulses` on one mob: every pulse rolled on its own, then the
  // closing strike once.
  double ChannelDamage(const AttackOption& attack, int type, int pulses,
                       const Landing& landing);
  // What a stun on this monster multiplies the swing by: its lift where the
  // swing collects, 1 everywhere else.
  double StunBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Leaves this swing's stun and counts the stuns down, as ApplyFreeze and
  // RunFreeze do.
  void ApplyStun(const AttackOption& attack, int hit);
  void RunStun(double dt);
  // What a mark multiplies the swing by, and it SPENDS the mark: one line
  // takes the lift and it is gone, so a swing of ten lines collects a tenth.
  double SpendMark(const AttackOption& attack, QueuedMob& mob);
  // Leaves this swing's mark and counts the marks down.
  void ApplyMark(const AttackOption& attack, int hit);
  void RunMark(double dt);
  // Leaves this swing's wound on the reached enemy with the highest MAX HP.
  // ONE monster carries a wound at a time, so a fresh one takes it off
  // whoever had it -- see Wound.
  void ApplyWound(const AttackOption& attack, int hit);
  void RunWound(double dt);
  // Whether a wound on a standing monster is as deep as `attack`'s heavier
  // form asks. False for every swing but Trickblade's, which has no form.
  bool WoundFull(const AttackOption& attack) const;

  int Reached(const AttackOption& attack) const;
  // Fires the barrage strikes that have come due.
  void RunBarrage(const CombatParams& params, double dt);
  void RunBarrageOf(const CombatParams& params, int index, double dt);
  // Enemies the wide half of a swing finds. See wide_hit_damage.
  int WideHitTargets(const AttackOption& attack, int hit) const;
  double RolledGroups(const std::vector<HitGroup>& groups,
                      const std::vector<double>& expected, int type,
                      const Landing& landing);
  // Enemies the once-per-swing Final Attack bank falls on: its own reach
  // held to the roster standing, which is Split Shot's ten behind a Snipe
  // that reached one. 0 without such a source.
  int PerSwingFinalAttackTargets(const AttackOption& attack, int hit) const;
  // Lines added because the character's swing is on a crowd:
  // lines_per_extra_enemy per enemy past the first, capped at
  // max_extra_lines.
  int ExtraLines(const AttackOption& attack) const;
  // Strikes a scattered swing throws, which the burns already laid may widen.
  int ScatterHits(const AttackOption& attack) const;
  std::vector<double> ScatterShares(const AttackOption& attack, int hit) const;
  std::vector<int> PierceOrder(const AttackOption& attack, int hit);
  // Queue indices the opening hit picks: the healthiest `lead_enemies` of
  // the mobs reached, since a hit that big is worth least where it overkills
  // -- which is why GMS aims it at the highest-HP target too.
  std::vector<int> LeadTargets(const AttackOption& attack, int hit) const;
  // What `attack`'s own strikes land on the front `hit` mobs: its lines, a
  // pierce gain, its opening hit and both Final Attack banks.
  double StrikeDamage(const AttackOption& attack, int hit) const;
  // What relighting `burn` buys over `cadence` seconds, above the burning
  // that mob had coming anyway.
  double BurnCredit(const DotApplication& burn, const QueuedMob& mob,
                    double cadence) const;
  // What `attack`'s burns are worth, each charged at what relighting buys
  // rather than in full.
  double BurnDamage(const AttackOption& attack, int hit) const;
  // What the strike `attack` sets off is worth per swing, spread over the
  // swings that go out while it waits.
  double SideStrikeDamage(const AttackOption& attack) const;
  // What the load riding `attack` is worth this swing: the whole of it while
  // a charge stands, nothing once spent, since one press takes the lot.
  double LoadedDamage(const AttackOption& attack) const;
  // Strikes of a told-apart swing the press is credited with: those landing
  // before it can go out again -- its cooldown, or its own press without
  // one. 1 for a swing whose strikes fall together.
  double BarrageStrikes(const AttackOption& attack) const;
  // What one swing would land on the queue as it stands. An empowered form
  // is averaged over the run of swings it takes its place in.
  double SwingDamage(const AttackOption& attack) const;
  // The form `attack` really lands this time: its empowered one when `count`
  // has come round. ADVANCES that count, so it is called once per landed
  // attack and nowhere else.
  const AttackOption& FormToLand(int& count, const AttackOption& attack);
  // What `attack` lands on the mob at `index`: its rolled damage, plus its
  // empowered form when that mob's mark has come round. ADVANCES the mark, so
  // it is called once per mob per landed swing.
  double DamageToMob(const AttackOption& attack, int index,
                     const Landing& landing);
  // What this landing multiplies its expected damage by: the rolls in a
  // played fight, exactly 1 in a measured one. The roll's mean is 1, so a
  // measurement ranks builds by what separates them rather than by dice.
  double Roll(const SwingRolls& rolls);
  // How much of a `chance` event happened: 1 or 0 rolled, `chance` itself
  // while measuring.
  double Chance(double chance);
  // What `attack` lands on one mob of `type`: each hit block at its own
  // roll, or the plain expected damage for an attack carrying no blocks.
  double RolledDamage(const AttackOption& attack, int type,
                      const Landing& landing);
  // What a bank of Final Attack sources lands on one mob: a roll per source,
  // per line where it rides them. `expected` is landed where there is nothing
  // to roll.
  double RolledFinalAttack(const std::vector<FinalAttackRoll>& sources,
                           const std::vector<double>& expected, int type,
                           const Landing& landing);
  // Whether the attack at `index` is still winding back up.
  bool Recharging(int index) const;
  // Whether the attack at `index` has a charge to spend. True for every
  // attack no buff loads -- see AttackOption::charges.
  bool Loaded(const CombatParams& params, int index) const;

  // Whether a hold bought out of a bank has a charge in hand. True for every
  // attack that keeps no bank.
  bool Charged(const CombatParams& params, int index) const;

  // Pulses the bank will pay for, or the whole hold where there is no bank.
  // A press spends a whole charge for part of one, as GMS spends a light for
  // every second held.
  int ChargedPulses(const CombatParams& params, int index) const;
  // The healing cast to spend this swing on, or -1: not low enough, nothing
  // to fight, or no such skill. A cleared map heals free on the beat.
  int HealToCast(const CombatParams& params) const;
  // Whether the swing at `index` could go out on this press: a swing rather
  // than a clock, off its cooldown, loaded, and charged.
  bool OnOffer(const CombatParams& params, int index) const;
  // What one swing is worth per SECOND on the queue as it stands -- damage
  // and the states it leaves, over the seconds it takes. The one measure
  // every pick in here is made on. By reference, so a swing can be priced
  // under a mask the fight is not standing in yet.
  double SwingRate(const CombatParams& params,
                   const AttackOption& attack) const;

  // A buff window the fight can see coming. seconds is infinite when nothing
  // is on its way, and mask is then the mask standing now.
  struct ComingWindow {
    double seconds = 0.0;
    int mask = 0;
  };
  // The next window to open, off the buff clocks. Only buffs on a clock of
  // their own are visible: one a swing lays, one lines charge and a shell
  // raised by need are not things to wait for.
  ComingWindow NextWindow(const CombatParams& params) const;
  // Whether waiting for `window` saves the attack at `index` a press it
  // would otherwise not have: a cooldown outlasting the wait, or a bank that
  // would not overflow while it sat.
  bool HoldSaves(const CombatParams& params, int index,
                 const ComingWindow& window) const;
  // Whether saving it pays: what the press gains inside `window` against
  // what pushing the train of presses back costs. `filler` is the swing that
  // would go out in its place, which makes this a comparison, not a wish.
  bool HoldPays(const CombatParams& params, int index, int filler,
                const ComingWindow& window) const;
  // The best ready swing, skipping every index `held` has set aside. -1 when
  // nothing is on offer.
  int TopAttack(const CombatParams& params,
                const std::vector<bool>& held) const;
  // The attack landing the most damage per SECOND on the queue as it stands,
  // or -1 with nothing to hit. Per second and per queue, so a slow animation
  // must hit proportionally harder and a wide skill loses its reach bonus on
  // a thin map. A big move ready just before a buff window is saved for it.
  int BestAttack(const CombatParams& params) const;
  // What this step swings with: the skill already winding up, or a fresh
  // pick. A skill mid-animation finishes first -- except the bare poke, or
  // the fallback would cost the skill it fell back from.
  int ChooseAttack(const CombatParams& params) const;
  // The swing that lays a buff nobody is holding, or -1. Outranks
  // BestAttack, outranked by the heal.
  int BuffToLay(const CombatParams& params) const;

  // The steps of one Advance, in the order it runs them.
  void GoIdle();
  void BeginMapIfChanged(const CombatParams& params);
  // Tops the roster up on the beat, and hands back the HP a beat is worth.
  void RespawnBeat(const CombatParams& params, double dt);
  void TakeMobHit(const CombatParams& params, double dt);
  // What is left of a hit once the buffs standing have taken their share.
  double BuffDamageTakenFactor(const CombatParams& params) const;
  // Spends a block off the standing shell and drops it when the last goes.
  // True when the hit was cancelled whole.
  bool BlockHit(const CombatParams& params);
  // Whether a shell is worth raising now: on a boss the moment it comes
  // round, on a map only once the pool is low enough for its heal to land.
  bool ShieldWanted(const CombatParams& params, const BuffOption& buff) const;
  // Whether a passive brings the player back from the hit that emptied them.
  // Asked only at 0 HP; a true answer has already refilled the pool.
  bool Revive(const CombatParams& params);
  // Puts the reflected share of a hit back into the mob that landed it.
  void Reflect(const CombatParams& params, double damage_taken);
  // Winds the buff clocks down, raises what has come round, and works out
  // the mask. Before the attacks, so a buff raised now is one this swing has.
  void RunBuffs(const CombatParams& params, double dt);
  // The stance of `buff` worth most over what is left of this fight, or -1
  // for a buff with one form. See SecondsLeft.
  int StanceToRaise(const CombatParams& params, const BuffOption& buff) const;
  // Seconds this encounter is expected to last, from the HP standing and the
  // rate so far. Infinite on a map, which does not end.
  double SecondsLeft(const CombatParams& params) const;
  // Takes a landed swing off the wait for each buff's next cast. `weight` is
  // the share CreditSwing uses, so a rapid swing does not pay a whole
  // attack's worth; `lines` feeds the buffs charged by hits, which count
  // nothing while standing.
  void CreditBuffs(const CombatParams& params, double weight, int lines);
  // Raises the buffs the swing at `swung` lays -- at the cast or on the
  // landing as `on_cast` says. True when any went up, the caller's cue to
  // re-read the swing under the new mask.
  bool LayBuffs(const CombatParams& params, int swung, bool on_cast);
  // The attacks as they stand under the buffs up. Every set holds the same
  // attacks in the same order, so an index survives a buff lapsing.
  const std::vector<AttackOption>& Attacks(const CombatParams& params) const;
  const std::vector<AttackOption>& AutoAttacks(
      const CombatParams& params) const;
  const std::vector<AttackOption>& TriggeredAttacks(
      const CombatParams& params) const;
  // The Freeze Stack cap under those buffs; deeper while Glacial Fury
  // stands.
  int FreezeCap(const CombatParams& params) const;
  // Fires the own-clock skills before the swing is aimed, so it is aimed at
  // what they leave standing.
  void RunAutoCasts(const CombatParams& params, double dt);
  // Puts `share` of the HP pool back, clamped at full.
  void RecoverHp(const CombatParams& params, double share);
  // Credits a landed swing to the skills clocked by swings, and fires what
  // has come round. `weight` is what the swing was worth -- a seventh for one
  // landing seven times as often. The count carries its remainder, or a rapid
  // attack would never set the skill off at all.
  void CreditSwing(const CombatParams& params, double weight);
  // Credits defeats to the skills clocked by dying and fires what has come
  // round. Off a count the reaping keeps, so a defeat counts however it was
  // dealt. The pending count is taken before anything strikes, so a wide cast
  // on a dying crowd cannot set itself off again and again in one step.
  void CreditKills(const CombatParams& params);
  // Clears what the step reports and opens the ledger's own. Everything here
  // describes THIS step, so none of it survives the last one.
  void OpenStep(const CombatParams& params);
  // Makes room for attacks that appeared since the last step -- a buff going
  // up loads its magazine's swing and needs that clock to exist. A bank
  // starts full, as a cooldown starts ready.
  void GrowForAttacks(const CombatParams& params);
  // Winds the cooldowns down before the swing is aimed, so one coming back
  // this step is available to it.
  void RunCooldowns(const CombatParams& params, double dt);
  // Points the next swing at the queue and names it for the charge bar.
  // Null with nothing to hit.
  const AttackOption* AimSwing(const CombatParams& params);
  // Charges the swing and lands every one the step comes round for.
  void RunSwing(const CombatParams& params, double dt);
  // One swing landing: the strike and everything riding on it. Leaves the
  // next swing aimed.
  void LandSwing(const CombatParams& params, const AttackOption& attack);
  // The side strike and the load the press sets off, both read off the AIMED
  // attack rather than the form that stood in for it: they belong to the
  // skill. Lands on what the swing left standing.
  void StrikeExtras(const AttackOption& cast, int swung);
  // The share of the HP pool `landed` recovers. Recovery rides the hit, so a
  // cast earns none, and a hold pays per pulse rather than per press.
  double SwingRecovery(const CombatParams& params, const AttackOption& landed,
                       double proc_recovered) const;
  // What the swing costs its own clocks: the cooldown, a charge, and the
  // lights a hold burned through.
  void SpendSwingClocks(const AttackOption& attack, int swung);
  // Takes `damage` off `mob` and counts it. Every way the character does
  // damage goes through here.
  void Hurt(QueuedMob& mob, double damage);
  void PublishPlayer(const CombatParams& params);
  void PublishTarget(const CombatParams& params);
  // Puts the buff being cast on the charge bar and fills it over that
  // animation. True when a cast took the bar.
  bool PublishCast();
  void MergeEngagedWindow(const CombatParams& params);
  void PublishRoster(const CombatParams& params);

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  // Measured rather than played. See CombatParams::measuring.
  bool measuring_ = false;
  // The encounter the queue was filled from: its type indices mean nothing
  // for any other, so a change here invalidates them.
  std::string encounter_;
  std::vector<QueuedMob> queue_;  // remaining mobs this cycle, front = engaged
  int next_mob_id_ = 0;           // stamped onto each arrival; see MobStatus
  double attack_phase_ = 0.0;     // seconds into the current swing
  // One buff animation the character owes. Raising a buff takes its cast off
  // the swing clock, so a handful at once leaves that clock in debt and the
  // character casting rather than swinging.
  struct OwedCast {
    std::string name;
    // The swing phase it is finished at: the clock climbing back to this mark
    // is what ends the cast.
    double done_at = 0.0;
    double seconds = 0.0;
  };
  // Newest last, which is the order they play out in. Cleared with the swing
  // clock, the casts being owed against it.
  std::vector<OwedCast> owed_casts_;
  double respawn_phase_ = 0.0;  // seconds into the current respawn cycle
  // Sampled when the cycle began, not read every step: the Wild Totem halves
  // the beat mid-fight, and the wait already being stood through is not the
  // one it shortens.
  double respawn_interval_ = 0.0;
  double player_hp_ = 0.0;  // remaining player HP, topped up on a beat
  double hit_phase_ = 0.0;  // seconds into the engaged mob's next hit
  // One entry per buff in params.buffs. These keep running across a change
  // of map: a buff belongs to the character, not to the mobs.
  struct BuffClock {
    double left = 0.0;           // seconds it still stands
    double cooldown_left = 0.0;  // seconds until it can go up again
    // Lines still to land before a buff charged by hits goes up.
    double charge_left = 0.0;
    // Hits the shell has left. An emptied shell falls at once, whatever is
    // left of its clock. 0 for a buff that is not a shell.
    int blocks_left = 0;
    // Which stance went up, or -1 for a buff with one form. Chosen at the
    // cast and left alone: GMS took away the key that swapped forms.
    int stance = -1;
    // Seconds into the duty cycle of a buff granting in bursts, from the
    // raise and wrapping at its interval. 0 for every other buff.
    double duty_phase = 0.0;
  };
  std::vector<BuffClock> buffs_;
  // Which buffs STAND, and which of those are granting now. They differ only
  // in a duty-cycled buff's gap between grants: the angel goes on striking
  // there, so its pulse is gated on the first and the tables picked with the
  // second. See BuffOption::duty_seconds.
  int buff_mask_ = 0;
  int lever_mask_ = 0;
  // Damage and seconds this ENCOUNTER, for the rate SecondsLeft divides
  // remaining HP by. Another encounter's damage says nothing about this one.
  double damage_dealt_ = 0.0;
  double fight_seconds_ = 0.0;
  // Per-attack totals, parallel to params.attacks. These run for the life of
  // the FIGHT, not per encounter: what reads them is a measurement.
  std::vector<AttackTally> by_attack_;
  // What is riding the credited attack. Set around the Hurt calls that are
  // not the swing striking for itself, and put back after.
  enum class Rider { kItself, kFinalAttack, kBurn };
  Rider riding_ = Rider::kItself;
  double own_clock_damage_ = 0.0;
  std::map<DamageSource, double> own_clock_by_source_;
  // Which attack the landing damage belongs to, or -1 for an own clock. Set
  // around each strike, the only place that knows.
  int attributing_ = -1;
  // What is striking, read only where attributing_ says the swing does not
  // own what landed.
  DamageSource striking_;
  // Seconds before a passive will revive the player again. Counts down
  // wherever the character is: it measures the pact, not the fight.
  double revive_left_ = 0.0;
  // What is left of the emergency heal's pour, and of the wait before it can
  // fire again. See RunEmergencyHeal.
  double emergency_left_ = 0.0;
  double emergency_cooldown_left_ = 0.0;
  // One entry per cast in params.auto_attacks.
  struct AutoClock {
    // Seconds into its next cast. Runs only while there is something to hit.
    double phase = 0.0;
    // Ticks spent of what one raising of its gating buff is worth. Zeroed
    // while that buff is down, so the count is per window.
    int pulses = 0;
    // Pulses since its last empowered one.
    int empowered_count = 0;
  };
  std::vector<AutoClock> auto_clocks_;
  // Seconds into each fountain's next pulse. Starts at 0, so the first pulse
  // falls one interval in rather than free on the opening step.
  std::vector<double> regen_phase_;
  // Swings credited toward each triggered attack's next cast. Fractional: a
  // swing can be worth less than a whole one.
  std::vector<double> trigger_count_;
  // Defeats credited toward the same casts. Whole: a monster died or did
  // not.
  std::vector<int> kill_count_;
  // Defeats since CreditKills last ran. Held rather than read off
  // view_.kills_this_step, which is zeroed every step: the count belongs to
  // the skill's clock, not to the step.
  int kills_pending_ = 0;
  // Where one swing stands, one entry per attack in params.attacks.
  struct AttackClock {
    // Seconds before it can be chosen again; 0 when ready.
    double cooldown_left = 0.0;
    // The same for the strike it sets off beside itself, held apart because
    // the swing is still there while its strike waits.
    double side_cooldown_left = 0.0;
    // Swings landed since its last empowered one.
    int empowered_count = 0;
    // Swings the loading buff has left to pay for. 0 once the buff is down,
    // which is what keeps the attack off the list of swings on offer.
    int charges_left = 0;
    // Seconds toward the next charge a load prepares for itself. 0 while the
    // bank is at its cap, so a raising never has its clock running under it.
    double load_phase = 0.0;
    // Charges banked for a hold bought out of a bank, fractional while the
    // next fills. Starts FULL, as a cooldown starts ready.
    double hold_charges = 0.0;
    // Strikes of a told-apart swing still to land, and the wait for the
    // next. The player is free while they run: what the cast bought is the
    // barrage, not the time it takes. Per attack, since two can be in the
    // air at once and neither cuts the other short.
    int strikes_left = 0;
    double next_strike_seconds = 0.0;
  };
  std::vector<AttackClock> attack_clocks_;
  // This step in seconds, for the one reader lining the swing's phase up
  // against a clock already wound down -- see HoldSaves.
  double step_seconds_ = 0.0;

  // Freeze Stacks held. Belongs to the character like the buff clocks do, so
  // it survives walking somewhere else.
  int freeze_stacks_ = 0;
  // The one wound standing. Belongs to the queue -- a monster that dies
  // takes its wound with it -- but held here because only one ever stands.
  WoundState wound_;
  // Shuffles each batch of arrivals so types are fought mixed (see TopUp).
  // Default-seeded, so a sim plays out the same way every run.
  std::mt19937 rng_;

  // The swing being charged, refreshed each Advance. The panel reads these
  // off the view; these are the fight's own copies, which the aim and the
  // strike both need.
  int reach_ = 1;  // also the width of the engaged window the UI draws
  double swing_seconds_ = 0.0;
  // The aimed attack, so landing it starts that attack's cooldown, and while
  // it charges the swing that is committed to. -1 with nothing aimed.
  int aimed_ = -1;
  // Enemies the swing is reaching, measured at the top of the step so a rain
  // on its own clock and the swing that set it off agree about the crowd. A
  // step old, which is near enough for a count that moves with the queue.
  int swing_enemies_ = 0;
  // Pulses the aimed swing is held for, settled when it was aimed: the orb
  // already being held is not re-timed under the player as the queue moves.
  int held_pulses_ = 0;
  // The level the last step ran at, so the next can catch a level-up.
  int player_level_ = 0;
  DamageLedger ledger_;
  FightView view_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
