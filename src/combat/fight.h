/* The live fight: the player auto-attacks a map's mobs while the front mob hits
 * back. Each respawn refills the mobs, keeping those still alive.
 *
 * The player's HP lives here because it doesn't outlast a fight. One engine
 * drives both halves of combat: the kills it reports are what the reward code
 * pays for, and the same step drives the panel.
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
  // Advances the fight by `elapsed_seconds`. A gap longer than one attack is
  // capped, so a stall loses progress instead of paying out a burst.
  void Advance(const CombatParams& params, double elapsed_seconds);

  // Lowers the HP of the monsters in `hp_by_id` to the given fractions and
  // removes any that died. Only lowers HP, and leaves unlisted monsters alone:
  // a local copy of a shared fight may be ahead of the server, but never
  // behind.
  void ClampRoster(const CombatParams& params,
                   const std::map<int, double>& hp_by_id);

  // What the last Advance published. Fully refreshed each step.
  const FightView& view() const {
    return view_;
  }

  // True while a valid encounter is being fought.
  bool active() const {
    return active_;
  }
  // True while all mobs are dead and the fight waits for the next respawn.
  bool respawning() const {
    return respawning_;
  }
  // Every damage line from the last Advance, in order. Empty unless
  // CombatParams::record_damage_lines is set.
  const std::vector<DamageLine>& damage_lines_this_step() const {
    return ledger_.lines_this_step();
  }
  // The skill name for a line's `credit`.
  const std::string& damage_credit_name(int credit) const {
    return ledger_.credit_name(credit);
  }
  // The Ice/Lightning mage's Freeze Stacks; 0 without Freezing Crush.
  int freeze_stacks() const {
    return freeze_stacks_;
  }
  // Seconds before the attack at `index` can be chosen again; 0 when ready.
  double cooldown_left(int index) const {
    return index >= 0 && index < static_cast<int>(attack_clocks_.size())
               ? attack_clocks_[index].cooldown_left
               : 0.0;
  }

  // Seconds until the next event that can change an attack's value: the attack
  // landing, or a buff starting or ending. A longer step still works but may
  // use the wrong buffs. Infinite when nothing will change on its own.
  double SecondsToNextEvent(const CombatParams& params) const;

  // Damage per params.attacks entry since the fight began, with burns credited
  // to the attack that applied them. Damage from things on their own timer is
  // in own_clock_damage() instead; together they are all the damage dealt.
  const std::vector<AttackTally>& by_attack() const {
    return by_attack_;
  }
  double own_clock_damage() const {
    return own_clock_damage_;
  }
  // own_clock_damage() split by source. Look up the index in the list for its
  // origin: kOwnClock in params.auto_attacks, kSwingClock and kKillClock in
  // params.triggered_attacks, the rest in params.attacks.
  const std::map<DamageSource, double>& own_clock_by_source() const {
    return own_clock_by_source_;
  }
  // Which params.buffs are active, as the mask CombatParams uses to index its
  // attack tables. Updated at the start of every step.
  int buff_mask() const {
    return buff_mask_;
  }

 private:
  // One burn on one monster. Its damage is fixed when applied; see the Dot
  // message.
  struct MobDot {
    double left_seconds = 0.0;
    double phase = 0.0;
    double interval_seconds = 0.0;
    double damage = 0.0;
    // Stacks carried, each dealing full damage. 1 for every burn except a Night
    // Lord's poison. Only fractional while measuring.
    double stacks = 0.0;
    SwingRolls rolls;
    // The attack that last applied it, so its damage is credited there; -1 for
    // an empty slot.
    int lit_by = -1;
    // The breakdown credit for its damage. See DamageLedger::Credit.
    int credit = -1;
  };

  // A mob in the queue: its type (an index into params.types) and its HP.
  struct QueuedMob {
    int type = 0;
    double hp = 0.0;
    // Stored because GMS picks a wound's target by max HP: a worn-down boss
    // part is still the biggest thing on the map. See ApplyWound.
    double max_hp = 0.0;
    // Never reused within an encounter, so a caller's per-mob bar can't be
    // given the mob that replaced the one it was drawing.
    int id = 0;
    // Hits from a marking attack since its last mark triggered. The count is on
    // the mob, so it's lost when the mob dies.
    int brand = 0;
    // One slot per burn source the character can apply; empty for everyone
    // except the Fire/Poison mage.
    std::vector<MobDot> dots;
    // Seconds it stays frozen, set by ice attacks. What being frozen is worth
    // depends on the character; see BoostForStacks.
    double frozen_left_seconds = 0.0;
    // Scarring Sword's scar: how long it lasts, and the chance it's there at
    // all. Kept as a probability rather than rolled, like every other chance in
    // this fight: a monster with a 50% chance of being scarred takes half the
    // bonus.
    double scarred_left_seconds = 0.0;
    double scar_odds = 0.0;
    // Whether it's a boss, which can't be stunned. See Afflicted.
    bool boss = false;
    // The stun on it, and the bonus it gives attacks that benefit.
    double stunned_left_seconds = 0.0;
    double stun_lift_pct = 0.0;
    // The angel's mark on it, and the bonus for the line that consumes it.
    double marked_left_seconds = 0.0;
    double mark_lift_pct = 0.0;
  };

  // The single wound in the fight, tracked by mob ID. Only one because that is
  // the rule: in GMS the wound moves to whichever mob was hit last.
  struct WoundState {
    int mob_id = -1;
    int stacks = 0;
    double left_seconds = 0.0;
  };

  // The landing for the mob at `index`, scaled by `scale`. The one place the
  // queue and the ledger meet.
  Landing LandingAt(int index, double scale) const {
    return ledger_.LandingAt(queue_[index].id, index, scale);
  }

  // Counts and removes every mob at 0 HP or below. Used by attacks and burns
  // alike.
  void Reap();
  // Applies each of `attack`'s burns to everything it hit. A mob already
  // burning has its timer restarted and damage recomputed; whether a new stack
  // is added depends on the burn.
  void ApplyDots(const AttackOption& attack, int hit);
  // Advances every burn by `dt`, dealing the ticks that come due.
  void RunDots(double dt);

  // Advances every HP regen effect by `dt`, healing the pulses that come due.
  // Each heals up to max HP, whether or not mobs are present.
  void RunRegen(const CombatParams& params, double dt);
  // The automatic heal when nearly dead: activates under the threshold, heals
  // over its window, then waits out its cooldown. See EmergencyHeal.
  void RunEmergencyHeal(const CombatParams& params, double dt);

  // Adds the mobs each type is missing: a respawn adds new monsters rather than
  // healing the current one. Doesn't touch the attack timer.
  void TopUp(const CombatParams& params);
  // Moves the healthiest mob to the front, so a single-target attack hits
  // whatever will last longest. Does nothing on maps; see focus_healthiest.
  void AimAtHealthiest(const CombatParams& params);
  // The HP fraction a landed `attack` heals through its random procs. Returned
  // rather than applied, since the strike doesn't manage HP. See Proc.
  double RollProcs(const AttackOption& attack, int hit);
  // The multiplier `stacks` Freeze Stacks give `attack` against a mob of `type`
  // that is or isn't `frozen`. The stack count sets the value; being frozen
  // sets whether it applies. `type` matters because Shatter depends on that
  // monster's defense.
  double BoostForStacks(const AttackOption& attack, int stacks, int type,
                        bool frozen) const;
  // The same against a monster in the queue, as it is right now.
  double FreezeBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // The multiplier the scar on this monster gives the attack. An already
  // scarred mob gives Chance Attack's full bonus; a fresh one gives it only to
  // lines after the scar, with nothing for the line that applied it.
  double ScarBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Whether the mob has any status the fight tracks: frozen, burning or
  // stunned.
  bool Afflicted(const QueuedMob& mob) const;
  // The bonus from the enemy's condition: whether it has a status, and how many
  // burns are on the group.
  double ConditionBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // The same with both values given, so callers can ask what one more status
  // would be worth.
  double ConditionBoostFor(const AttackOption& attack, bool afflicted,
                           int alight) const;
  // Burns active across the group, which the drain effects count.
  int BurnsAlight() const;
  // The same counted in stacks, which scattered attacks scale with.
  int BurnStacksAlight() const;
  // How long one raise of this buff lasts, including time added per burn.
  double BuffWindowSeconds(const BuffOption& buff) const;
  // Rolls for every buff a landed attack can raise; see
  // BuffOption::raise_chance. `afflicted` is whether the attack hit an enemy
  // with a status, which some require.
  void RaiseRolledBuffs(const CombatParams& params, bool afflicted);
  // Raises one such buff, whose windows are params.buffs[first, end): fills the
  // first free one on a successful roll and keeps the group sorted.
  void RaiseOneWindow(const CombatParams& params, int first, int end);
  // Sorts each rolled buff's windows by time remaining, so the active ones are
  // always first in their group.
  void CompactRolledWindows(const CombatParams& params);
  double BurnLeftOn(const QueuedMob& mob, int slot) const;
  double BurningRate(const CombatParams& params, const QueuedMob& mob,
                     int alight) const;
  // What applying this attack's burns is worth to later attacks, beyond the
  // burn damage BurnCredit already counts.
  double BurnStateCredit(const CombatParams& params,
                         const AttackOption& attack) const;
  // Both status bonuses in one factor, since every caller wants both.
  double StateBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Applies this attack's scar to the front `hit` mobs, after the strike, like
  // burns. Stored as a probability: n lines scar with probability 1 - (1 -
  // chance)^n.
  void ApplyScar(const AttackOption& attack, int hit);
  // Counts down every scar, clearing the probability when it expires.
  void RunScar(double dt);
  // Applies this attack's freeze to the front `hit` mobs. After the strike, so
  // the attack is valued on the state the mobs were in when it hit.
  void ApplyFreeze(const AttackOption& attack, int hit);
  // Counts down freezes on monsters. The character's own stacks are spent, not
  // timed, so they're untouched here.
  void RunFreeze(double dt);
  // What the freeze this attack would apply is worth on top of its damage: the
  // added seconds, valued at FrozenRate. Priced like a reapplied burn (see
  // BurnCredit), and worth nothing against a mob already frozen that long.
  double FrozenCredit(const CombatParams& params,
                      const AttackOption& attack) const;
  // How much damage per second the best available attack gains against `mob`
  // from it being frozen.
  double FrozenRate(const CombatParams& params, const QueuedMob& mob) const;
  // The front of the queue, for callers without a specific enemy. A plain
  // unfrozen type-0 mob when the queue is empty.
  const QueuedMob& FrontMob() const;
  // Stacks one strike of `attack` adds. Uses the skill's single-target count
  // when it lists one and there's only one enemy. See
  // AttackOption::freeze_build.
  int FreezeBuilt(const AttackOption& attack) const;
  // What the stacks `attack` would add are worth to the best available attack.
  // Added to its rate like side-strike damage; otherwise an attack chooser that
  // only looks at this attack would never build stacks.
  double FreezeCredit(const CombatParams& params,
                      const AttackOption& attack) const;
  // Updates the stack count: ice adds a stack per line, lightning spends one.
  // After the strike, so an attack is valued on the stacks it started with.
  void CreditFreeze(const CombatParams& params, const AttackOption& attack);
  // Hits that come with a strike rather than being part of it: the opening hit,
  // both Final Attack types, and the wide hit. Rolled after the main strike, so
  // they see the marks it consumed.
  void StrikeRiders(const AttackOption& attack, int hit,
                    const std::vector<int>& lead);
  // Applies every status the strike leaves on what it hit. Runs before dead
  // mobs are removed, so the indices still match.
  void ApplyStates(const AttackOption& attack, int hit);
  // Lands one attack on the front of the queue and removes what it killed.
  // Normal attacks and auto-firing skills are handled the same way here.
  //
  // Returns the HP fraction its random procs heal, reported rather than
  // applied, like kills. `pulses` is how long a held attack was held; -1 lets
  // the strike decide from the current queue.
  double Strike(const AttackOption& attack, DamageSource source,
                int pulses = -1);
  // Pulses worth holding for: enough to bring every locked-on enemy within
  // range of the finishing strike, and no more. It never retargets, so pulses
  // after they're dead are wasted. 0 for non-held attacks.
  int ChannelPulses(const AttackOption& attack, int hit) const;
  // Seconds one attack takes against the current queue: its own time, or for a
  // held attack, only as long as holding is worthwhile.
  double SwingSecondsAgainst(const AttackOption& attack) const;
  // The same for the attack being charged now, fixed when it was aimed: a hold
  // in progress isn't retimed mid-hold.
  double HeldSeconds(const AttackOption& attack) const;
  // One pulse of a held attack against `type`: the attack's first group of
  // lines, which the hold repeats.
  double PulseDamage(const AttackOption& attack, int type) const;
  // The finishing strike of a hold: every group after the first.
  double HeldPulseDamage(const AttackOption& attack, int type,
                         int pulses) const;
  double FinishDamage(const AttackOption& attack, int type) const;
  // A hold of `pulses` on one mob: every pulse rolled separately, then the
  // finishing strike once.
  double ChannelDamage(const AttackOption& attack, int type, int pulses,
                       const Landing& landing);
  // The multiplier a stun on this monster gives the attack: its bonus if the
  // attack benefits, otherwise 1.
  double StunBoost(const AttackOption& attack, const QueuedMob& mob) const;
  // Applies this attack's stun and counts stuns down, like ApplyFreeze and
  // RunFreeze.
  void ApplyStun(const AttackOption& attack, int hit);
  void RunStun(double dt);
  // The multiplier a mark gives, and consumes the mark: one line gets the bonus
  // and it's gone, so a ten-line attack gets a tenth of it.
  double SpendMark(const AttackOption& attack, QueuedMob& mob);
  // Applies this attack's mark and counts marks down.
  void ApplyMark(const AttackOption& attack, int hit);
  void RunMark(double dt);
  // Applies this attack's wound to the hit enemy with the highest max HP. Only
  // one monster can have a wound, so a new one moves it from whoever had it.
  // See Wound.
  void ApplyWound(const AttackOption& attack, int hit);
  void RunWound(double dt);
  // Whether a living monster's wound has enough stacks for `attack`'s heavier
  // form. False for every attack except Trickblade's form.
  bool WoundFull(const AttackOption& attack) const;

  int Reached(const AttackOption& attack) const;
  // Fires barrage strikes that have come due.
  void RunBarrage(const CombatParams& params, double dt);
  void RunBarrageOf(const CombatParams& params, int index, double dt);
  // Enemies the wide part of an attack hits. See wide_hit_damage.
  int WideHitTargets(const AttackOption& attack, int hit) const;
  double RolledGroups(const std::vector<HitGroup>& groups,
                      const std::vector<double>& expected, int type,
                      const Landing& landing);
  // Enemies the per-attack Final Attack sources hit: their own reach, limited
  // to the mobs present (e.g. Split Shot's ten after a Snipe that hit one). 0
  // without such a source.
  int PerSwingFinalAttackTargets(const AttackOption& attack, int hit) const;
  // Lines added when the character's attack hits a crowd: lines_per_extra_enemy
  // per enemy past the first, up to max_extra_lines.
  int ExtraLines(const AttackOption& attack) const;
  // Hits a scattered attack makes, which existing burns can increase.
  int ScatterHits(const AttackOption& attack) const;
  std::vector<double> ScatterShares(const AttackOption& attack, int hit) const;
  std::vector<int> PierceOrder(const AttackOption& attack, int hit);
  // Queue indices the opening hit targets: the healthiest `lead_enemies` of
  // those in range, since a hit that big wastes the least there. GMS also aims
  // it at the highest-HP target.
  std::vector<int> LeadTargets(const AttackOption& attack, int hit) const;
  // Damage `attack`'s own strikes deal to the front `hit` mobs: its lines,
  // pierce bonus, opening hit and both Final Attack types.
  double StrikeDamage(const AttackOption& attack, int hit) const;
  // Damage gained by reapplying `burn` over `cadence` seconds, beyond what the
  // mob's existing burn would have dealt anyway.
  double BurnCredit(const DotApplication& burn, const QueuedMob& mob,
                    double cadence) const;
  // Value of `attack`'s burns, each counted at what reapplying gains rather
  // than its full damage.
  double BurnDamage(const AttackOption& attack, int hit) const;
  // Value per attack of the strike `attack` triggers, spread over the attacks
  // made while it's on cooldown.
  double SideStrikeDamage(const AttackOption& attack) const;
  // Value of the stored attack `attack` fires this time: all of it while a
  // charge is available, nothing once spent, since one press uses them all.
  double LoadedDamage(const AttackOption& attack) const;
  // Strikes of a spaced-out attack credited to one press: those that land
  // before it can be used again (its cooldown, or its own attack time if it has
  // none). 1 for attacks whose strikes land together.
  double BarrageStrikes(const AttackOption& attack) const;
  // Damage one attack would deal to the current queue. An empowered form is
  // averaged over the attacks it replaces one of.
  double SwingDamage(const AttackOption& attack) const;
  // The form `attack` actually uses this time: its empowered form when `count`
  // reaches the trigger. Advances `count`, so call it exactly once per landed
  // attack.
  const AttackOption& FormToLand(int& count, const AttackOption& attack);
  // Damage `attack` deals to the mob at `index`: its rolled damage, plus its
  // empowered form when that mob's mark count triggers. Advances the mark, so
  // call it exactly once per mob per landed attack.
  double DamageToMob(const AttackOption& attack, int index,
                     const Landing& landing);
  // The random multiplier for this hit: rolled in a played fight, exactly 1
  // when measuring. The roll averages 1, so measurement compares builds without
  // dice noise.
  double Roll(const SwingRolls& rolls);
  // How much of a `chance` event happened: 1 or 0 when rolled, `chance` itself
  // when measuring.
  double Chance(double chance);
  // Damage `attack` deals to one mob of `type`: each hit group with its own
  // roll, or the plain expected damage for an attack with no groups.
  double RolledDamage(const AttackOption& attack, int type,
                      const Landing& landing);
  // Damage a set of Final Attack sources deals to one mob: one roll per source,
  // per line for per-line sources. `expected` is used when there is nothing to
  // roll.
  double RolledFinalAttack(const std::vector<FinalAttackRoll>& sources,
                           const std::vector<double>& expected, int type,
                           const Landing& landing);
  // Whether the attack at `index` is still on cooldown.
  bool Recharging(int index) const;
  // Whether the attack at `index` has a charge to spend. True for every attack
  // no buff loads; see AttackOption::charges.
  bool Loaded(const CombatParams& params, int index) const;

  // Whether a charge-based hold has a charge available. True for attacks
  // without charges.
  bool Charged(const CombatParams& params, int index) const;

  // Pulses the charges will pay for, or the full hold without charges. A press
  // spends a whole charge for part of one, as GMS spends a light for every
  // second held.
  int ChargedPulses(const CombatParams& params, int index) const;
  // The healing skill to use for this attack, or -1: HP isn't low enough,
  // there's nothing to fight, or there's no such skill. An empty map heals for
  // free on respawn.
  int HealToCast(const CombatParams& params) const;
  // Whether the attack at `index` can be used now: a normal attack (not
  // auto-firing), off cooldown, loaded, and charged.
  bool OnOffer(const CombatParams& params, int index) const;
  // One attack's value per second against the current queue: damage plus the
  // statuses it leaves, divided by the time it takes. Every choice in here uses
  // this measure. Takes the attack by reference, so it can be valued under
  // buffs that aren't active yet.
  double SwingRate(const CombatParams& params,
                   const AttackOption& attack) const;

  // A buff window the fight can see coming. `seconds` is infinite when none is
  // coming, and `mask` is then the current mask.
  struct ComingWindow {
    double seconds = 0.0;
    int mask = 0;
  };
  // The next buff window to open, from the buff timers. Only buffs on their own
  // cooldown are predictable: buffs applied by attacks, charged by lines, or
  // raised when needed (shields) can't be waited for.
  ComingWindow NextWindow(const CombatParams& params) const;
  // Whether waiting for `window` gains the attack at `index` an extra use it
  // wouldn't otherwise get: a cooldown longer than the wait, or charges that
  // wouldn't overflow while waiting.
  bool HoldSaves(const CombatParams& params, int index,
                 const ComingWindow& window) const;
  // Whether saving it pays off: what the use gains inside `window`, against the
  // cost of delaying all later uses. `filler` is the attack used in its place,
  // which makes this a real comparison.
  bool HoldPays(const CombatParams& params, int index, int filler,
                const ComingWindow& window) const;
  // The best available attack, skipping indices marked in `held`. -1 when none
  // is available.
  int TopAttack(const CombatParams& params,
                const std::vector<bool>& held) const;
  // The attack with the most damage per second against the current queue, or -1
  // if there's nothing to hit. Measured per second and against the actual
  // queue, so slow animations must hit proportionally harder and wide skills
  // lose their edge on a sparse map. A big attack ready just before a buff
  // window is saved for it.
  int BestAttack(const CombatParams& params) const;
  // The attack to use this step: the one already being charged, or a new pick.
  // An attack mid-animation finishes first, except the basic attack, since
  // waiting on it would delay the skill it filled in for.
  int ChooseAttack(const CombatParams& params) const;
  // The attack that applies a buff that isn't active, or -1. Takes priority
  // over BestAttack; the heal takes priority over this.
  int BuffToLay(const CombatParams& params) const;

  // The steps of one Advance, in the order it runs them.
  void GoIdle();
  void BeginMapIfChanged(const CombatParams& params);
  // Refills the mobs on respawn, and restores the HP a respawn gives.
  void RespawnBeat(const CombatParams& params, double dt);
  void TakeMobHit(const CombatParams& params, double dt);
  // The fraction of a hit left after active buffs reduce it.
  double BuffDamageTakenFactor(const CombatParams& params) const;
  // Uses one of the active shield's blocks, removing the shield when the last
  // is used. Returns true if the hit was fully blocked.
  bool BlockHit(const CombatParams& params);
  // Whether a shield is worth raising now: on a boss as soon as it's ready, on
  // a map only once HP is low enough for its heal to matter.
  bool ShieldWanted(const CombatParams& params, const BuffOption& buff) const;
  // Whether a passive revives the player from a killing hit. Only called at 0
  // HP; if it returns true, HP has already been refilled.
  bool Revive(const CombatParams& params);
  // Reflects the reflected fraction of a hit back to the mob that dealt it.
  void Reflect(const CombatParams& params, double damage_taken);
  // Counts buff timers down, raises buffs that are ready, and computes the
  // mask. Runs before attacks, so a buff raised now applies to this attack.
  void RunBuffs(const CombatParams& params, double dt);
  // The form of `buff` worth the most over the rest of this fight, or -1 for
  // single-form buffs. See SecondsLeft.
  int StanceToRaise(const CombatParams& params, const BuffOption& buff) const;
  // Seconds this encounter is expected to last, from remaining HP and the
  // damage rate so far. Infinite on a map, which never ends.
  double SecondsLeft(const CombatParams& params) const;
  // Reduces each buff's cooldown for a landed attack. `weight` is the same
  // factor CreditSwing uses, so a rapid attack doesn't count as a full one.
  // `lines` feeds buffs charged by hits, which don't count while the buff is
  // active.
  void CreditBuffs(const CombatParams& params, double weight, int lines);
  // Raises the buffs the attack at `swung` applies, on cast or on landing as
  // `on_cast` says. Returns true if any went up, telling the caller to re-read
  // the attack under the new mask.
  bool LayBuffs(const CombatParams& params, int swung, bool on_cast);
  // The attack lists under the active buffs. Every set has the same attacks in
  // the same order, so an index stays valid when a buff ends.
  const std::vector<AttackOption>& Attacks(const CombatParams& params) const;
  const std::vector<AttackOption>& AutoAttacks(
      const CombatParams& params) const;
  const std::vector<AttackOption>& TriggeredAttacks(
      const CombatParams& params) const;
  // Max Freeze Stacks under those buffs; higher while Glacial Fury is active.
  int FreezeCap(const CombatParams& params) const;
  // Fires auto-firing skills before the attack is aimed, so it targets whatever
  // they leave alive.
  void RunAutoCasts(const CombatParams& params, double dt);
  // Heals `share` of max HP, capped at full.
  void RecoverHp(const CombatParams& params, double share);
  // Credits a landed attack to skills triggered by attacks, and fires any that
  // are due. `weight` is the attack's value: a seventh for one that lands seven
  // times as often. Remainders carry over, or a rapid attack would never
  // trigger the skill.
  void CreditSwing(const CombatParams& params, double weight);
  // Credits kills to skills triggered by kills and fires any that are due. Uses
  // a count the reaping keeps, so kills count however they happened. The
  // pending count is taken before anything fires, so a wide skill on a dying
  // crowd can't retrigger itself over and over in one step.
  void CreditKills(const CombatParams& params);
  // Clears the step's reported values and starts the ledger's step. All of it
  // describes this step only.
  void OpenStep(const CombatParams& params);
  // Makes room for attacks added since the last step (a buff going up adds its
  // magazine's attack, which needs a timer). Charges start full, like cooldowns
  // start ready.
  void GrowForAttacks(const CombatParams& params);
  // Counts cooldowns down before aiming, so an attack coming off cooldown this
  // step can be used.
  void RunCooldowns(const CombatParams& params, double dt);
  // Aims the next attack at the queue and names it for the charge bar. Null if
  // there's nothing to hit.
  const AttackOption* AimSwing(const CombatParams& params);
  // Charges the attack and lands every one due this step.
  void RunSwing(const CombatParams& params, double dt);
  // One attack landing: the strike and everything that comes with it. Leaves
  // the next attack aimed.
  void LandSwing(const CombatParams& params, const AttackOption& attack);
  // The side strike and stored attack a press triggers. Both use the aimed
  // attack rather than any form that replaced it, since they belong to the
  // skill. They hit whatever the attack left alive.
  void StrikeExtras(const AttackOption& cast, int swung);
  // The HP fraction `landed` heals. Recovery comes from hits, so a cast gets
  // none, and a hold pays per pulse rather than per press.
  double SwingRecovery(const CombatParams& params, const AttackOption& landed,
                       double proc_recovered) const;
  // Applies the attack's own costs: its cooldown, a charge, and the lights a
  // hold used.
  void SpendSwingClocks(const AttackOption& attack, int swung);
  // Deals `damage` to `mob` and counts it. All character damage goes through
  // here.
  void Hurt(QueuedMob& mob, double damage);
  void PublishPlayer(const CombatParams& params);
  void PublishTarget(const CombatParams& params);
  // Shows the buff being cast on the charge bar, filling over its animation.
  // Returns true if a cast is using the bar.
  bool PublishCast();
  void MergeEngagedWindow(const CombatParams& params);
  void PublishRoster(const CombatParams& params);

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  // Measured rather than played. See CombatParams::measuring.
  bool measuring_ = false;
  // The encounter the queue was filled from. Type indices are only valid for
  // that encounter, so a change invalidates them.
  std::string encounter_;
  std::vector<QueuedMob> queue_;  // mobs left this cycle; front is engaged
  int next_mob_id_ = 0;           // assigned to each new mob; see MobStatus
  double attack_phase_ = 0.0;     // seconds into the current attack
  // A buff animation the character still has to play. Raising a buff takes its
  // cast time from the attack timer, so raising several at once puts that timer
  // in debt, and the character casts instead of attacking.
  struct OwedCast {
    std::string name;
    // The attack timer value at which it finishes: the timer climbing back to
    // this ends the cast.
    double done_at = 0.0;
    double seconds = 0.0;
  };
  // Newest last, which is the order they play. Cleared with the attack timer,
  // since they're owed against it.
  std::vector<OwedCast> owed_casts_;
  double respawn_phase_ = 0.0;  // seconds into the current respawn cycle
  // Read when the cycle starts, not every step: the Wild Totem halves the
  // interval mid-fight, but not the wait already in progress.
  double respawn_interval_ = 0.0;
  double player_hp_ = 0.0;  // remaining HP, restored on respawn
  double hit_phase_ = 0.0;  // seconds until the front mob's next hit
  // One entry per buff in params.buffs. These keep running across map changes,
  // since buffs belong to the character, not the mobs.
  struct BuffClock {
    double left = 0.0;           // seconds it has left
    double cooldown_left = 0.0;  // seconds until it can be raised again
    // Lines left to land before a hit-charged buff goes up.
    double charge_left = 0.0;
    // Hits the shield has left. An empty shield ends immediately, whatever time
    // is left. 0 for buffs that aren't shields.
    int blocks_left = 0;
    // Which form was raised, or -1 for single-form buffs. Chosen at cast and
    // never changed: GMS removed the key that swapped forms.
    int stance = -1;
    // Seconds into the on/off cycle of a buff that grants in bursts, from when
    // it was raised and wrapping at its interval. 0 for other buffs.
    double duty_phase = 0.0;
  };
  std::vector<BuffClock> buffs_;
  // Which buffs are up, and which of those are currently granting. They differ
  // only during a duty-cycled buff's off time: the angel keeps attacking then,
  // so its pulse checks the first mask and the damage tables use the second.
  // See BuffOption::duty_seconds.
  int buff_mask_ = 0;
  int lever_mask_ = 0;
  // Damage and seconds in this encounter, for the rate SecondsLeft divides
  // remaining HP by. Another encounter's damage says nothing about this one.
  double damage_dealt_ = 0.0;
  double fight_seconds_ = 0.0;
  // Per-attack totals, parallel to params.attacks. These cover the whole fight,
  // not one encounter, since their reader is a measurement.
  std::vector<AttackTally> by_attack_;
  // What kind of damage is being credited to the attack. Set around Hurt calls
  // that aren't the attack's own strike, and reset after.
  enum class Rider { kItself, kFinalAttack, kBurn };
  Rider riding_ = Rider::kItself;
  double own_clock_damage_ = 0.0;
  std::map<DamageSource, double> own_clock_by_source_;
  // The attack the current damage belongs to, or -1 for something on its own
  // timer. Set around each strike, the only place that knows.
  int attributing_ = -1;
  // What is striking, used only when attributing_ says the damage isn't the
  // attack's own.
  DamageSource striking_;
  // Seconds until a passive can revive the player again. Counts down wherever
  // the character is, since it tracks the skill, not the fight.
  double revive_left_ = 0.0;
  // Time left in the emergency heal's window, and in its cooldown. See
  // RunEmergencyHeal.
  double emergency_left_ = 0.0;
  double emergency_cooldown_left_ = 0.0;
  // One entry per skill in params.auto_attacks.
  struct AutoClock {
    // Seconds into its next cast. Only advances while there's something to hit.
    double phase = 0.0;
    // Ticks used of what one raise of its required buff allows. Reset while
    // that buff is down, so the count is per raise.
    int pulses = 0;
    // Pulses since its last empowered one.
    int empowered_count = 0;
  };
  std::vector<AutoClock> auto_clocks_;
  // Seconds into each regen effect's next pulse. Starts at 0, so the first
  // pulse comes one interval in rather than free on the first step.
  std::vector<double> regen_phase_;
  // Attacks credited toward each triggered skill's next cast. Fractional, since
  // an attack can count for less than one.
  std::vector<double> trigger_count_;
  // Kills credited toward the same casts. Whole numbers.
  std::vector<int> kill_count_;
  // Kills since CreditKills last ran. Stored rather than read from
  // view_.kills_this_step, which resets every step, since the count belongs to
  // the skill's trigger rather than the step.
  int kills_pending_ = 0;
  // The state of one attack, one entry per attack in params.attacks.
  struct AttackClock {
    // Seconds before it can be chosen again; 0 when ready.
    double cooldown_left = 0.0;
    // The same for the side strike it triggers. Separate because the attack
    // stays available while its side strike is on cooldown.
    double side_cooldown_left = 0.0;
    // Attacks landed since its last empowered one.
    int empowered_count = 0;
    // Uses left from the loading buff. 0 once the buff ends, which removes the
    // attack from the available list.
    int charges_left = 0;
    // Seconds toward the next self-recharged charge. 0 while at max charges, so
    // the timer never runs while the buff is providing them.
    double load_phase = 0.0;
    // Charges available for a charge-based hold, fractional while the next
    // fills. Starts full, like a cooldown starts ready.
    double hold_charges = 0.0;
    // Strikes left in a spaced-out attack, and time to the next. The player can
    // act while they land: the cast bought the barrage, not the time it takes.
    // Per attack, since two can be active at once without cutting each other
    // short.
    int strikes_left = 0;
    double next_strike_seconds = 0.0;
  };
  std::vector<AttackClock> attack_clocks_;
  // This step's length, used by HoldSaves to line the attack timer up against
  // cooldowns already counted down.
  double step_seconds_ = 0.0;

  // Freeze Stacks held. Belongs to the character like the buff timers, so it
  // survives changing maps.
  int freeze_stacks_ = 0;
  // The single active wound. It belongs to a mob (a dying mob takes its wound
  // with it), but is stored here because only one exists at a time.
  WoundState wound_;
  // Shuffles each batch of new mobs so types are mixed (see TopUp). Uses the
  // default seed, so a sim plays out the same way every run.
  std::mt19937 rng_;

  // The attack being charged, updated each Advance. The panel reads the view;
  // these are the fight's own copies, needed for aiming and striking.
  int reach_ = 1;  // also the width of the engaged window the UI draws
  double swing_seconds_ = 0.0;
  // The aimed attack, so landing it starts that attack's cooldown, and so the
  // fight stays committed to it while charging. -1 if nothing is aimed.
  int aimed_ = -1;
  // Enemies the attack is reaching, measured at the start of the step so an
  // auto-firing rain and the attack that triggered it agree on the crowd. A
  // step out of date, which is close enough.
  int swing_enemies_ = 0;
  // Pulses the aimed attack will be held for, fixed when aimed: a hold in
  // progress isn't retimed as the queue changes.
  int held_pulses_ = 0;
  // The level at the last step, to detect level-ups.
  int player_level_ = 0;
  DamageLedger ledger_;
  FightView view_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
