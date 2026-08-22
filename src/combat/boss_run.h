/* One run at one boss: the phases in order, the clock they are fought
 * against, and the beats between them.
 *
 * The fight itself is the same CombatSim the farming loop steps, so a boss is
 * hit with the same damage model, the same skill chooser and the same buffs --
 * what differs is the encounter it is handed. Nothing respawns, nothing hits
 * back, and it all runs in real time; see ComputeBossParams.
 *
 * A run holds no game state of its own. It reads the character out of the
 * GameState it is stepped with and pays them through the same reward path a
 * kill on a map goes through, so what a boss is worth cannot drift from what a
 * monster is worth.
 */
#ifndef MS_SRC_COMBAT_BOSS_RUN_H_
#define MS_SRC_COMBAT_BOSS_RUN_H_

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

namespace ms {

// The pause before the fight starts, so the player can see what they are up
// against before anything moves.
inline constexpr double kBossCountdownSeconds = 3.0;
// How long a dead monster's bar stays on screen. Its slot is not filled again
// -- the player watched it die there.
inline constexpr double kBossDeathHoldSeconds = 1.0;
// The beat between a phase ending and the next arriving.
inline constexpr double kBossPhaseGapSeconds = 2.0;
// How long a finished fight is held before the screen goes back. An abort
// takes no hold at all: the player asked to leave, and there is nothing left
// on screen for them to watch.
inline constexpr double kBossEndHoldSeconds = 1.0;

// How long a stack of damage numbers stays on screen.
inline constexpr double kDamageStackSeconds = 0.9;
// The most stacks held at once. A generous ceiling on a phase of ten bars,
// there so a fight cannot grow the list without bound if one is ever drawn
// slower than the swings arrive. The oldest go first.
inline constexpr int kMaxDamageStacks = 64;

// One number a landing left behind.
struct DamageNumber {
  int64_t damage = 0;
  bool crit = false;
};

// One stack of numbers: what one attack landed on one monster, in the order
// the lines landed. A stack is drawn whole or not at all -- what a cramped
// corner costs is the rows furthest from the monster, not the stack.
struct DamageStack {
  // The slot that took it, by the id a slot keeps for its whole life.
  int mob_id = 0;
  std::vector<DamageNumber> lines;
  // Seconds it has been on screen. Real ones: it is an animation, and the
  // game's pacing band has no business stretching it.
  double age = 0.0;
  // Which side of the bar the arena should try first, drawn when the stack was
  // made. Drawn once rather than per frame, or a stack that has not changed
  // would move every time it was redrawn.
  int preference = 0;
};

// One line of what a clear paid: an item's display name and how many of it
// reached the bag.
struct BossRewardItem {
  std::string name;
  int64_t count = 0;
};

// What a cleared fight paid. What actually landed, not what the table offers:
// a drop can miss its roll, and a full bag loses one that hit, and the card
// the player reads should not claim either of them.
struct BossReward {
  int64_t meso = 0;
  int64_t exp = 0;
  std::vector<BossRewardItem> items;
};

// Where a run is up to. The three at the end are all ways of being finished,
// held apart because the screen says a different thing about each.
enum class BossRunState {
  kCountdown,
  kFighting,
  kPhaseGap,
  kWon,
  kTimedOut,
  kAborted,
};

// One monster's bar. A slot is made when the phase starts and never reused:
// once its monster is dead it fades and then leaves the space empty, so the
// bars beside it never move.
struct BossSlot {
  int id = 0;
  std::string name;
  // Where this one stands, from the phase that spawned it.
  int x = 0;
  int y = 0;
  double hp_fraction = 0.0;
  bool alive = true;
  // False once the dead bar's hold has run out. The slot stays in the list --
  // what it holds is a gap.
  bool visible = true;
  // Seconds since this one died, for the beat its empty bar is held for.
  double dead_for = 0.0;
};

// Which of `phase`'s player spots a press moves to, as an index into
// `player_spots`. The nearest spot strictly that way wins, measured along the
// direction pressed; two the same distance along it are settled by whichever
// is nearer across it. A spot further across the arrow than along it is not
// that way at all and is passed over. `from` is returned when nothing lies
// that way, or when two spots are as good as each other -- a press with no one
// answer moves nobody.
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy);

class BossRun {
 public:
  // `boss` is owned by the GameState and must outlive the run. `difficulty` is
  // an index into its difficulties; an invalid one makes a run that is over
  // before it starts.
  BossRun(std::string boss_key, const Boss& boss, int difficulty_index);

  // Steps the run by elapsed_seconds of real time, paying the character for
  // whatever died. Does nothing once the run is finished.
  void Advance(GameState& state, double elapsed_seconds);
  // Gives up the run. The screen goes straight back rather than holding a
  // beat: the player asked to leave.
  void Abort();
  // Walks the player one spot in the direction pressed, which is one of the
  // four unit vectors. Does nothing once the fight is over, or in a phase that
  // named nowhere else to stand.
  void MovePlayer(int dx, int dy);

  BossRunState state() const {
    return state_;
  }
  // True once the run has finished AND its closing beat has been held: the
  // moment the screen should go back.
  bool done() const;
  // True for a run that cleared every phase, whether or not the beat is up.
  bool won() const {
    return state_ == BossRunState::kWon;
  }
  // What the fight is called: "Normal Zakum".
  const std::string& title() const {
    return title_;
  }
  // The boss alone, without its difficulty. What the player is asked about on
  // the way out: they are leaving Zakum, and which Zakum it was is not the
  // question.
  const std::string& boss_name() const {
    return boss_name_;
  }
  // Which phase is being fought, counting from 1.
  int phase() const {
    return phase_ + 1;
  }
  int phase_count() const {
    return phases_;
  }
  // What the fight paid. Empty until it is won.
  const BossReward& reward() const {
    return reward_;
  }
  // What is left of the current phase, over what it started with. Every
  // monster in the phase counts toward it, so eight arms at half HP reads 50%.
  double phase_hp_fraction() const {
    return phase_hp_fraction_;
  }
  // Seconds left on the fight's clock. Runs from the moment the countdown
  // ends, and stops when the fight does.
  double seconds_left() const {
    return seconds_left_;
  }
  // Seconds left of the pause before the fight starts. 0 once it has.
  double countdown_left() const {
    return countdown_left_;
  }
  // Every bar of the current phase, in the order they were spawned.
  const std::vector<BossSlot>& slots() const {
    return slots_;
  }
  // The damage numbers still on screen, oldest first.
  const std::vector<DamageStack>& damage_stacks() const {
    return damage_stacks_;
  }
  // Where the player stands in the current phase -- where they have walked
  // to, not where the phase put them -- and how many cells the arena holds
  // around everyone. Both are measured off the spots when the
  // phase names neither, which leaves the arena no margin.
  ArenaSpot player_spot() const;
  // Everywhere the player may stand this phase, the spot they are on
  // included. Empty for a phase that named none.
  std::vector<ArenaSpot> player_spots() const;
  int arena_width() const;
  int arena_height() const;
  // The swing being charged and how far along it is, for the player's bar.
  const std::string& attack_name() const {
    return sim_.attack_name();
  }
  double attack_fraction() const {
    return sim_.attack_fraction();
  }

 private:
  const BossDifficulty* difficulty() const;
  // The phase being fought, or null once the run is over.
  const BossPhase* current_phase() const;
  // Ages the stacks of numbers by dt and drops the ones whose time is up.
  void AgeDamageStacks(double dt);
  // Turns what the fight just landed into stacks, one per attack per monster.
  void CollectDamageStacks();
  // Rebuilds the bars from the fight's roster: what is still standing keeps
  // its bar, and what has gone starts fading in the slot it held.
  void SyncSlots(double dt);
  // What is left of the phase, over what it holds when full.
  void ComputePhaseHp(const CombatParams& params);
  // Steps one phase of the fight forward, moving on when it empties.
  void RunPhase(GameState& state, double dt);
  // Pays the difficulty's reward table, once, for a fight that was cleared,
  // and records what landed. The drops roll against `item_drop_pct` the way a
  // monster's do; the meso and the EXP are flat.
  void PayReward(GameState& state, double item_drop_pct);
  // Ends the run in `outcome`, holding the screen for the closing beat -- or
  // for nothing at all, if the run was given up.
  void Finish(BossRunState outcome);
  // Stands the player where the current phase starts them. Every phase is its
  // own arena, so where they walked to in the last one means nothing here.
  void StandPlayerAtStart();

  std::string boss_key_;
  const Boss* boss_ = nullptr;
  int difficulty_index_ = 0;
  std::string title_;
  std::string boss_name_;
  int phases_ = 0;

  BossRunState state_ = BossRunState::kCountdown;
  int phase_ = 0;
  double countdown_left_ = kBossCountdownSeconds;
  double seconds_left_ = 0.0;
  // Seconds left of whatever beat is being held: the gap between phases, or
  // the pause at the end before the screen goes back.
  double hold_left_ = 0.0;
  double phase_hp_fraction_ = 0.0;
  // Which of the phase's player spots they stand on. -1 for a phase that named
  // none, whose player never leaves the spot it wrote down.
  int player_at_ = -1;
  std::vector<BossSlot> slots_;
  std::vector<DamageStack> damage_stacks_;
  // Picks which side of a bar each stack asks for. Default-seeded, so a run
  // plays out the same way twice and a test can say where a stack went.
  std::mt19937 rng_;
  BossReward reward_;
  CombatSim sim_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_RUN_H_
