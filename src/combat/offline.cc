#include "src/combat/offline.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

// What one stepped sample of the fight came to.
struct Sample {
  double seconds = 0.0;  // how much was actually stepped
  std::vector<int64_t> kills;
  bool died = false;
  // The lowest the pool fell in each half of the sample, as a fraction. The
  // trough rather than the last reading, because HP swings within every beat:
  // it drops to the mob's hits and comes back on the respawn. Two troughs a
  // half-sample apart are what say whether the character is holding.
  double first_trough = 1.0;
  double second_trough = 1.0;
};

// Steps a cold fight through `seconds` on `params`, stopping early if the
// player dies.
Sample StepSample(const CombatParams& params, double seconds) {
  Sample sample;
  sample.kills.assign(params.types.size(), 0);
  CombatSim sim;
  double half = seconds / 2.0;
  for (double elapsed = 0.0; elapsed < seconds;
       elapsed += kOfflineStepSeconds) {
    sim.Advance(params, kOfflineStepSeconds);
    for (std::size_t i = 0; i < sample.kills.size(); ++i) {
      sample.kills[i] += sim.kills_this_step()[i];
    }
    sample.seconds = elapsed + kOfflineStepSeconds;
    double& trough =
        elapsed < half ? sample.first_trough : sample.second_trough;
    trough = std::min(trough, sim.player_hp_fraction());
    if (sim.died_this_step()) {
      sample.died = true;
      return sample;
    }
  }
  return sample;
}

// Seconds the character has left before the pool runs out, projected from how
// far the trough fell between the two halves of `sample`. Infinite for a
// character whose pool held or recovered, which is every map they can farm
// indefinitely.
double SecondsUntilDry(const Sample& sample) {
  double drop = sample.first_trough - sample.second_trough;
  if (drop <= 0.0 || sample.seconds <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  // The troughs are half a sample apart, so that is the stretch the drop was
  // taken over. What is left of the pool is the second trough.
  double per_second = drop / (sample.seconds / 2.0);
  return sample.second_trough / per_second;
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
    double left = std::min(seconds - sample.seconds, SecondsUntilDry(sample));
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
  report.end_level = state.character.proto().level();
  if (report.died) {
    // The same price the live fight charges: the trip home and nothing else.
    // What was farmed before the fall stands.
    state.current_map = kHomeMap;
  }
  return report;
}

}  // namespace ms
