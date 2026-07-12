#include "src/combat/idle_rate.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

double KillCycleSeconds(double damage_per_hit, double swing_interval_seconds,
                        const Mob& mob, double respawn_interval_seconds) {
  if (damage_per_hit <= 0.0) {
    return std::numeric_limits<double>::infinity();  // Never killed.
  }
  // Discrete hits: overkill on the last swing is wasted, no overflow to the
  // next mob.
  double hits_to_kill = std::ceil(mob.max_hp() / damage_per_hit);
  double kill_time = hits_to_kill * swing_interval_seconds;
  // Respawns land only on tick boundaries, so a kill that spills past a tick
  // wastes the rest of it: round up to a whole number of ticks.
  double ticks = std::ceil(kill_time / respawn_interval_seconds);
  return ticks * respawn_interval_seconds * kGameSpeedFactor;
}

std::vector<double> MapKillPeriods(const OffenseStats& offense,
                                   const EquipPrototype& weapon,
                                   const std::vector<const Mob*>& mobs,
                                   int spawn_count,
                                   double respawn_interval_seconds) {
  double swing_interval = SwingIntervalSeconds(
      BaseAttackDelayMs(weapon.equip_type()), weapon.attack_speed());
  double slots_per_type =
      mobs.empty() ? 0.0 : static_cast<double>(spawn_count) / mobs.size();

  std::vector<double> periods;
  periods.reserve(mobs.size());
  for (const Mob* mob : mobs) {
    double cycle =
        KillCycleSeconds(ExpectedAttackDamage(offense, *mob), swing_interval,
                         *mob, respawn_interval_seconds);
    // The type's slots cycle in parallel, so the map-level period shrinks by
    // the slot share; zero slots (or an unkillable mob) leave it at +inf.
    periods.push_back(slots_per_type > 0.0
                          ? cycle / slots_per_type
                          : std::numeric_limits<double>::infinity());
  }
  return periods;
}

int64_t FlushKills(double period_seconds, double elapsed_seconds,
                   double* accumulator) {
  if (!std::isfinite(period_seconds) || period_seconds <= 0.0) {
    return 0;
  }
  *accumulator += elapsed_seconds / period_seconds;
  double whole = std::floor(*accumulator);
  *accumulator -= whole;
  return static_cast<int64_t>(whole);
}

}  // namespace ms
