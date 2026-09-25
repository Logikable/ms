/* One attempt at a boss: its phases in order, the time limit, and the pauses
 * between phases.
 *
 * The fight uses the same CombatSim as farming, so bosses get the same damage
 * model, skill choice and buffs; only the encounter differs. Nothing respawns,
 * nothing hits back, and it runs in real time. See ComputeBossParams.
 *
 * A run keeps no game state of its own. It reads the character from the
 * GameState it is stepped with and pays through the same reward code as
 * farming, so boss rewards stay consistent with mob rewards.
 */
#ifndef MS_SRC_COMBAT_BOSS_RUN_H_
#define MS_SRC_COMBAT_BOSS_RUN_H_

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/combat/boss_timing.h"
#include "src/combat/damage_breakdown.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/fight_authority.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

namespace ms {

// How long a dead monster's bar stays on screen. Its slot is never refilled,
// since the player saw it die there.
inline constexpr double kBossDeathHoldSeconds = 1.0;

// How long a stack of damage numbers stays on screen.
inline constexpr double kDamageStackSeconds = 0.9;
// How long each hit in a stack shows before the next replaces it. One frame of
// a boss fight, so a twelve-slash attack flashes through all twelve.
inline constexpr double kDamageStrikeSeconds = 0.03;
// Maximum stacks kept at once, so the list can't grow forever if drawing falls
// behind. The oldest are dropped first.
inline constexpr int kMaxDamageStacks = 64;
// Same limit for the local player's own damage writes, which are one per hit:
// one attack across ten bars makes a write per slash per bar.
inline constexpr int kMaxDamageWrites = 512;

// One damage number.
struct DamageNumber {
  int64_t damage = 0;
  bool crit = false;
};

// What one attack from one party member dealt to one monster, in order. Drawn
// bottom-up, so a cramped corner cuts off the top rather than the whole stack.
// Not used for the local player, whose numbers go into rows above the monster
// (see DamageWrite).
struct DamageStack {
  // The monster hit, by its slot's permanent ID.
  int mob_id = 0;
  // Who dealt it, as an index into the run's members. Never 0 (the local
  // player); other players' numbers are drawn dim.
  int owner = 0;
  // What dealt it. A monster holds at most one stack per player per source; a
  // new landing replaces that source's old stack.
  DamageSource source;
  std::vector<DamageNumber> lines;
  // Where each hit begins in `lines`: one entry for a single hit, twelve for
  // twelve slashes. Never empty while `lines` isn't. Drawers use StrikeAt.
  std::vector<int> strike_starts;
  // Seconds on screen, in real time: it is an animation, so game speed doesn't
  // apply.
  double age = 0.0;
  // The half-open range of `lines` showing at `age`. Each hit replaces the last
  // every kDamageStrikeSeconds, and the last one stays until the end.
  std::pair<int, int> StrikeAt(double age) const;
  // The most lines in any one hit. The drawer reserves this much space, so a
  // flashing stack doesn't change shape.
  int TallestStrike() const;

  // Which side of the bar to try first. Picked once when the stack is made;
  // picking per frame would make it jump around.
  int preference = 0;
};

// One hit of the local player's damage, written into the rows above the
// monster: line i goes to row i, counting up from the bar. A write owns rows,
// not a block, so a one-line attack after a fifteen-line one replaces only the
// bottom row and leaves the other fourteen.
struct DamageWrite {
  // The monster hit, by its slot's permanent ID.
  int mob_id = 0;
  std::vector<DamageNumber> lines;
  // Delay after the attack before this hit shows. Twelve slashes make twelve
  // writes at once, each kDamageStrikeSeconds after the last.
  double delay = 0.0;
  // Seconds since the attack landed, in real time: it is an animation, so game
  // speed doesn't apply.
  double age = 0.0;

  // Seconds this write has been showing; negative until its delay passes.
  double showing() const {
    return age - delay;
  }
  // Whether it's on screen: its delay has passed and it hasn't faded.
  bool live() const {
    return showing() >= 0.0 && showing() < kDamageStackSeconds;
  }
};

// One row of the numbers above a monster. Empty if nothing was written there or
// its number faded.
struct DamageRow {
  bool filled = false;
  DamageNumber number;
};

// The numbers above `mob_id`, row 0 nearest the bar. Each row shows the newest
// live write that reaches it, so a tall attack keeps its upper rows while
// shorter ones change below. As tall as the tallest live write; callers with
// less room draw what fits.
std::vector<DamageRow> DamageColumn(const std::vector<DamageWrite>& writes,
                                    int mob_id);

// One line of a clear's reward: an item's display name and how many reached the
// bag.
struct BossRewardItem {
  std::string name;
  int64_t count = 0;
  // From DropIsPrize: gear, or a token that buys gear. The reward card lists
  // these separately from the regular clear rewards.
  bool prize = false;
  // The table's drop rate for it, before any drop rate bonus. The card sorts by
  // this, so the rarest drop is listed first in its group.
  double chance = 0.0;
};

// What a cleared fight actually paid, not what the table offers. A drop can
// miss its roll, and a full bag can lose one that hit.
struct BossReward {
  int64_t meso = 0;
  int64_t exp = 0;
  // Only paid by bosses with a daily or weekly limit. A boss the player can
  // fight as often as they like doesn't give it.
  int64_t honor = 0;
  std::vector<BossRewardItem> items;
};

// One monster's bar. Created when the phase starts and never reused: a dead
// monster's slot fades and then empties, so the bars beside it never shift.
struct BossSlot {
  int id = 0;
  std::string name;
  // Its position. It starts where the phase spawned it and stays there, unless
  // it has a walk.
  int x = 0;
  int y = 0;
  // How it wanders. An unset walk means it stands still.
  ArenaWalk walk;
  // How many moves of its walk it has made; each new move follows from these.
  // Stored so each move costs one move's work instead of replaying thousands.
  int steps_taken = 0;
  // When the next move and dash are due, in run seconds. Set from the walk when
  // the slot is created, so a monster arriving in a later phase follows the
  // time already spent.
  double next_move_at = 0.0;
  double next_dash_at = 0.0;
  // Cells left in the current dash, and its direction.
  int dash_left = 0;
  int dash_dx = 0;
  // A jump: the row it left, and when it lands. Walking pauses until it lands,
  // so it always lands where it jumped from.
  bool airborne = false;
  int ground_y = 0;
  double next_jump_at = 0.0;
  double land_at = 0.0;
  double hp_fraction = 0.0;
  bool alive = true;
  // False once a dead bar's display time runs out. The slot stays in the list
  // as an empty gap.
  bool visible = true;
  // Seconds since this monster died, for how long its empty bar stays up.
  double dead_for = 0.0;
};

// One player in a fight as the arena draws them: where they stand and what they
// are charging. Players whose client has disconnected aren't included.
struct FightMember {
  // Empty for the local player, who is always first.
  std::string name;
  // Which of the phase's player spots they stand on, or -1 if the phase has
  // none.
  int spot = -1;
  std::string attack_name;
  double attack_fraction = 0.0;
  // Number of active buffs.
  int buff_count = 0;
};

// Which of `phase`'s player spots a key press moves to. Picks the nearest spot
// in the pressed direction, measured along that direction and then across it; a
// spot further across than along doesn't count as that direction. Returns
// `from` when no spot lies that way or two spots tie. Spots in `taken` are
// skipped, and the search continues past them.
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy,
                   const std::vector<int>& taken);
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy);

class BossRun {
 public:
  // `boss` and `authority` must outlive the run. An invalid `difficulty_index`
  // makes a run that is already over. A null `authority` means a solo fight
  // that tracks its own phases, timer and monster HP. A `practice` run pays
  // nothing, and the caller doesn't record a clear for it.
  BossRun(std::string boss_key, const Boss& boss, int difficulty_index,
          FightAuthority* authority = nullptr, bool practice = false);

  // Advances the run by `elapsed_seconds` of real time, paying the character
  // for anything that died. Does nothing once the run is finished.
  void Advance(GameState& state, double elapsed_seconds);
  // Gives up the run. The screen returns immediately instead of pausing, since
  // the player chose to leave.
  void Abort();
  // Moves the player one spot in the pressed direction. Does nothing once the
  // fight is over, or if the phase has nowhere else to stand.
  void MovePlayer(int dx, int dy);

  BossRunState state() const {
    return state_;
  }
  // True once the run has finished and its end pause has passed: time for the
  // screen to return.
  bool done() const;
  // True if every phase was cleared, even during the end pause.
  bool won() const {
    return state_ == BossRunState::kWon;
  }
  // The fight's name, e.g. "Normal Zakum".
  const std::string& title() const {
    return title_;
  }
  // Whether this is a practice run, which pays nothing and uses no clear. The
  // caller checks this before recording a clear.
  bool practice() const {
    return practice_;
  }
  // The boss's name without the difficulty, used in the prompt when leaving.
  const std::string& boss_name() const {
    return boss_name_;
  }
  // The current phase, starting from 1.
  int phase() const {
    return phase_ + 1;
  }
  // This phase's music track, or the last one a phase set: a phase without one
  // keeps the previous track playing.
  std::string_view bgm() const;
  int phase_count() const {
    return phases_;
  }
  // What the fight paid. Empty until it is won.
  const BossReward& reward() const {
    return reward_;
  }
  // How long the clear took, from the end of the countdown to the last
  // monster's death, including phase gaps. 0 until it is won.
  double clear_seconds() const {
    return clear_seconds_;
  }
  // This player's damage by skill, and the seconds of actual fighting it covers
  // (excluding the countdown and phase gaps).
  const DamageBreakdown& breakdown() const {
    return breakdown_;
  }
  // Every player's rows, this player's first with no name. Other players' rows
  // arrive when a party fight ends, as each last reported them.
  std::vector<PlayerBreakdown> breakdowns() const;
  // The current phase's remaining HP as a fraction of its starting total. Every
  // monster in the phase counts, so eight arms at half HP reads 50%.
  double phase_hp_fraction() const {
    return phase_hp_fraction_;
  }
  // Seconds left on the fight's timer. Starts when the countdown ends and stops
  // when the fight does.
  double seconds_left() const {
    return seconds_left_;
  }
  // Seconds left in the countdown before the fight starts. 0 once it has
  // started.
  double countdown_left() const {
    return countdown_left_;
  }
  // Time since the run started, countdown included. For arena animations that
  // don't depend on the fight.
  double elapsed_seconds() const {
    return elapsed_seconds_;
  }
  // Every bar in the current phase, in spawn order.
  const std::vector<BossSlot>& slots() const {
    return slots_;
  }
  // Other players' damage numbers still on screen, oldest first. Never the
  // local player's, which are drawn as rows instead.
  const std::vector<DamageStack>& damage_stacks() const {
    return damage_stacks_;
  }
  // The local player's damage numbers still on screen, oldest first. Use
  // DamageColumn to get a monster's rows.
  const std::vector<DamageWrite>& damage_writes() const {
    return damage_writes_;
  }
  // Everyone in the fight, local player first. Just one for a solo fight.
  const std::vector<FightMember>& members() const {
    return members_;
  }
  // How many ways the reward is split: everyone in the fight when it began, or
  // 1 for a solo fight.
  int share_count() const {
    return share_count_;
  }
  // Where the player has moved to in this phase, and the arena's size in cells.
  // If the phase doesn't set a size, it is measured from the spots, with no
  // margin.
  ArenaSpot player_spot() const;
  // Every spot the player can stand on this phase, including their current one.
  // Empty if the phase has none.
  std::vector<ArenaSpot> player_spots() const;
  int arena_width() const;
  int arena_height() const;
  // The attack being charged and its progress, for the player's charge bar.
  const std::string& attack_name() const {
    return sim_.view().attack_name;
  }
  double attack_fraction() const {
    return sim_.view().attack_fraction;
  }

 private:
  const BossDifficulty* difficulty() const;
  // The damage table for the current phase, rebuilt only when an input changes.
  // See params_.
  const CombatParams& PhaseParams(const GameState& state);
  // The current phase, or null once the run is over.
  const BossPhase* current_phase() const;
  // Ages every damage number on screen by `dt` and removes expired ones.
  void AgeDamageNumbers(double dt);
  // Turns the local player's latest damage into writes, one per hit per
  // monster.
  void CollectDamageWrites();
  // Shows `stack`, replacing whatever that member's same source last left on
  // the same monster.
  void Replace(DamageStack stack);
  // Creates a bar for each monster in a new phase, at its spawn position.
  void FillSlots(const CombatParams& params);
  // Updates the bars from the fight's mob list: living monsters keep their bar,
  // and dead ones start fading in place.
  void SyncSlots(double dt);
  // Moves walking monsters to where they should be at the current run time.
  // Called after the slots are synced, in both solo and shared fights.
  void DriftSlots();
  // Walks, dashes and jumps `slot` up to `elapsed` seconds into the run, one
  // move at a time.
  void DriftSlot(const BossPhase& phase, BossSlot& slot, double elapsed);
  // When `slot`'s next move is due: the next cell of its current dash, or else
  // the sooner of its next step and next dash.
  static double NextMoveAt(const BossSlot& slot);
  // Makes whichever move `slot` has due, and schedules the next one.
  void MoveSlot(const BossPhase& phase, BossSlot& slot);
  // Moves `slot` one step along its walk. Leaves it in place if the walk has
  // nowhere to go.
  void StepSlot(const BossPhase& phase, BossSlot& slot);
  // Moves `slot` one cell along its dash. Returns false if it can't enter that
  // cell, which ends the dash.
  bool DashSlot(const BossPhase& phase, BossSlot& slot);
  // When `slot`'s next jump event is due: landing if it's in the air, the next
  // jump if it's on the ground. kNeverMoves if it never jumps.
  static double NextJumpAt(const BossSlot& slot);
  // Starts or ends `slot`'s jump, and schedules the other half.
  static void JumpSlot(BossSlot& slot);
  // Computes the phase's remaining HP as a fraction of its full HP.
  void ComputePhaseHp(const CombatParams& params);
  // Advances one phase of the fight, moving on when all its monsters are dead.
  void RunPhase(GameState& state, double dt);
  // Pays the difficulty's reward once for a clear and records what was
  // received. Meso is divided by share_count_; EXP is paid in full to everyone.
  // `awards` holds the drops.
  void PayReward(GameState& state, const std::vector<SharedAward>& awards);
  // Rolls drops for a solo fight, each against this character's item drop rate.
  // In a party the server rolls them, so a guaranteed drop is guaranteed and a
  // single drop goes to exactly one player.
  std::vector<SharedAward> RollAwards(GameState& state,
                                      double item_drop_pct) const;
  // One step of a party fight: read the server's state, simulate local attacks,
  // report the damage, and draw everyone else's.
  void AdvanceShared(GameState& state, double dt);
  // Copies the phase, timer and player positions from the server.
  void TakeShared(const SharedFight& shared);
  // Runs the local fight against the shared monsters: this player's attacks
  // land, monster HP is capped to the server's values, and the damage is
  // reported.
  void RunSharedPhase(GameState& state, double dt, const SharedFight& shared);
  // Sends this run's damage to the party. Uses the network interval rather than
  // the faster screen refresh.
  void ReportToParty(double dt);
  // Draws other players' damage. Lines on a monster this client has already
  // removed are dropped, since there's nowhere to draw them.
  void AddSharedStacks(const std::vector<SharedLine>& lines);
  // Advances a solo fight, with its own countdown, phases and timer.
  void RunAlone(GameState& state, double dt);
  // Puts the local player first in the members, so their stacks have owner 0.
  void StandSelf();
  // Spots other players are standing on, which movement skips over.
  std::vector<int> TakenSpots() const;
  // Ends the run with `outcome`, then pauses before returning, unless the run
  // was aborted.
  void Finish(BossRunState outcome);
  // Puts the player on the phase's first spot. Each phase is a new arena, so
  // the previous position doesn't carry over.
  void StandPlayerAtStart();

  std::string boss_key_;
  const Boss* boss_ = nullptr;
  int difficulty_index_ = 0;
  std::string title_;
  bool practice_ = false;
  std::string boss_name_;
  int phases_ = 0;

  BossRunState state_ = BossRunState::kCountdown;
  int phase_ = 0;
  double countdown_left_ = kBossCountdownSeconds;
  double seconds_left_ = 0.0;
  double clear_seconds_ = 0.0;
  DamageBreakdown breakdown_;
  // Party damage breakdowns from the server, and this player's account ID.
  std::vector<PlayerBreakdown> shared_breakdowns_;
  std::string self_account_;
  double elapsed_seconds_ = 0.0;
  // Seconds left in the current pause: between phases, or at the end before the
  // screen returns.
  double hold_left_ = 0.0;
  double phase_hp_fraction_ = 0.0;
  // The phase's damage table, and the phase and level it was built for. Nothing
  // that feeds it changes within a phase, and rebuilding it every frame took
  // 94% of a daily-boss sim's time. Level is tracked too, since a clear pays
  // EXP while the last phase is still running.
  CombatParams params_;
  int params_phase_ = -1;
  int params_level_ = 0;
  // Which of the phase's player spots the player is on. -1 if the phase has
  // none; the player then stands at the origin and can't move.
  int player_at_ = -1;
  std::vector<BossSlot> slots_;
  std::vector<DamageStack> damage_stacks_;
  std::vector<DamageWrite> damage_writes_;
  // The server for a party fight, or null for a solo fight.
  FightAuthority* authority_ = nullptr;
  std::vector<FightMember> members_;
  int share_count_ = 1;
  // Maps between mob IDs and slots. Mob IDs are local to this client; slot
  // numbers are the same on every client.
  std::map<int, int> slot_of_mob_;
  std::vector<int> mob_of_slot_;
  // Which member each server player is. The local player is always first here,
  // while the server lists players in party order.
  std::vector<int> member_of_player_;
  // Damage dealt since the last report. Rounded the same way as the numbers on
  // screen, so the server's HP matches what players saw.
  std::vector<SharedLine> landed_;
  // Seconds until the next report. 0 sends on the next step, which is how a
  // fight and each new phase start.
  double report_due_ = 0.0;
  // The Drop preset's rate from the last step, sent to the server for the
  // party's best-rate rule. Kept for a clear that happens on a step with no new
  // value.
  double item_drop_pct_ = 0.0;
  // Chooses which side of a bar each stack prefers. Uses the default seed, so a
  // run plays out the same way every time and tests can predict placement.
  std::mt19937 rng_;
  BossReward reward_;
  CombatSim sim_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_RUN_H_
