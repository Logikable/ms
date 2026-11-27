#include "src/combat/measure.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "src/combat/constants.h"
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

void ToCycleShare(const CombatParams& params, Sequence* played) {
  const double seconds = played->seconds;
  if (seconds <= 0.0) {
    return;
  }
  double removed = 0.0;
  double long_damage = 0.0;  // everything the long attacks dealt, uncut
  double busy = 0.0;         // seconds the long attacks' casts took
  double cast_cut = 0.0;     // of those, the ones a long fight wouldn't spend
  for (int i = 0; i < static_cast<int>(played->by_attack.size()) &&
                  i < static_cast<int>(params.attacks.size());
       ++i) {
    const AttackOption& attack = params.attacks[i];
    AttackTally& tally = played->by_attack[i];
    if (attack.cooldown_seconds <= seconds || tally.swings == 0) {
      continue;
    }
    const double kept = seconds / attack.cooldown_seconds;
    long_damage += tally.damage;
    double cut = tally.damage * (1.0 - kept);
    tally.damage -= cut;
    for (std::pair<std::string, double>& source : played->own_clock_by_source) {
      if (source.first == attack.name) {
        long_damage += source.second;
        const double off = source.second * (1.0 - kept);
        source.second -= off;
        played->own_clock_damage -= off;
        cut += off;
      }
    }
    removed += cut;
    busy += tally.swings * attack.swing_seconds;
    cast_cut += tally.swings * attack.swing_seconds * (1.0 - kept);
  }
  if (removed <= 0.0) {
    return;
  }
  // The rest of the run's rate, over the time the long casts didn't take.
  const double rest = played->damage - long_damage;
  const double free_seconds = std::max(1e-9, seconds - busy);
  const double back = cast_cut * rest / free_seconds;
  if (played->main_attack >= 0) {
    played->by_attack[played->main_attack].damage += back;
  }
  played->damage += back - removed;
}

namespace {

double DealtSoFar(const CombatSim& sim) {
  double dealt = sim.own_clock_damage();
  for (const AttackTally& tally : sim.by_attack()) {
    dealt += tally.damage;
  }
  return dealt;
}

// Advances `sim` by `seconds`.
void RunFor(CombatSim& sim, const CombatParams& params, double seconds) {
  double elapsed = 0.0;
  while (elapsed < seconds) {
    const double step =
        std::min(std::max(sim.SecondsToNextEvent(params), kLeastStep),
                 seconds - elapsed);
    sim.Advance(params, step);
    elapsed += step;
  }
}

}  // namespace

double MeasureAscents(const CombatParams& params, double horizon, double held) {
  if (!params.active || params.types.empty() || horizon <= 0.0 ||
      absl::c_none_of(params.attacks,
                      [](const AttackOption& a) { return a.held; })) {
    return 0.0;
  }
  const CombatParams measured = AsMeasurement(params, 1);
  // As a boss run lets them out at 75, 50 and 25% HP: the first two wait out
  // a burst, the last goes at once.
  CombatSim released;
  released.Advance(measured, 0.0);
  double elapsed = 0.0;
  for (int use = 1; use <= kAscentUsesPerBossFight; ++use) {
    const double at = horizon * use / (kAscentUsesPerBossFight + 1);
    RunFor(released, measured, at - elapsed);
    elapsed = at;
    released.Release(1, /*at_once=*/use == kAscentUsesPerBossFight);
  }
  RunFor(released, measured, horizon - elapsed);
  return DealtSoFar(released) - held;
}

double WithAscents(double dps, double body, double ascents, double time_limit) {
  if (ascents == 0.0 || dps <= 0.0 || body <= 0.0) {
    return dps;
  }
  if (time_limit > 0.0 && body / dps > time_limit) {
    return dps + ascents / time_limit;
  }
  return body / (std::max(0.0, body - ascents) / dps);
}

}  // namespace ms
