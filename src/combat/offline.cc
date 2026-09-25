#include "src/combat/offline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "src/character/consumables.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

// Fraction of the sample skipped before reading HP. A character logs off at
// full HP and settles to the map's usual level; readings during that settling
// describe the drop, not the map.
constexpr double kOfflineWarmupFraction = 0.25;

// Close enough to full HP to count as full. HP that returns to full after the
// warm-up is not draining.
constexpr double kOfflineFullPool = 0.999;

// How much larger than the HP's normal noise a fitted decline must be to count.
// HP always fluctuates, so a fitted line always has some slope; below this, it
// is just noise.
constexpr double kOfflineTrendNoiseMultiple = 2.0;

// Minimum HP fraction to be paid beyond the sample. A player who gets this
// close to dying in ten minutes can't hold the map for hours: the same dip will
// recur.
constexpr double kOfflineTroughFloor = 0.10;

// Tracks the player's HP over a sample to decide whether the map can be held.
// Readings are added as the fight runs; the verdict comes at the end.
//
// HP fluctuates on every map, so the question is whether it falls by more than
// the noise. This fits a line over the whole sample and compares the fall to
// the scatter. Two readings or a single low point are too noisy to decide.
class PoolTrend {
 public:
  explicit PoolTrend(double fit_from_seconds) : fit_from_(fit_from_seconds) {
  }

  void Add(double seconds, double fraction) {
    // Track the lowest HP over the whole sample, warm-up included: a near-death
    // is a fact about the map.
    trough_ = std::min(trough_, fraction);
    if (seconds < fit_from_) {
      return;  // still sliding out of the pool they logged off with
    }
    refilled_ = refilled_ || fraction >= kOfflineFullPool;
    ++readings_;
    sum_t_ += seconds;
    sum_p_ += fraction;
    sum_tt_ += seconds * seconds;
    sum_tp_ += seconds * fraction;
    sum_pp_ += fraction * fraction;
    last_seconds_ = seconds;
  }

  // Seconds until HP runs out, counted from the end of the sample. Infinite if
  // the sample shows no drain; zero if the player barely survived.
  double SecondsUntilDry() const {
    if (trough_ <= kOfflineTroughFloor) {
      return 0.0;
    }
    if (refilled_ || readings_ < 3) {
      return std::numeric_limits<double>::infinity();
    }
    double n = readings_;
    double tt = sum_tt_ - sum_t_ * sum_t_ / n;
    double tp = sum_tp_ - sum_t_ * sum_p_ / n;
    double pp = sum_pp_ - sum_p_ * sum_p_ / n;
    if (tt <= 0.0) {
      return std::numeric_limits<double>::infinity();
    }
    double slope = tp / tt;  // share of the pool per second
    if (slope >= 0.0) {
      return std::numeric_limits<double>::infinity();
    }
    double fall = -slope * (last_seconds_ - fit_from_);
    double scatter = std::sqrt(std::max(0.0, pp - slope * tp) / n);
    if (fall < kOfflineTrendNoiseMultiple * scatter) {
      return std::numeric_limits<double>::infinity();
    }
    // Use the fitted line, not the last reading, which could be at either end
    // of a swing.
    double level = (sum_p_ - slope * sum_t_) / n + slope * last_seconds_;
    return std::max(0.0, level / -slope);
  }

 private:
  double fit_from_ = 0.0;
  double trough_ = 1.0;
  bool refilled_ = false;
  double readings_ = 0.0;
  double last_seconds_ = 0.0;
  double sum_t_ = 0.0;
  double sum_p_ = 0.0;
  double sum_tt_ = 0.0;
  double sum_tp_ = 0.0;
  double sum_pp_ = 0.0;
};

// The result of one simulated sample.
struct Sample {
  explicit Sample(double fit_from_seconds) : pool(fit_from_seconds) {
  }

  double seconds = 0.0;  // how much was actually stepped
  std::vector<int64_t> kills;
  bool died = false;
  PoolTrend pool;
};

// Runs a fresh fight on `params` for `seconds`, stopping early if the player
// dies.
Sample StepSample(const CombatParams& params, double seconds) {
  Sample sample(seconds * kOfflineWarmupFraction);
  sample.kills.assign(params.types.size(), 0);
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < seconds;
       elapsed += kOfflineStepSeconds) {
    sim.Advance(params, kOfflineStepSeconds);
    for (std::size_t i = 0; i < sample.kills.size(); ++i) {
      sample.kills[i] += sim.view().kills_this_step[i];
    }
    sample.seconds = elapsed + kOfflineStepSeconds;
    sample.pool.Add(sample.seconds, sim.view().player_hp_fraction);
    if (sim.view().died_this_step) {
      sample.died = true;
      return sample;
    }
  }
  return sample;
}

}  // namespace

double AbsenceSeconds(int64_t last_seen_unix_seconds,
                      int64_t now_unix_seconds) {
  if (last_seen_unix_seconds <= 0) {
    return 0.0;
  }
  return std::max<double>(
      0.0, static_cast<double>(now_unix_seconds - last_seen_unix_seconds));
}

OfflineReport ApplyOfflineProgress(GameState& state, double seconds) {
  OfflineReport report;
  report.absence = std::max(0.0, seconds);
  report.start_level = state.character.proto().level();
  report.end_level = report.start_level;
  if (seconds <= 0.0) {
    return report;
  }
  CombatParams params = ComputeCombatParams(state);
  if (!params.active) {
    return report;  // nothing to farm: no map, no weapon, or no mobs
  }
  std::map<std::string, MapData>::const_iterator map =
      state.maps.find(state.current_map);
  if (map != state.maps.end()) {
    report.map_name = map->second.name();
  }
  report.farmed = true;

  Sample sample = StepSample(params, std::min(seconds, kOfflineSampleSeconds));
  report.seconds = sample.seconds;
  std::vector<int64_t> kills = sample.kills;

  if (sample.died) {
    report.died = true;
  } else if (seconds > sample.seconds && sample.seconds > 0.0) {
    // Scale the sample to the rest of the absence, but only for as long as HP
    // lasts. A character slowly losing the map farms until HP runs out, then
    // dies.
    double left =
        std::min(seconds - sample.seconds, sample.pool.SecondsUntilDry());
    if (left < seconds - sample.seconds) {
      report.died = true;
    }
    for (std::size_t i = 0; i < kills.size(); ++i) {
      kills[i] += static_cast<int64_t>(static_cast<double>(sample.kills[i]) /
                                       sample.seconds * left);
    }
    report.seconds += left;
  }

  for (int64_t killed : kills) {
    report.kills += killed;
  }
  report.rewards = AwardCombatRewards(state, params, kills);
  // Buffs and potions run the whole time, like when the player watches.
  // report.seconds covers all farming, cut short only by death.
  report.rewards.consumable_cost =
      state.character.ChargeFarmingConsumables(report.seconds);
  report.end_level = state.character.proto().level();
  if (report.died) {
    // Same penalty as a live death: sent home, keeping what was farmed.
    state.current_map = kHomeMap;
  }
  return report;
}

}  // namespace ms
