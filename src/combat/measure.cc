#include "src/combat/measure.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/damage_ledger.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"

namespace ms {
namespace {

// Never advance by less than this. Nothing in a fight is this short; it is
// here so a clock reading zero cannot stall the loop.
constexpr double kLeastStep = 1e-6;

// The params a measurement wants: one kind of monster, as many as the question
// asks, and nothing hitting back. One kind because every damage table is read
// off the type a swing lands on; type 0 is the caller's own first, so a boss
// is measured against the part the fight opens on.
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

// What an own-clock source is called, out of the list its origin indexes into.
// A side strike and a load are the swing's skill striking again.
std::string SourceName(const CombatParams& params, const DamageSource& source) {
  auto named = [&](const std::vector<AttackOption>& list,
                   const char* suffix) -> std::string {
    if (source.index < 0 || source.index >= static_cast<int>(list.size())) {
      return "(unnamed)";
    }
    return list[source.index].name + suffix;
  };
  switch (source.origin) {
    case DamageOrigin::kOwnClock:
      // Index -1 is the reflection, which no cast stands behind.
      return source.index < 0 ? "(reflected)" : named(params.auto_attacks, "");
    case DamageOrigin::kSwingClock:
      return named(params.triggered_attacks, "");
    case DamageOrigin::kKillClock:
      return named(params.triggered_attacks, "");
    case DamageOrigin::kSideStrike:
      return named(params.attacks, " (side strike)");
    case DamageOrigin::kLoad:
      return named(params.attacks, " (load)");
    case DamageOrigin::kBurn:
      // A burn is credited to the swing that lit it wherever one lit it, so
      // only the ones an own clock left ever reach here.
      return "(burn)";
    case DamageOrigin::kSwing:
      break;
  }
  return "(unnamed)";
}

}  // namespace

Sequence MeasureFight(const CombatParams& params, double horizon, int enemies) {
  Sequence played;
  played.by_attack.assign(params.attacks.size(), AttackTally{});
  played.buff_uptime.assign(params.buffs.size(), 0.0);
  if (!params.active || params.types.empty() || params.attacks.empty() ||
      horizon <= 0.0) {
    return played;
  }
  CombatParams measured = AsMeasurement(params, enemies);

  CombatSim sim;
  // Fills the queue, aims the first swing and raises what stands from the
  // off: the step below is sized to a swing that does not exist until now.
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

  const std::vector<AttackTally>& tallies = sim.by_attack();
  for (int i = 0; i < static_cast<int>(played.by_attack.size()) &&
                  i < static_cast<int>(tallies.size());
       ++i) {
    played.by_attack[i] = tallies[i];
    played.damage += tallies[i].damage;
  }
  played.own_clock_damage = sim.own_clock_damage();
  played.damage += played.own_clock_damage;
  for (const std::pair<const DamageSource, double>& source :
       sim.own_clock_by_source()) {
    played.own_clock_by_source.push_back(
        {SourceName(measured, source.first), source.second});
  }
  std::sort(played.own_clock_by_source.begin(),
            played.own_clock_by_source.end(),
            [](const std::pair<std::string, double>& a,
               const std::pair<std::string, double>& b) {
              return a.second > b.second;
            });
  for (int i = 0; i < static_cast<int>(played.by_attack.size()); ++i) {
    if (played.by_attack[i].swings > 0 &&
        (played.main_attack < 0 ||
         played.by_attack[i].swings >
             played.by_attack[played.main_attack].swings)) {
      played.main_attack = i;
    }
  }
  for (double& share : played.buff_uptime) {
    share /= horizon;
  }
  return played;
}

}  // namespace ms
