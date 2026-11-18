#include "src/combat/clear_time.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"

namespace ms {

std::vector<double> ClearTimes(CombatParams params, double seconds,
                               double step) {
  if (!params.active) {
    return {};
  }
  params.respawn_on_clear = true;
  CombatSim sim;
  std::vector<double> times;
  double started = 0.0;
  double t = 0.0;
  for (; t < seconds; t += step) {
    sim.Advance(params, step);
    if (sim.view().died_this_step) {
      return {seconds};
    }
    // The opening fill isn't a respawn, so the first clear runs from 0.
    if (sim.view().respawned_this_step) {
      times.push_back(t - started);
      started = t;
    }
  }
  double running = t - started;
  if (times.empty() ||
      running > *std::max_element(times.begin(), times.end())) {
    times.push_back(running);
  }
  return times;
}

double ClearPercentile(std::vector<double> times, double fraction) {
  if (times.empty()) {
    return 0.0;
  }
  std::sort(times.begin(), times.end());
  int rank = static_cast<int>(std::ceil(fraction * times.size()));
  return times[std::clamp(rank - 1, 0, static_cast<int>(times.size()) - 1)];
}

}  // namespace ms
