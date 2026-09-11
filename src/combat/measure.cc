#include "src/combat/measure.h"

#include <algorithm>
#include <vector>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"

namespace ms {
namespace {

// Never advance by less than this. Nothing in a fight is this short; it is
// here so a clock reading zero cannot stall the loop.
constexpr double kLeastStep = 1e-6;

// The params as a measurement wants them: one kind of monster, as many of them
// as the question asks for, and nothing hitting back.
//
// One kind because every damage table is read off the type a swing lands on,
// and a crowd standing in for a map is one number rather than a roster. Type 0
// is what the caller's own first type is, so a boss is measured against the
// part the fight opens on.
CombatParams AsMeasurement(const CombatParams& params, int enemies) {
  CombatParams measured = params;
  measured.measuring = true;
  measured.record_damage_lines = false;
  // What is asked is the rate, not whether the character lives through it.
  measured.hit_seconds = 0.0;
  measured.types.resize(1);
  measured.types[0].simultaneous = std::max(1, enemies);
  return measured;
}

}  // namespace

Sequence MeasureFight(const CombatParams& params, double horizon, int enemies) {
  Sequence played;
  played.damage_by_attack.assign(params.attacks.size(), 0.0);
  played.buff_uptime.assign(params.buffs.size(), 0.0);
  if (!params.active || params.types.empty() || params.attacks.empty() ||
      horizon <= 0.0) {
    return played;
  }
  CombatParams measured = AsMeasurement(params, enemies);

  CombatSim sim;
  // Fills the queue, aims the first swing and raises whatever stands from the
  // off, before anything reads them -- the step below is sized to the swing
  // being wound up, and there is none until this has run.
  sim.Advance(measured, 0.0);
  for (double elapsed = 0.0; elapsed < horizon;) {
    // Straight to the next thing that can change what a swing is worth, rather
    // than a hundred steps of winding clocks between one swing and the next.
    double step =
        std::min(std::max(sim.SecondsToNextEvent(measured), kLeastStep),
                 horizon - elapsed);
    sim.Advance(measured, step);
    elapsed += step;
    played.seconds = elapsed;
    // Read after the step, the buffs having been run at the top of it: a step
    // ends on every edge one of them has, so none of it is spent half up.
    int mask = sim.buff_mask();
    for (int i = 0; i < static_cast<int>(played.buff_uptime.size()); ++i) {
      if ((mask >> i) & 1) {
        played.buff_uptime[i] += step;
      }
    }
  }

  const std::vector<double>& dealt = sim.damage_by_attack();
  const std::vector<int>& swings = sim.swings_by_attack();
  for (int i = 0; i < static_cast<int>(played.damage_by_attack.size()) &&
                  i < static_cast<int>(dealt.size());
       ++i) {
    played.damage_by_attack[i] = dealt[i];
    played.damage += dealt[i];
  }
  played.own_clock_damage = sim.own_clock_damage();
  played.damage += played.own_clock_damage;
  for (int i = 0; i < static_cast<int>(swings.size()); ++i) {
    if (swings[i] > 0 &&
        (played.main_attack < 0 || swings[i] > swings[played.main_attack])) {
      played.main_attack = i;
    }
  }
  for (double& share : played.buff_uptime) {
    share /= horizon;
  }
  return played;
}

}  // namespace ms
