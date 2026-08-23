/* Battle Analysis: what a stretch of farming is actually worth, measured
 * rather than predicted.
 *
 * The player starts it, farms, and stops it. What comes back is the damage,
 * the kills, the meso and the EXP of that stretch, and the rate each of them
 * came in at. Every rate is per real second or per real hour: the game runs
 * slower than GMS by a factor that grows with the level (see GameSpeedFactor),
 * and the tool reports the clock the player is actually sitting at.
 *
 * Both ends land on a respawn beat. A measurement that started mid-cycle would
 * count the tail of a roster somebody else's swing had already cleared, and one
 * that stopped mid-cycle would count the head of a roster it never finished --
 * so the tool waits for the beat at each end and covers whole cycles.
 *
 * It is fed one tick at a time and holds no clock of its own. A caller that
 * stops feeding it -- the boss screen, where the map is not being farmed --
 * stops the measurement's clock with it. Nothing here is saved: a measurement
 * belongs to the session the player took it in.
 */
#ifndef MS_SRC_COMBAT_BATTLE_ANALYSIS_H_
#define MS_SRC_COMBAT_BATTLE_ANALYSIS_H_

#include <cstdint>

namespace ms {

// What the tool is doing, and what its status row says.
enum class AnalysisState {
  kStopped,
  kWaitingToStart,
  kRunning,
  kWaitingToStop,
};

// One tick of the fight, as the tool is told about it. The seconds are real
// ones; everything else is what that tick produced.
struct AnalysisSample {
  double seconds = 0.0;
  bool respawned = false;  // a respawn beat came round on this tick
  double damage = 0.0;
  int64_t kills = 0;
  int64_t meso = 0;
  int64_t exp = 0;
};

class BattleAnalysis {
 public:
  // Arms the tool. The next beat starts the measurement, and clears whatever
  // the last one left behind.
  void Start();
  // Stops it on the next beat. Pressed before the first one has arrived, it
  // puts the tool away instead: there is no measurement to round off.
  void Stop();

  AnalysisState state() const {
    return state_;
  }
  // Whether the entry the player presses reads Stop rather than Start.
  bool started() const {
    return state_ != AnalysisState::kStopped;
  }

  // Folds one tick in. Ignored while the tool is stopped.
  void Advance(const AnalysisSample& sample);

  double seconds() const {
    return seconds_;
  }
  // Respawn cycles the measurement has covered. The beat that started it is
  // not one of them: a cycle is the ground between two beats.
  int64_t cycles() const {
    return cycles_;
  }
  int64_t damage() const;
  int64_t kills() const {
    return kills_;
  }
  int64_t meso() const {
    return meso_;
  }
  int64_t exp() const {
    return exp_;
  }

  // The rates, rounded to whole numbers. All 0 until the measurement has time
  // in it, which is what a panel drawing it before the first beat shows.
  int64_t damage_per_second() const;
  int64_t kills_per_hour() const;
  int64_t meso_per_hour() const;
  int64_t exp_per_hour() const;

 private:
  // Empties the totals, leaving the state alone.
  void Reset();
  // `total` spread over the measurement's hours, or 0 with no time in it.
  int64_t PerHour(double total) const;

  AnalysisState state_ = AnalysisState::kStopped;
  double seconds_ = 0.0;
  int64_t cycles_ = 0;
  double damage_ = 0.0;
  int64_t kills_ = 0;
  int64_t meso_ = 0;
  int64_t exp_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_BATTLE_ANALYSIS_H_
