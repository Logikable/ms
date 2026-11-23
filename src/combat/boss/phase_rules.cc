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
        spawn.entry_invulnerable_ms() > 0 || spawn.has_guard() ||
        spawn.has_self_shield()) {
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
  const double since = fight_seconds - phase_began;
  if (window.interval_ms() <= 0 || window.open_ms() <= 0 || since < 0.0) {
    return false;
  }
  // Open from each interval mark, so the first comes one interval in.
  double interval = window.interval_ms() / 1000.0;
  double into = std::fmod(since, interval);
  return since >= interval && into < window.open_ms() / 1000.0;
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

GuardTimes GuardTimesAt(const SpawnGuard& guard, double opened_at,
                        double fight_seconds) {
  GuardTimes times;
  times.cut = guard.damage_pct() / 100.0;
  times.open_seconds = guard.open_ms() / 1000.0;
  times.interval_seconds = guard.interval_ms() / 1000.0;
  if (GuardOpen(guard, opened_at, fight_seconds)) {
    times.open_left = opened_at + times.open_seconds - fight_seconds;
  }
  if (times.interval_seconds > 0.0) {
    double next = times.interval_seconds;
    if (opened_at >= 0.0) {
      next = opened_at + times.interval_seconds;
    }
    times.opens_in = std::max(0.0, next - fight_seconds);
  }
  return times;
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

std::vector<ShieldSpan> SelfShieldSpans(const BossPhase& phase,
                                        const SelfShield& shield,
                                        double phase_began, double from,
                                        double to) {
  std::vector<ShieldSpan> spans;
  double anchor = 0.0;
  double period = 0.0;
  double offset = 0.0;
  double length = 0.0;
  // The first span: one interval in for a clock, the first time round for a
  // form.
  int first = 0;
  if (!shield.form().empty()) {
    double cycle = 0.0;
    bool found = false;
    for (const PhaseForm& form : phase.forms()) {
      if (!found && form.name() == shield.form()) {
        offset = cycle;
        length = std::max(0, form.seconds());
        found = true;
      }
      cycle += std::max(0, form.seconds());
    }
    if (!found || cycle <= 0.0 || length <= 0.0) {
      return spans;
    }
    anchor = phase_began;
    period = cycle;
  } else {
    if (shield.interval_ms() <= 0 || shield.duration_ms() <= 0) {
      return spans;
    }
    period = shield.interval_ms() / 1000.0;
    length = shield.duration_ms() / 1000.0;
    first = 1;
  }
  int k = std::max(
      first,
      static_cast<int>(std::floor((from - anchor - offset - length) / period)));
  for (;; ++k) {
    ShieldSpan span;
    span.start = anchor + offset + k * period;
    span.end = span.start + length;
    if (span.start > to) {
      break;
    }
    if (span.end >= from) {
      spans.push_back(span);
    }
  }
  return spans;
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
                                const std::vector<double>& shields_left,
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
    if (spawn->has_guard()) {
      rule.guard = GuardTimesAt(spawn->guard(), opened_at, fight_seconds);
      if (!GuardOpen(spawn->guard(), opened_at, fight_seconds)) {
        rule.damage_taken *= rule.guard.cut;
      }
    }
    if (spawn->self_shield().damage_pct() > 0 &&
        slot < static_cast<int>(shields_left.size()) &&
        shields_left[slot] > 0.0) {
      rule.damage_taken *= spawn->self_shield().damage_pct() / 100.0;
    }
    rule.hp_floor = HpFloor(phase, slot_spawns, alive, slot);
  }
  return rules;
}

std::vector<double> ShieldsBetween(const BossPhase& phase,
                                   const std::vector<int>& slot_spawns,
                                   const std::vector<bool>& alive,
                                   const std::vector<double>& shields_left,
                                   double phase_began, double from, double to) {
  std::vector<double> gained(slot_spawns.size(), 0.0);
  for (int slot = 0; slot < static_cast<int>(slot_spawns.size()); ++slot) {
    const Spawn* spawn = SpawnOf(phase, slot_spawns, slot);
    if (spawn == nullptr || !spawn->has_self_shield() ||
        slot >= static_cast<int>(alive.size()) || !alive[slot] || to <= from) {
      continue;
    }
    const SelfShield& shield = spawn->self_shield();
    bool holds = slot < static_cast<int>(shields_left.size()) &&
                 shields_left[slot] > 0.0;
    for (const ShieldSpan& span :
         SelfShieldSpans(phase, shield, phase_began, from, to)) {
      if (span.start > from && span.start <= to) {
        gained[slot] += shield.percent() / 100.0;
        holds = true;
      }
      double overlap = std::min(to, span.end) - std::max(from, span.start);
      if (holds && overlap > 0.0) {
        gained[slot] += overlap * shield.percent_per_second() / 100.0;
      }
    }
  }
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

std::vector<bool> SelfShieldsEnding(const BossPhase& phase,
                                    const std::vector<int>& slot_spawns,
                                    double phase_began, double from,
                                    double to) {
  std::vector<bool> ending(slot_spawns.size(), false);
  for (int slot = 0; slot < static_cast<int>(slot_spawns.size()); ++slot) {
    const Spawn* spawn = SpawnOf(phase, slot_spawns, slot);
    if (spawn == nullptr || !spawn->has_self_shield() || to <= from) {
      continue;
    }
    for (const ShieldSpan& span :
         SelfShieldSpans(phase, spawn->self_shield(), phase_began, from, to)) {
      ending[slot] = ending[slot] || (span.end > from && span.end <= to);
    }
  }
  return ending;
}

}  // namespace ms
