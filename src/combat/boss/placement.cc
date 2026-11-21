#include "src/combat/boss/placement.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <vector>

#include "src/protos/boss.pb.h"

namespace ms {

namespace {

constexpr int kPerZone = 2;

bool Living(const std::vector<bool>& alive, int zone) {
  return zone >= 1 && zone <= static_cast<int>(alive.size()) && alive[zone - 1];
}

// The first living zone with `room` places left under `cap`, or 0.
int RoomFor(const std::vector<bool>& alive, const std::vector<int>& cap,
            const std::vector<int>& held, int room) {
  for (int z = 1; z <= static_cast<int>(alive.size()); ++z) {
    if (alive[z - 1] && cap[z - 1] - held[z - 1] >= room) {
      return z;
    }
  }
  return 0;
}

// How many each zone gets when spread: one at a time to the emptiest, ties
// to the zone more players already stand in and then the earlier zone, so as
// few move as can. As a phase starts the first zone fills first.
std::vector<int> SpreadShares(const std::vector<bool>& alive,
                              const std::vector<int>& standing, int players,
                              bool fresh) {
  std::vector<int> share(alive.size(), 0);
  if (fresh && !alive.empty() && alive[0]) {
    share[0] = std::min(kPerZone, players);
    players -= share[0];
  }
  for (; players > 0; --players) {
    int best = -1;
    for (int z = 0; z < static_cast<int>(alive.size()); ++z) {
      if (!alive[z] || share[z] >= kPerZone) {
        continue;
      }
      if (best < 0 || share[z] < share[best] ||
          (share[z] == share[best] && standing[z] > standing[best])) {
        best = z;
      }
    }
    if (best < 0) {
      break;
    }
    ++share[best];
  }
  return share;
}

}  // namespace

std::vector<bool> ZonesAlive(const BossPhase& phase,
                             const std::vector<int>& slot_spawns,
                             const std::vector<bool>& alive) {
  std::vector<bool> zones(phase.zones_size(), false);
  for (int slot = 0; slot < static_cast<int>(slot_spawns.size()) &&
                     slot < static_cast<int>(alive.size());
       ++slot) {
    int spawn = slot_spawns[slot];
    if (!alive[slot] || spawn < 0 || spawn >= phase.spawns_size()) {
      continue;
    }
    int zone = phase.spawns(spawn).zone();
    if (zone >= 1 && zone <= phase.zones_size()) {
      zones[zone - 1] = true;
    }
  }
  return zones;
}

std::vector<int> PlacePlayers(ZonePlacement mode,
                              const std::vector<bool>& alive,
                              const std::vector<int>& current,
                              std::mt19937& rng) {
  const int players = static_cast<int>(current.size());
  const int zones = static_cast<int>(alive.size());
  std::vector<int> placed = current;
  if (mode == ZONE_PLACEMENT_UNSPECIFIED || zones == 0) {
    return placed;
  }
  const bool fresh = std::all_of(current.begin(), current.end(),
                                 [](int zone) { return zone == 0; });
  std::vector<int> order(players);
  std::iota(order.begin(), order.end(), 0);
  if (fresh) {
    std::shuffle(order.begin(), order.end(), rng);
  }
  std::vector<int> standing(zones, 0);
  for (int zone : current) {
    if (Living(alive, zone)) {
      ++standing[zone - 1];
    }
  }
  const std::vector<int> cap =
      mode == ZONE_PLACEMENT_SPREAD
          ? SpreadShares(alive, standing, players, fresh)
          : std::vector<int>(zones, kPerZone);
  // Whoever is within their zone's share stays.
  std::vector<int> held(zones, 0);
  std::map<int, std::vector<int>> movers_from;
  for (int player : order) {
    int zone = current[player];
    if (Living(alive, zone) && held[zone - 1] < cap[zone - 1]) {
      ++held[zone - 1];
    } else {
      movers_from[zone].push_back(player);
    }
  }
  // Movers from one zone go on together where a zone has room for both.
  for (const auto& [from, movers] : movers_from) {
    for (std::size_t i = 0; i < movers.size(); i += kPerZone) {
      std::size_t end = std::min(movers.size(), i + kPerZone);
      int group = static_cast<int>(end - i);
      int together =
          mode == ZONE_PLACEMENT_PAIRS ? RoomFor(alive, cap, held, group) : 0;
      for (std::size_t k = i; k < end; ++k) {
        int zone = together != 0 ? together : RoomFor(alive, cap, held, 1);
        if (zone == 0) {
          // Nowhere with room: wait where nothing is left to hit.
          zone = from;
          if (Living(alive, zone) || zone == 0) {
            std::vector<bool>::const_iterator dead =
                std::find(alive.begin(), alive.end(), false);
            zone = dead == alive.end()
                       ? 0
                       : static_cast<int>(dead - alive.begin()) + 1;
          }
        } else {
          ++held[zone - 1];
        }
        placed[movers[k]] = zone;
      }
    }
  }
  return placed;
}

int FreeSpotInZone(const BossPhase& phase, int zone,
                   const std::vector<int>& taken) {
  if (zone < 1 || zone > phase.zones_size()) {
    return -1;
  }
  for (int spot : phase.zones(zone - 1).spots()) {
    if (std::find(taken.begin(), taken.end(), spot) == taken.end()) {
      return spot;
    }
  }
  return -1;
}

}  // namespace ms
