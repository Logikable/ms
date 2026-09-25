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

// Minimum step size. Nothing in a fight is this short; it only prevents a timer
// at zero from stalling the loop.
constexpr double kLeastStep = 1e-6;

// Turns `params` into a measurement: one monster type, `enemies` of them, and
// nothing hitting back. Only one type because damage tables are per type. Type
// 0 is the caller's first, so a boss is measured against its opening phase.
CombatParams AsMeasurement(const CombatParams& params, int enemies) {
  CombatParams measured = params;
  measured.measuring = true;
  measured.record_damage_lines = false;
  // We want the damage rate, not whether the character survives.
  measured.hit_seconds = 0.0;
  measured.types.resize(1);
  measured.types[0].simultaneous = std::max(1, enemies);
  return measured;
}

// Display name for a damage source on its own timer, looked up in the list its
// origin indexes. Side strikes and loads reuse the attack's own skill name.
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
      // Index -1 is reflected damage, which has no skill behind it.
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
      // Burns are credited to the attack that applied them, so only burns from
      // own-timer sources reach here.
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
  // Advance by zero first to fill the queue, pick the first target and apply
  // opening buffs; the step size below depends on that first attack.
  sim.Advance(measured, 0.0);
  for (double elapsed = 0.0; elapsed < horizon;) {
    // Jump straight to the next event that can change damage, instead of many
    // small steps between attacks.
    double step =
        std::min(std::max(sim.SecondsToNextEvent(measured), kLeastStep),
                 horizon - elapsed);
    sim.Advance(measured, step);
    elapsed += step;
    played.seconds = elapsed;
    // Buffs update at the start of each step, and steps end at every buff edge,
    // so a buff is either up or down for the whole step.
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
