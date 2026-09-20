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

// The rule the whole system rests on: a LINE pays once however many
// characters walk it, and the lines of one branch sum.
TEST(LinkTest, OneLinePaysOnceAndTheBranchSums) {
  LinkTally tally;
  tally.Record(JOB_HERO, 120);
  tally.Record(JOB_DARK_KNIGHT, 70);
  tally.Record(JOB_DARK_KNIGHT, 210);
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 5);

  // Another branch's characters say nothing about this one.
  EXPECT_EQ(tally.LevelFor(JOB_MAGICIAN), 0);
  EXPECT_EQ(tally.LevelFor(JOB_ARCHER), 0);
  EXPECT_EQ(tally.LevelFor(JOB_ROGUE), 0);
}

// A line is the SECOND job, so every advancement along one counts as the same
// character would.
TEST(LinkTest, EveryAdvancementOfALineIsThatLine) {
  LinkTally tally;
  tally.Record(JOB_CRUSADER, 210);
  tally.Record(JOB_FIGHTER, 210);
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 3);

  // And a character who never took a second advancement has no line to pay.
  LinkTally undecided;
  undecided.Record(JOB_SWORDMAN, 210);
  undecided.Record(JOB_BEGINNER, 210);
  EXPECT_EQ(undecided.LevelFor(JOB_SWORDMAN), 0);
  EXPECT_TRUE(undecided.empty());
}

// The two-line branches reach 6 where the three-line ones reach 9. The data
// runs to GMS's 9 either way -- what a branch reaches is the roster's answer,
// not the skill's.
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

// What MirrorAccount leans on: the played character folds into a copy, so the
// tally the session holds is never disturbed by whoever is reading it.
TEST(LinkTest, WithFoldsOneMoreCharacterInWithoutKeepingThem) {
  LinkTally tally;
  tally.Record(JOB_HERO, 210);
  EXPECT_EQ(tally.With(JOB_PALADIN, 120).LevelFor(JOB_SWORDMAN), 5);
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 3);
}

}  // namespace
}  // namespace ms
