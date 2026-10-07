#include "src/combat/arena_spots.h"

#include <vector>

#include "gtest/gtest.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

void AddSpot(google::protobuf::RepeatedPtrField<ArenaSpot>* spots, int x,
             int y) {
  ArenaSpot* spot = spots->Add();
  spot->set_x(x);
  spot->set_y(y);
}

// Gloom's arena: five floor spots, then the X that opens over them.
BossPhase XArena() {
  BossPhase phase;
  phase.set_arena_width(9);
  phase.set_arena_height(6);
  for (int x : {4, 0, 8, 2, 6}) {
    AddSpot(phase.mutable_player_spots(), x, 5);
  }
  TimedSpots* timed = phase.mutable_timed_spots();
  timed->set_interval_ms(55000);
  timed->set_open_ms(20000);
  for (int y : {4, 3, 1, 0}) {
    int x = y == 4 || y == 0 ? 1 : 2;
    AddSpot(timed->mutable_spots(), x, y);
    AddSpot(timed->mutable_spots(), 8 - x, y);
  }
  return phase;
}

constexpr int kFloorMiddle = 0;
constexpr int kFloorLeft = 1;
constexpr int kFloorRight = 2;
constexpr int kFloorInnerLeft = 3;
constexpr int kFloorInnerRight = 4;
constexpr int kLowLeft = 5;
constexpr int kLowRight = 6;
constexpr int kKneeLeft = 7;
constexpr int kTopLeft = 11;

TEST(ArenaSpotsTest, TimedSpotsOpenFromEachIntervalForTheirSpan) {
  BossPhase phase = XArena();
  EXPECT_EQ(AllPlayerSpots(phase).size(), 13u);
  EXPECT_FALSE(TimedSpotsOpen(phase, 0.0));
  EXPECT_FALSE(TimedSpotsOpen(phase, 54.9));
  EXPECT_TRUE(TimedSpotsOpen(phase, 55.0));
  EXPECT_TRUE(TimedSpotsOpen(phase, 74.9));
  EXPECT_FALSE(TimedSpotsOpen(phase, 75.0));
  EXPECT_TRUE(TimedSpotsOpen(phase, 110.0));
  EXPECT_EQ(ClosedSpots(phase, 60.0), std::vector<int>());
  EXPECT_EQ(ClosedSpots(phase, 80.0),
            std::vector<int>({5, 6, 7, 8, 9, 10, 11, 12}));
  EXPECT_EQ(ClosedSpots(BossPhase(), 80.0), std::vector<int>());
}

TEST(ArenaSpotsTest, DropsToNearestFreeSpot) {
  BossPhase phase = XArena();
  // Open: nobody moves.
  EXPECT_EQ(DropFromClosedSpots(phase, 60.0, {kLowLeft, kTopLeft}),
            std::vector<int>({kLowLeft, kTopLeft}));
  // Straight down from the knee; from x:1 the two floor spots beside it tie,
  // and the inner one wins, at either height.
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0, {kKneeLeft}),
            std::vector<int>({kFloorInnerLeft}));
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0, {kLowLeft}),
            std::vector<int>({kFloorInnerLeft}));
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0, {kLowRight}),
            std::vector<int>({kFloorInnerRight}));
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0, {kTopLeft}),
            std::vector<int>({kFloorInnerLeft}));
  // Party order settles who lands where, and nobody lands on a member already
  // on the floor or one who left.
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0, {kKneeLeft, kLowLeft}),
            std::vector<int>({kFloorInnerLeft, kFloorLeft}));
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0,
                                {kFloorInnerLeft, -1, kLowLeft, kFloorMiddle}),
            std::vector<int>({kFloorInnerLeft, -1, kFloorLeft, kFloorMiddle}));
}

TEST(ArenaSpotsTest, APlayerWithNowhereToDropStays) {
  BossPhase phase = XArena();
  phase.mutable_player_spots()->DeleteSubrange(1, 4);
  EXPECT_EQ(DropFromClosedSpots(phase, 80.0, {kFloorMiddle, 1}),
            std::vector<int>({kFloorMiddle, 1}));
}

TEST(ArenaSpotsTest, MovementReachesTimedSpotsUnlessTheyAreTaken) {
  BossPhase phase = XArena();
  EXPECT_EQ(NextPlayerSpot(phase, kFloorLeft, 0, -1), kLowLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kLowLeft, 0, -1), kKneeLeft);
  // Up from the middle is a tie between the two knees.
  EXPECT_EQ(NextPlayerSpot(phase, kFloorMiddle, 0, -1), kFloorMiddle);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorLeft, 0, -1, ClosedSpots(phase, 0.0)),
            kFloorLeft);
}

TEST(ArenaSpotsTest, ArenaSizeFallsBackToTheFurthestSpot) {
  BossPhase phase = XArena();
  EXPECT_EQ(ArenaSize(phase).x(), 9);
  EXPECT_EQ(ArenaSize(phase).y(), 6);
  phase.clear_arena_width();
  phase.clear_arena_height();
  AddSpot(phase.mutable_timed_spots()->mutable_spots(), 10, 0);
  EXPECT_EQ(ArenaSize(phase).x(), 11);
  EXPECT_EQ(ArenaSize(phase).y(), 6);
}

}  // namespace
}  // namespace ms
