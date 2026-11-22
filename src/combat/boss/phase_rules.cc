#include "src/combat/boss/phase_rules.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

namespace {

const Spawn* SpawnOf(const BossPhase& phase,
                     const std::vector<int>& slot_spawns, int slot) {
  if (slot < 0 || slot >= static_cast<int>(slot_spawns.size())) {
    return nullptr;
  }
  int index = slot_spawns[slot];
  return index >= 0 && index < phase.spawns_size() ? &phase.spawns(index)
                                                   : nullptr;
}

// Interval boundaries passed in (from, to].
int TicksBetween(int interval_ms, double from, double to) {
  if (interval_ms <= 0 || to <= from) {
    return 0;
  }
  double interval = interval_ms / 1000.0;
  return static_cast<int>(std::floor(to / interval) -
                          std::floor(std::max(0.0, from) / interval));
}

}  // namespace

bool HasSlotRules(const BossPhase& phase) {
  for (const Spawn& spawn : phase.spawns()) {
    if (spawn.zone() != 0 || spawn.has_invulnerable() ||
        !spawn.hp_steps().empty() || spawn.has_shield() ||
        spawn.entry_invulnerable_ms() > 0 || spawn.has_guard()) {
      return true;
    }
  }
  return false;
}

int ZoneOfSpot(const BossPhase& phase, int spot) {
  for (int z = 0; z < phase.zones_size(); ++z) {
    const PhaseZone& zone = phase.zones(z);
    if (std::find(zone.spots().begin(), zone.spots().end(), spot) !=
        zone.spots().end()) {
      return z + 1;
    }
  }
  return 0;
}

bool Invulnerable(const Spawn& spawn, double fight_seconds,
                  double phase_began) {
  if (fight_seconds - phase_began < spawn.entry_invulnerable_ms() / 1000.0) {
    return true;
  }
  const SpawnWindow& window = spawn.invulnerable();
  if (window.interval_ms() <= 0 || window.open_ms() <= 0 ||
      fight_seconds < 0.0) {
    return false;
  }
  // Open from each interval mark, so the first comes one interval in.
  double interval = window.interval_ms() / 1000.0;
  double into = std::fmod(fight_seconds, interval);
  return fight_seconds >= interval && into < window.open_ms() / 1000.0;
}

void AdvanceGuards(const BossPhase& phase, const std::vector<int>& slot_spawns,
                   const std::vector<double>& hp_fractions,
                   double fight_seconds, std::vector<GuardState>& guards) {
  guards.resize(slot_spawns.size());
  for (int slot = 0; slot < static_cast<int>(slot_spawns.size()); ++slot) {
    const Spawn* spawn = SpawnOf(phase, slot_spawns, slot);
    if (spawn == nullptr || !spawn->has_guard()) {
      continue;
    }
    const SpawnGuard& guard = spawn->guard();
    GuardState& state = guards[slot];
    const double interval = guard.interval_ms() / 1000.0;
    if (interval > 0.0) {
      double next =
          state.opened_at < 0.0 ? interval : state.opened_at + interval;
      while (next <= fight_seconds) {
        state.opened_at = next;
        next += interval;
      }
    }
    if (slot >= static_cast<int>(hp_fractions.size())) {
      continue;
    }
    while (state.hp_opens_spent < guard.hp_opens_size() &&
           hp_fractions[slot] * 100.0 <= guard.hp_opens(state.hp_opens_spent)) {
      if (!GuardOpen(guard, state.opened_at, fight_seconds)) {
        state.opened_at = fight_seconds;
      }
      ++state.hp_opens_spent;
    }
  }
}

bool GuardOpen(const SpawnGuard& guard, double opened_at,
               double fight_seconds) {
  return opened_at >= 0.0 && fight_seconds >= opened_at &&
         fight_seconds - opened_at < guard.open_ms() / 1000.0;
}

double PhaseGuardOpenedAt(const BossPhase& phase,
                          const std::vector<int>& slot_spawns,
                          const std::vector<GuardState>& guards) {
  for (int slot = 0; slot < static_cast<int>(slot_spawns.size()); ++slot) {
    const Spawn* spawn = SpawnOf(phase, slot_spawns, slot);
    if (spawn != nullptr && spawn->has_guard()) {
      return slot < static_cast<int>(guards.size()) ? guards[slot].opened_at
                                                    : -1.0;
    }
  }
  return -1.0;
}

double HpFloor(const BossPhase& phase, const std::vector<int>& slot_spawns,
               const std::vector<bool>& alive, int slot) {
  const Spawn* spawn = SpawnOf(phase, slot_spawns, slot);
  if (spawn == nullptr || spawn->hp_steps().empty()) {
    return 0.0;
  }
  int dead = 0;
  for (int other = 0; other < static_cast<int>(alive.size()); ++other) {
    dead += other != slot && !alive[other];
  }
  return dead < spawn->hp_steps_size() ? spawn->hp_steps(dead) / 100.0 : 0.0;
}

std::vector<SlotRule> SlotRules(const BossPhase& phase,
                                const std::vector<int>& slot_spawns,
                                const std::vector<bool>& alive,
                                const std::vector<GuardState>& guards,
                                double fight_seconds, double phase_began,
                                int spot, const std::vector<int>& spots) {
  const int zone = ZoneOfSpot(phase, spot);
  int crowd = 0;
  for (int other : spots) {
    crowd += zone != 0 && ZoneOfSpot(phase, other) == zone;
  }
  const ZoneCrowding& crowding = phase.crowding();
  const bool crowded = crowding.players() > 0 && crowd >= crowding.players();
  std::vector<SlotRule> rules(slot_spawns.size());
  for (int slot = 0; slot < static_cast<int>(rules.size()); ++slot) {
    const Spawn* spawn = SpawnOf(phase, slot_spawns, slot);
    if (spawn == nullptr) {
      continue;
    }
    SlotRule& rule = rules[slot];
    rule.reachable = (spawn->zone() == 0 || spawn->zone() == zone) &&
                     !Invulnerable(*spawn, fight_seconds, phase_began);
    if (crowded && spawn->zone() != 0) {
      rule.damage_taken = crowding.damage_pct() / 100.0;
    }
    const double opened_at =
        slot < static_cast<int>(guards.size()) ? guards[slot].opened_at : -1.0;
    if (spawn->has_guard() &&
        !GuardOpen(spawn->guard(), opened_at, fight_seconds)) {
      rule.damage_taken *= spawn->guard().damage_pct() / 100.0;
    }
    rule.hp_floor = HpFloor(phase, slot_spawns, alive, slot);
  }
  return rules;
}

std::vector<double> ShieldsBetween(const BossPhase& phase,
                                   const std::vector<int>& slot_spawns,
                                   const std::vector<bool>& alive, double from,
                                   double to) {
  std::vector<double> gained(slot_spawns.size(), 0.0);
  for (int giver = 0; giver < static_cast<int>(slot_spawns.size()); ++giver) {
    const Spawn* spawn = SpawnOf(phase, slot_spawns, giver);
    if (spawn == nullptr || giver >= static_cast<int>(alive.size()) ||
        !alive[giver]) {
      continue;
    }
    const SpawnShield& shield = spawn->shield();
    int ticks = TicksBetween(shield.interval_ms(), from, to);
    if (ticks <= 0 || shield.percent() <= 0) {
      continue;
    }
    for (int slot = 0; slot < static_cast<int>(slot_spawns.size()); ++slot) {
      const Spawn* taker = SpawnOf(phase, slot_spawns, slot);
      if (slot == giver || taker == nullptr ||
          slot >= static_cast<int>(alive.size()) || !alive[slot] ||
          std::find(shield.mobs().begin(), shield.mobs().end(), taker->mob()) ==
              shield.mobs().end()) {
        continue;
      }
      gained[slot] += ticks * shield.percent() / 100.0;
    }
  }
  return gained;
}

}  // namespace ms
