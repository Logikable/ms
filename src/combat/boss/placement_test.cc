#include "src/combat/boss/placement.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Players per zone, zone 0 (nowhere) first.
std::vector<int> Counts(const std::vector<int>& placed, int zones) {
  std::vector<int> counts(zones + 1, 0);
  for (int zone : placed) {
    ++counts[zone];
  }
  return counts;
}

std::vector<int> Place(ZonePlacement mode, const std::vector<bool>& alive,
                       const std::vector<int>& current) {
  std::mt19937 rng;
  return PlacePlayers(mode, alive, current, rng);
}

const std::vector<bool> kAll = {true, true, true};

// Kaling's rooms: two to Taowu's (the first), the rest even over the others.
TEST(PlacementTest, ASpreadFillsTheFirstZoneThenEvensTheRest) {
  const std::vector<std::vector<int>> want = {
      {0, 1, 0, 0}, {0, 2, 0, 0}, {0, 2, 1, 0},
      {0, 2, 1, 1}, {0, 2, 2, 1}, {0, 2, 2, 2},
  };
  for (int n = 1; n <= 6; ++n) {
    std::vector<int> fresh(n, 0);
    EXPECT_EQ(Counts(Place(ZONE_PLACEMENT_SPREAD, kAll, fresh), 3), want[n - 1])
        << n << " players";
  }
}

TEST(PlacementTest, WhoGoesFirstIsDrawn) {
  std::vector<int> firsts;
  for (unsigned seed = 0; seed < 20; ++seed) {
    std::mt19937 rng(seed);
    std::vector<int> placed =
        PlacePlayers(ZONE_PLACEMENT_SPREAD, kAll, {0, 0, 0}, rng);
    firsts.push_back(static_cast<int>(
        std::find(placed.begin(), placed.end(), 2) - placed.begin()));
  }
  std::sort(firsts.begin(), firsts.end());
  EXPECT_NE(firsts.front(), firsts.back());
}

// Four in the rooms, Taowu dead: his two split over the others, and those
// already standing there stay.
TEST(PlacementTest, ASpreadReevensAfterAKill) {
  std::vector<int> placed =
      Place(ZONE_PLACEMENT_SPREAD, {false, true, true}, {1, 1, 2, 3});
  EXPECT_EQ(placed[2], 2);
  EXPECT_EQ(placed[3], 3);
  EXPECT_EQ(Counts(placed, 3), (std::vector<int>{0, 0, 2, 2}));
}

TEST(PlacementTest, NobodyIsSentPastTwo) {
  std::vector<int> placed =
      Place(ZONE_PLACEMENT_SPREAD, {false, false, true}, {1, 1, 2, 2, 3, 3});
  std::vector<int> counts = Counts(placed, 3);
  EXPECT_EQ(counts[3], 2);
  // The rest wait in their own dead rooms.
  EXPECT_EQ(placed[0], 1);
  EXPECT_EQ(placed[2], 2);
}

TEST(PlacementTest, PairsFillOneZoneAtATime) {
  const std::vector<std::vector<int>> want = {
      {0, 1, 0, 0}, {0, 2, 0, 0}, {0, 2, 1, 0},
      {0, 2, 2, 0}, {0, 2, 2, 1}, {0, 2, 2, 2},
  };
  for (int n = 1; n <= 6; ++n) {
    std::vector<int> fresh(n, 0);
    EXPECT_EQ(Counts(Place(ZONE_PLACEMENT_PAIRS, kAll, fresh), 3), want[n - 1])
        << n << " players";
  }
}

// Three in the last phase: the pair moves on together to an empty Peril, not
// beside the one already alone on another; with one Peril left, the third
// stands outside it.
TEST(PlacementTest, APairMovesOnTogetherAndTheThirdWaitsOutside) {
  std::vector<int> placed =
      Place(ZONE_PLACEMENT_PAIRS, {false, true, true}, {1, 1, 2});
  EXPECT_EQ(placed, (std::vector<int>{3, 3, 2}));
  placed = Place(ZONE_PLACEMENT_PAIRS, {false, false, true}, placed);
  EXPECT_EQ(placed, (std::vector<int>{3, 3, 2}));
}

TEST(PlacementTest, NoModeMovesNobody) {
  EXPECT_EQ(Place(ZONE_PLACEMENT_UNSPECIFIED, kAll, {0, 2}),
            (std::vector<int>{0, 2}));
}

TEST(PlacementTest, AZoneIsAliveWhileAnyOfItsMonstersStands) {
  BossPhase phase;
  phase.add_zones();
  phase.add_zones();
  phase.add_spawns();  // zone 0: anywhere
  phase.add_spawns()->set_zone(1);
  phase.add_spawns()->set_zone(2);
  EXPECT_EQ(ZonesAlive(phase, {0, 1, 2}, {true, false, true}),
            (std::vector<bool>{false, true}));
}

TEST(PlacementTest, AFreeSpotIsTheZonesFirstNotTaken) {
  BossPhase phase;
  PhaseZone* zone = phase.add_zones();
  zone->add_spots(4);
  zone->add_spots(5);
  EXPECT_EQ(FreeSpotInZone(phase, 1, {}), 4);
  EXPECT_EQ(FreeSpotInZone(phase, 1, {4}), 5);
  EXPECT_EQ(FreeSpotInZone(phase, 1, {4, 5}), -1);
  EXPECT_EQ(FreeSpotInZone(phase, 2, {}), -1);
}

}  // namespace
}  // namespace ms
