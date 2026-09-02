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

// The opening of a sample, as a share of it, that is thrown away before the
// pool is read at all. A character logs off full and slides into whatever band
// the map holds them in, and a reading taken across that slide describes the
// slide rather than the map.
constexpr double kOfflineWarmupFraction = 0.25;

// Close enough to a full pool to count as one. A character whose pool comes
// back to the top after the warm-up is not draining, whatever the dips between
// do -- so this is asked before any trend is fitted.
constexpr double kOfflineFullPool = 0.999;

// How far a fitted fall must stand clear of the pool's own scatter to be
// believed, as a multiple of it. The pool swings on every map -- it drops to
// the mobs' hits and comes back on the respawn -- so a line through it always
// has some slope, and below this the line is describing that swing.
constexpr double kOfflineTrendNoiseMultiple = 2.0;

// The share of the pool a character has to hold to be credited past the
// sample. One that came this close to empty in ten minutes is not holding the
// map: over an absence of hours the same dip comes round many times, and one
// of them lands on a beat that finishes them.
constexpr double kOfflineTroughFloor = 0.10;

// The player's pool over a sample, and what it says about whether the map can
// be held. Readings go in as the fight is stepped; the question is asked once
// at the end.
//
// The pool swings on every map, so the question is never whether one reading
// sits below another but whether there is a fall here bigger than the swing.
// That is a line fitted to the whole sample, weighed against its own scatter
// -- two readings a few minutes apart cannot tell the two apart, and a
// minimum, which is what a trough is, is the noisiest reading there is.
class PoolTrend {
 public:
  explicit PoolTrend(double fit_from_seconds) : fit_from_(fit_from_seconds) {
  }

  void Add(double seconds, double fraction) {
    // The trough is taken over the whole sample: how close the character came
    // to dying is a fact about the map, not about the stretch of it fitted.
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

  // Seconds the character has left before the pool runs out, from the end of
  // the sample. Infinite for a pool the sample cannot show draining, which is
  // every map they can farm indefinitely; zero for one they only just held.
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
    // Off the line rather than off the last reading, which is one swing of the
    // pool and could be either end of it.
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

// What one stepped sample of the fight came to.
struct Sample {
  explicit Sample(double fit_from_seconds) : pool(fit_from_seconds) {
  }

  double seconds = 0.0;  // how much was actually stepped
  std::vector<int64_t> kills;
  bool died = false;
  PoolTrend pool;
};

// Steps a cold fight through `seconds` on `params`, stopping early if the
// player dies.
Sample StepSample(const CombatParams& params, double seconds) {
  Sample sample(seconds * kOfflineWarmupFraction);
  sample.kills.assign(params.types.size(), 0);
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < seconds;
       elapsed += kOfflineStepSeconds) {
    sim.Advance(params, kOfflineStepSeconds);
    for (std::size_t i = 0; i < sample.kills.size(); ++i) {
      sample.kills[i] += sim.kills_this_step()[i];
    }
    sample.seconds = elapsed + kOfflineStepSeconds;
    sample.pool.Add(sample.seconds, sim.player_hp_fraction());
    if (sim.died_this_step()) {
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
    // Past the sample the rest of the absence is scaled from it -- but only as
    // far as the pool lasts. A character who is slowly losing the map farms
    // until it runs out and then falls, whatever is left of the absence.
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
  // The potion drank through the absence exactly as it drinks through a
  // watched evening: the character was farming the whole of report.seconds,
  // which stops early only where they fell.
  report.rewards.consumable_cost = state.character.ChargeConsumable(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, report.seconds);
  report.end_level = state.character.proto().level();
  if (report.died) {
    // The same price the live fight charges: the trip home and nothing else.
    // What was farmed before the fall stands.
    state.current_map = kHomeMap;
  }
  return report;
}

}  // namespace ms
