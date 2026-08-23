#include "src/combat/battle_analysis.h"

#include <cmath>
#include <cstdint>

namespace ms {
namespace {

constexpr double kSecondsPerHour = 3600.0;

}  // namespace

void BattleAnalysis::Reset() {
  seconds_ = 0.0;
  cycles_ = 0;
  damage_ = 0.0;
  kills_ = 0;
  meso_ = 0;
  exp_ = 0;
}

void BattleAnalysis::Start() {
  if (state_ == AnalysisState::kWaitingToStop) {
    state_ = AnalysisState::kRunning;  // the stop is taken back
    return;
  }
  if (state_ == AnalysisState::kRunning) {
    return;
  }
  state_ = AnalysisState::kWaitingToStart;
  Reset();
}

void BattleAnalysis::Stop() {
  if (state_ == AnalysisState::kWaitingToStart) {
    state_ = AnalysisState::kStopped;
    return;
  }
  if (state_ == AnalysisState::kRunning) {
    state_ = AnalysisState::kWaitingToStop;
  }
}

void BattleAnalysis::Advance(const AnalysisSample& sample) {
  switch (state_) {
    case AnalysisState::kStopped:
      return;
    case AnalysisState::kWaitingToStart:
      if (!sample.respawned) {
        return;
      }
      // The beat that starts it is the boundary, not a cycle of its own.
      state_ = AnalysisState::kRunning;
      break;
    case AnalysisState::kRunning:
    case AnalysisState::kWaitingToStop:
      if (sample.respawned) {
        ++cycles_;
      }
      break;
  }
  seconds_ += sample.seconds;
  damage_ += sample.damage;
  kills_ += sample.kills;
  meso_ += sample.meso;
  exp_ += sample.exp;
  // The tick that closes it still counts: the beat lands at the top of a tick,
  // and what the rest of that tick paid was earned inside the measurement.
  if (state_ == AnalysisState::kWaitingToStop && sample.respawned) {
    state_ = AnalysisState::kStopped;
  }
}

int64_t BattleAnalysis::damage() const {
  return static_cast<int64_t>(std::llround(damage_));
}

int64_t BattleAnalysis::PerHour(double total) const {
  if (seconds_ <= 0.0) {
    return 0;
  }
  return static_cast<int64_t>(std::llround(total * kSecondsPerHour / seconds_));
}

int64_t BattleAnalysis::damage_per_second() const {
  if (seconds_ <= 0.0) {
    return 0;
  }
  return static_cast<int64_t>(std::llround(damage_ / seconds_));
}

int64_t BattleAnalysis::kills_per_hour() const {
  return PerHour(static_cast<double>(kills_));
}

int64_t BattleAnalysis::meso_per_hour() const {
  return PerHour(static_cast<double>(meso_));
}

int64_t BattleAnalysis::exp_per_hour() const {
  return PerHour(static_cast<double>(exp_));
}

}  // namespace ms
