#include "src/character/link.h"

#include <gtest/gtest.h>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

TEST(LinkTest, ARungAtEachOfGmsThreeLevels) {
  EXPECT_EQ(LinkRungsFor(69), 0);
  EXPECT_EQ(LinkRungsFor(70), 1);
  EXPECT_EQ(LinkRungsFor(119), 1);
  EXPECT_EQ(LinkRungsFor(120), 2);
  EXPECT_EQ(LinkRungsFor(209), 2);
  EXPECT_EQ(LinkRungsFor(210), 3);
  EXPECT_EQ(LinkRungsFor(300), 3);
}

// The rule the system is built on: a line counts once however many characters
// are in it, and the lines of one branch add up.
TEST(LinkTest, OneLinePaysOnceAndTheBranchSums) {
  LinkTally tally;
  tally.Record(JOB_HERO, 120);
  tally.Record(JOB_DARK_KNIGHT, 70);
  tally.Record(JOB_DARK_KNIGHT, 210);
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 5);

  // Characters of another branch don't affect this one.
  EXPECT_EQ(tally.LevelFor(JOB_MAGICIAN), 0);
  EXPECT_EQ(tally.LevelFor(JOB_ARCHER), 0);
  EXPECT_EQ(tally.LevelFor(JOB_ROGUE), 0);
}

// A line is identified by its 2nd job, so every advancement along it counts as
// the same character would.
TEST(LinkTest, EveryAdvancementOfALineIsThatLine) {
  LinkTally tally;
  tally.Record(JOB_CRUSADER, 210);
  tally.Record(JOB_FIGHTER, 210);
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 3);

  // A character who never took a 2nd advancement has no line.
  LinkTally undecided;
  undecided.Record(JOB_SWORDMAN, 210);
  undecided.Record(JOB_BEGINNER, 210);
  EXPECT_EQ(undecided.LevelFor(JOB_SWORDMAN), 0);
  EXPECT_TRUE(undecided.empty());
}

// Branches with two lines reach 6 where three-line branches reach 9. The data
// goes up to GMS's 9 either way; how far a branch gets depends on the roster,
// not the skill.
TEST(LinkTest, ABranchReachesThreeRungsPerLineItHas) {
  LinkTally tally;
  for (Job job : {JOB_HERO, JOB_PALADIN, JOB_DARK_KNIGHT, JOB_BISHOP,
                  JOB_FIRE_POISON_ARCH_MAGE, JOB_ICE_LIGHTNING_ARCH_MAGE,
                  JOB_BOW_MASTER, JOB_MARKSMAN, JOB_NIGHT_LORD, JOB_SHADOWER}) {
    tally.Record(job, 210);
  }
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 9);
  EXPECT_EQ(tally.LevelFor(JOB_MAGICIAN), 9);
  EXPECT_EQ(tally.LevelFor(JOB_ARCHER), 6);
  EXPECT_EQ(tally.LevelFor(JOB_ROGUE), 6);
}

// MirrorAccount relies on this: the character being played is added to a copy,
// so the session's tally is never changed by whoever reads it.
TEST(LinkTest, WithFoldsOneMoreCharacterInWithoutKeepingThem) {
  LinkTally tally;
  tally.Record(JOB_HERO, 210);
  EXPECT_EQ(tally.With(JOB_PALADIN, 120).LevelFor(JOB_SWORDMAN), 5);
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 3);
}

}  // namespace
}  // namespace ms
