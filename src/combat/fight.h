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
#include <random>
#include <string>
#include <vector>

#include "src/combat/encounter.h"

namespace ms {

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
  // The engaged window as HP bars, one per distinct type the next swing will
  // hit, in the order they appear in the queue. Empty while
  // respawning/inactive.
  const std::vector<EngagedGroup>& engaged_groups() const {
    return engaged_groups_;
  }

 private:
  // A mob waiting in or being fought in the queue: its type (an index into
  // params.types) and its remaining HP.
  struct QueuedMob {
    int type = 0;
    double hp = 0.0;
    // Strikes this mob has taken from a swing that marks what it hits, since
    // the last mark went off on it. A mark rides the mob rather than the
    // swing, so one that dies partway there takes its count to the grave and
    // whatever replaces it starts at nothing.
    int brand = 0;
  };

  // Brings the queue back up to a full roster, adding only what each type is
  // missing: a respawn puts new monsters on the map, it does not heal the one
  // being fought. Leaves the swing clock alone, since whether a top-up should
  // interrupt the swing depends on why it happened.
  void TopUp(const CombatParams& params);
  // Lands one attack on the front of the queue: the first max_enemies mobs
  // each take their own type's damage, one of them also takes the opening hit,
  // and the dead are counted and leave. A swing and a skill on its own clock
  // are the same thing here.
  void Strike(const AttackOption& attack);
  // The order a swing that gains as it travels goes through the `hit` mobs it
  // reached, drawn fresh each swing: nothing here has a position, so which
  // enemy an arrow meets first is arbitrary and drawing it keeps the gain from
  // always falling on the same end of the queue. Empty for every other swing,
  // whose order cannot be seen.
  std::vector<int> PierceOrder(const AttackOption& attack, int hit);
  // Indices into the queue of the mobs `attack`'s opening hit picks, empty when
  // it has none. The healthiest of the `hit` mobs the swing reaches, as many of
  // them as lead_enemies: a hit that big is worth least where it overkills, and
  // GMS aims the same shape at the highest-HP target for the same reason.
  std::vector<int> LeadTargets(const AttackOption& attack, int hit) const;
  // What one swing of `attack` would land on the queue as it stands, the
  // opening hit and Final Attack included. An attack with an empowered form is
  // averaged over the run of swings that form takes its place in.
  double SwingDamage(const AttackOption& attack) const;
  // What `attack` really lands this time: its empowered form when the count in
  // `counts` has come round, and itself otherwise. Advancing that count is the
  // point of the call, so it is made once per landed attack and nowhere else.
  // Serves both clocks -- the character's swings and a summon's pulses -- with
  // a counter apiece.
  const AttackOption& FormToLand(std::vector<int>& counts, int size, int index,
                                 const AttackOption& attack);
  // What `attack` lands on the queued mob at `index`: its ordinary damage
  // rolled, and its empowered form on top when that mob's mark has come round.
  // Only that form is the mark's business -- everything else is answered
  // before the swing lands, by FormToLand. Advances the mark, so it is called
  // once per mob per landed swing.
  double DamageToMob(const AttackOption& attack, int index);
  // What `attack` lands on one mob of `type` this time: each of its hit blocks
  // at its own roll. The plain expected damage for an attack carrying no
  // blocks, which is every one built by hand rather than by the encounter.
  double RolledDamage(const AttackOption& attack, int type);
  // What the Final Attacks following `attack` land on one mob of `type` this
  // time: a roll per source, per line where the source rides them. The plain
  // expected damage for an attack carrying no sources.
  double RolledFinalAttack(const AttackOption& attack, int type);
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
  // Takes what a landed swing is worth off the wait for each buff's next
  // cast. `weight` is what that swing counted for, the same share
  // CreditSwing uses -- a rapid swing must not pay a whole attack's worth.
  void CreditBuffs(const CombatParams& params, double weight);
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
  // Charges the swing and lands it when it comes round.
  void RunSwing(const CombatParams& params, double dt);
  // Refreshes the target and the engaged window for the panel to draw.
  void PublishTarget(const CombatParams& params);
  void MergeEngagedWindow(const CombatParams& params);

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  // Map the queue was filled from. Its type indices only mean anything for that
  // map, so a change here invalidates them.
  std::string map_;
  std::vector<QueuedMob> queue_;  // remaining mobs this cycle, front = engaged
  double attack_phase_ = 0.0;     // seconds into the current swing
  double respawn_phase_ = 0.0;    // seconds into the current respawn cycle
  double player_hp_ = 0.0;        // remaining player HP, topped up on a beat
  double hit_phase_ = 0.0;        // seconds into the engaged mob's next hit
  // Seconds each buff has left standing, and seconds until each can be put up
  // again -- both parallel to params.buffs. They keep running across a change
  // of map, unlike the fight's own clocks: a buff belongs to the character
  // rather than to the mobs in front of them.
  std::vector<double> buff_left_;
  std::vector<double> buff_cooldown_left_;
  // Which buffs are standing, as the bitmask CombatParams indexes its damage
  // tables by. Worked out once a step, at the top.
  int buff_mask_ = 0;
  // Seconds left before a passive will revive the player again. Counts down
  // wherever the character is, since what it measures is the pact rather than
  // the fight, and stays at 0 for everyone who holds no such skill.
  double revive_left_ = 0.0;
  // Seconds into each auto-attack's next cast, parallel to
  // params.auto_attacks. Runs only while there is something to hit.
  std::vector<double> auto_phase_;
  // Swings credited toward each triggered attack's next cast, parallel to
  // params.triggered_attacks. Fractional, since a swing can be worth less than
  // a whole one.
  std::vector<double> trigger_count_;
  // Swings landed with each attack since its last empowered one, parallel to
  // params.attacks. Stays at 0 for every attack that has no empowered form,
  // which is all of them but the Sniper's Piercing Arrow.
  std::vector<int> empowered_count_;
  // The same count on the other clock, parallel to params.auto_attacks: pulses
  // a summon has fired since its last empowered one. The F/P Mage's Creeping
  // Toxin is the one that has any.
  std::vector<int> auto_empowered_count_;
  // Seconds each swing has left before it can be chosen again, parallel to
  // params.attacks. 0 for a swing that is ready, which is all of them for a
  // character holding no cooldown skill.
  std::vector<double> cooldown_left_;

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
  std::vector<int64_t> kills_this_step_;
  bool died_this_step_ = false;
  std::vector<EngagedGroup> engaged_groups_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
