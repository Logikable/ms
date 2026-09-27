#include "analysis/alt_plan.h"

#include <gtest/gtest.h>

#include <vector>

namespace ms {
namespace {

AltLadder Ladder(double hours70, double hours120, double hours210) {
  AltLadder ladder;
  const double hours[] = {hours70, hours120, hours210};
  for (int rung = 0; rung < kLinkRungsPerLine; ++rung) {
    ladder.seconds[rung] = hours[rung] * 3600.0;
    ladder.meso[rung] = static_cast<int64_t>(hours[rung] * 1000);
  }
  return ladder;
}

// A Hero's own line is never offered, the Page and Hunter lines are, and of the
// two archer lines only the cheaper rung is offered. An alt already standing on
// a rung is charged only the climb from there.
TEST(AltStepsTest, OneCheapestRungPerSkill) {
  AltLadders ladders = {{JOB_FIGHTER, Ladder(1, 2, 3)},
                        {JOB_PAGE, Ladder(1, 2, 3)},
                        {JOB_HUNTER, Ladder(2, 4, 8)},
                        {JOB_CROSSBOWMAN, Ladder(5, 6, 9)}};
  AltLevels alts = {{JOB_PAGE, 120}, {JOB_HUNTER, 120}};

  std::vector<AltStep> steps = AltSteps(ladders, alts, JOB_HERO);
  ASSERT_EQ(steps.size(), 2u);
  EXPECT_EQ(steps[0].line, JOB_PAGE);
  EXPECT_EQ(steps[0].level, 210);
  EXPECT_DOUBLE_EQ(steps[0].seconds, 3600.0);
  EXPECT_EQ(steps[0].meso, 1000);
  EXPECT_EQ(steps[1].line, JOB_HUNTER);
  EXPECT_EQ(steps[1].level, 210);
  EXPECT_DOUBLE_EQ(steps[1].seconds, 4 * 3600.0);
}

// A line whose alt holds every rung, or whose climb never reached the next,
// offers nothing.
TEST(AltStepsTest, NothingPastTheLadder) {
  AltLadder short_climb = Ladder(1, 2, 3);
  short_climb.seconds[2] = -1.0;
  AltLadders ladders = {{JOB_PAGE, Ladder(1, 2, 3)}, {JOB_HUNTER, short_climb}};
  AltLevels alts = {{JOB_PAGE, 210}, {JOB_HUNTER, 120}};
  EXPECT_TRUE(AltSteps(ladders, alts, JOB_HERO).empty());
  EXPECT_EQ(AltTally(alts).LevelFor(JOB_SWORDMAN), 3);
  EXPECT_EQ(AltTally(alts).LevelFor(JOB_ARCHER), 2);
}

}  // namespace
}  // namespace ms
