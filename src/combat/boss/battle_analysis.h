/* Battle Analysis: measures what a stretch of farming actually earns.
 *
 * The player starts it, farms, and stops it. It reports the damage, kills, meso
 * and EXP over that time, and the rate of each. Rates are per real second or
 * hour, the time the player actually spends; the game runs slower than GMS by a
 * factor that grows with level (see GameSpeedFactor).
 *
 * Measurement starts and stops on a respawn. Starting mid-cycle would count
 * mobs that were partly cleared already, and stopping mid-cycle would count a
 * cycle only partly finished, so it waits for a respawn at each end.
 *
 * It is fed one tick at a time and has no clock of its own. When the caller
 * stops feeding it, such as on the boss screen, the measurement pauses too.
 * Nothing here is saved.
 */
#ifndef MS_SRC_COMBAT_BATTLE_ANALYSIS_H_
#define MS_SRC_COMBAT_BATTLE_ANALYSIS_H_

#include <cstdint>

namespace ms {

// The tool's state, shown in its status row.
enum class AnalysisState {
  kStopped,
  kWaitingToStart,
  kRunning,
  kWaitingToStop,
};

// One tick of the fight. `seconds` is real time; the rest is what the tick
// produced.
struct AnalysisSample {
  double seconds = 0.0;
  bool respawned = false;  // true on the tick a respawn happened
  double damage = 0.0;
  int64_t kills = 0;
  int64_t meso = 0;
  int64_t exp = 0;
};

class BattleAnalysis {
 public:
  // Arms the tool. The next respawn starts measuring and clears the last
  // result. If a stop is pending, this cancels it and measuring continues, so
  // an accidental stop costs nothing.
  void Start();
  // Stops measuring at the next respawn. If measuring hasn't started yet, this
  // turns the tool off instead.
  void Stop();

  AnalysisState state() const {
    return state_;
  }
  // Whether pressing the button now stops the tool rather than starts it. The
  // button's label comes from this. During a pending stop it reads Start again.
  bool stops_on_press() const {
    return state_ == AnalysisState::kWaitingToStart ||
           state_ == AnalysisState::kRunning;
  }

  // Adds one tick. Ignored while stopped.
  void Advance(const AnalysisSample& sample);

  double seconds() const {
    return seconds_;
  }
  // Complete respawn cycles measured. The respawn that started measuring
  // doesn't count as one.
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

  // Rates, rounded to whole numbers. All 0 until some time has been measured.
  int64_t damage_per_second() const;
  int64_t kills_per_hour() const;
  int64_t meso_per_hour() const;
  int64_t exp_per_hour() const;

 private:
  // Clears the totals without changing the state.
  void Reset();
  // `total` per hour of measured time, or 0 if none.
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
