#include "src/combat/arena_spots.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace ms {

std::vector<ArenaSpot> AllPlayerSpots(const BossPhase& phase) {
  std::vector<ArenaSpot> spots(phase.player_spots().begin(),
                               phase.player_spots().end());
  spots.insert(spots.end(), phase.timed_spots().spots().begin(),
               phase.timed_spots().spots().end());
  return spots;
}

ArenaSpot ArenaSize(const BossPhase& phase) {
  ArenaSpot extent;
  for (const Spawn& spawn : phase.spawns()) {
    for (const ArenaSpot& spot : spawn.spots()) {
      extent.set_x(std::max(extent.x(), spot.x() + 1));
      extent.set_y(std::max(extent.y(), spot.y() + 1));
    }
  }
  for (const ArenaSpot& spot : AllPlayerSpots(phase)) {
    extent.set_x(std::max(extent.x(), spot.x() + 1));
    extent.set_y(std::max(extent.y(), spot.y() + 1));
  }
  if (phase.arena_width() > 0) {
    extent.set_x(phase.arena_width());
  }
  if (phase.arena_height() > 0) {
    extent.set_y(phase.arena_height());
  }
  return extent;
}

bool TimedSpotsOpen(const BossPhase& phase, double fight_seconds) {
  const TimedSpots& timed = phase.timed_spots();
  if (timed.interval_ms() <= 0 || timed.spots().empty()) {
    return false;
  }
  double interval = timed.interval_ms() / 1000.0;
  if (fight_seconds < interval) {
    return false;
  }
  return std::fmod(fight_seconds, interval) < timed.open_ms() / 1000.0;
}

std::vector<int> ClosedSpots(const BossPhase& phase, double fight_seconds) {
  std::vector<int> closed;
  if (TimedSpotsOpen(phase, fight_seconds)) {
    return closed;
  }
  for (int i = 0; i < phase.timed_spots().spots_size(); ++i) {
    closed.push_back(phase.player_spots_size() + i);
  }
  return closed;
}

std::vector<int> DropFromClosedSpots(const BossPhase& phase,
                                     double fight_seconds,
                                     std::vector<int> standing) {
  std::vector<int> closed = ClosedSpots(phase, fight_seconds);
  if (closed.empty()) {
    return standing;
  }
  std::vector<ArenaSpot> spots = AllPlayerSpots(phase);
  // Doubled, so an even width's middle falls on a whole number.
  int middle = ArenaSize(phase).x() - 1;
  for (int& at : standing) {
    if (std::find(closed.begin(), closed.end(), at) == closed.end()) {
      continue;
    }
    const ArenaSpot& from = spots[at];
    int best = at;
    int best_distance = 0;
    int best_off_middle = 0;
    for (int i = 0; i < static_cast<int>(spots.size()); ++i) {
      if (std::find(closed.begin(), closed.end(), i) != closed.end() ||
          std::find(standing.begin(), standing.end(), i) != standing.end()) {
        continue;
      }
      int dx = spots[i].x() - from.x();
      int dy = spots[i].y() - from.y();
      int distance = dx * dx + dy * dy;
      int off_middle = std::abs(2 * spots[i].x() - middle);
      if (best == at || distance < best_distance ||
          (distance == best_distance && off_middle < best_off_middle)) {
        best = i;
        best_distance = distance;
        best_off_middle = off_middle;
      }
    }
    at = best;
  }
  return standing;
}

int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy) {
  return NextPlayerSpot(phase, from, dx, dy, {});
}

int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy,
                   const std::vector<int>& taken) {
  std::vector<ArenaSpot> spots = AllPlayerSpots(phase);
  if (from < 0 || from >= static_cast<int>(spots.size())) {
    return from;
  }
  const ArenaSpot& at = spots[from];
  int best = from;
  int best_along = 0;
  int best_across = 0;
  bool tied = false;
  for (int i = 0; i < static_cast<int>(spots.size()); ++i) {
    if (std::find(taken.begin(), taken.end(), i) != taken.end()) {
      continue;
    }
    const ArenaSpot& spot = spots[i];
    int step_x = spot.x() - at.x();
    int step_y = spot.y() - at.y();
    // How far the spot is in the pressed direction, and how far off to the
    // side. Only one of dx and dy is ever nonzero, so each is a single term.
    int along = step_x * dx + step_y * dy;
    int across = std::abs(step_x * dy) + std::abs(step_y * dx);
    // Skip spots further to the side than ahead. Otherwise pressing Right in
    // Horntail's top corner would jump to the spot under his tail.
    if (along <= 0 || across > along) {
      continue;
    }
    if (best == from || along < best_along ||
        (along == best_along && across < best_across)) {
      best = i;
      best_along = along;
      best_across = across;
      tied = false;
      continue;
    }
    tied = tied || (along == best_along && across == best_across);
  }
  return tied ? from : best;
}

}  // namespace ms
