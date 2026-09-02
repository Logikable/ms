#include "src/combat/offline.h"

#include <algorithm>
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
// slide rather than the map -- so the first half of a sample always looks
// healthier than the second, and every map reads as draining.
constexpr double kOfflineWarmupFraction = 0.25;

// Close enough to a full pool to count as one. A character whose pool comes
// back to the top after the warm-up is not draining, whatever the dips between
// do -- so this is asked before any trend is fitted.
constexpr double kOfflineFullPool = 0.999;

// What one stepped sample of the fight came to.
struct Sample {
  double seconds = 0.0;  // how much was actually stepped
  std::vector<int64_t> kills;
  bool died = false;
  // The lowest the pool fell in each half of the stretch past the warm-up, as
  // a fraction. The trough rather than the last reading, because HP swings
  // within every beat: it drops to the mob's hits and comes back on the
  // respawn.
  double first_trough = 1.0;
  double second_trough = 1.0;
  // Whether the pool came back to full after the warm-up.
  bool refilled = false;
};

// Steps a cold fight through `seconds` on `params`, stopping early if the
// player dies.
Sample StepSample(const CombatParams& params, double seconds) {
  Sample sample;
  sample.kills.assign(params.types.size(), 0);
  CombatSim sim;
  double warmup = seconds * kOfflineWarmupFraction;
  double half = warmup + (seconds - warmup) / 2.0;
  for (double elapsed = 0.0; elapsed < seconds;
       elapsed += kOfflineStepSeconds) {
    sim.Advance(params, kOfflineStepSeconds);
    for (std::size_t i = 0; i < sample.kills.size(); ++i) {
      sample.kills[i] += sim.kills_this_step()[i];
    }
    sample.seconds = elapsed + kOfflineStepSeconds;
    if (elapsed >= warmup) {
      double& trough =
          elapsed < half ? sample.first_trough : sample.second_trough;
      trough = std::min(trough, sim.player_hp_fraction());
      sample.refilled =
          sample.refilled || sim.player_hp_fraction() >= kOfflineFullPool;
    }
    if (sim.died_this_step()) {
      sample.died = true;
      return sample;
    }
  }
  return sample;
}

// Seconds the character has left before the pool runs out, projected from how
// far the trough fell between the two halves of the sample past its warm-up.
// Infinite for a character whose pool held or recovered, which is every map
// they can farm indefinitely.
double SecondsUntilDry(const Sample& sample) {
  if (sample.refilled) {
    return std::numeric_limits<double>::infinity();
  }
  double measured = sample.seconds * (1.0 - kOfflineWarmupFraction);
  double drop = sample.first_trough - sample.second_trough;
  if (drop <= 0.0 || measured <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  // The troughs are half the measured stretch apart, so that is what the drop
  // was taken over. What is left of the pool is the second trough.
  double per_second = drop / (measured / 2.0);
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
