#include "src/frontend/progress_watcher.h"

#include <gtest/gtest.h>

#include "src/character/character.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

Character At(int level, Job job) {
  Character proto;
  proto.set_level(level);
  proto.set_job(job);
  return proto;
}

// Seeded from the character as loaded, so launching into a level 13 character
// is not itself a level-up.
TEST(ProgressWatcherTest, NoticesNothingOnTheCharacterItWasSeededFrom) {
  Character loaded = At(13, JOB_SWORDMAN);
  ProgressWatcher watcher(loaded);
  EXPECT_EQ(watcher.Notice(loaded).kind, kNothingNoticed);
}

TEST(ProgressWatcherTest, AClimbNamesItsEndsAndWhatItPaid) {
  ProgressWatcher watcher(At(10, JOB_SWORDMAN));
  Progress progress = watcher.Notice(At(12, JOB_SWORDMAN));
  EXPECT_EQ(progress.kind, kLevelGained);
  EXPECT_EQ(progress.from_level, 10);
  EXPECT_EQ(progress.to_level, 12);
  EXPECT_GT(progress.ap, 0);
  EXPECT_GT(progress.sp, 0);
  // Both levels of the climb are paid for, not just the one it landed on.
  EXPECT_EQ(progress.ap, GainsForLevels(10, 12).ap);
  EXPECT_EQ(progress.sp, GainsForLevels(10, 12).sp);
  // And the climb is spent: looking again at the same character says nothing.
  EXPECT_EQ(watcher.Notice(At(12, JOB_SWORDMAN)).kind, kNothingNoticed);
}

// A Beginner's SP is real but unreachable -- the skills tab belongs to a job
// -- so the card does not mention what they cannot go and spend.
TEST(ProgressWatcherTest, ABeginnerIsPaidNoSp) {
  // Past level 11, where SP starts being paid -- below it every job earns 0
  // and the rule would be true for the wrong reason.
  ProgressWatcher beginner(At(15, JOB_BEGINNER));
  Progress unpaid = beginner.Notice(At(16, JOB_BEGINNER));
  EXPECT_EQ(unpaid.kind, kLevelGained);
  EXPECT_GT(unpaid.ap, 0);
  EXPECT_EQ(unpaid.sp, 0);

  ProgressWatcher swordman(At(15, JOB_SWORDMAN));
  EXPECT_GT(swordman.Notice(At(16, JOB_SWORDMAN)).sp, 0);
}

// The advancement is the larger news, and reaching the level that offers one
// does not itself take it, so the two cannot describe the same moment.
TEST(ProgressWatcherTest, AnAdvancementWinsOverTheLevelBesideIt) {
  ProgressWatcher watcher(At(10, JOB_BEGINNER));
  Progress progress = watcher.Notice(At(11, JOB_SWORDMAN));
  EXPECT_EQ(progress.kind, kJobAdvanced);
  EXPECT_EQ(progress.from_job, JOB_BEGINNER);
  EXPECT_EQ(progress.to_job, JOB_SWORDMAN);
  // The level it arrived at is taken with it, so it is not reported twice.
  EXPECT_EQ(watcher.Notice(At(11, JOB_SWORDMAN)).kind, kNothingNoticed);
}

// A level that somehow went down must not leave the next real level-up
// reporting a climb it did not make.
TEST(ProgressWatcherTest, ALevelThatFallsIsTakenAsTheNewFloor) {
  ProgressWatcher watcher(At(20, JOB_SWORDMAN));
  EXPECT_EQ(watcher.Notice(At(15, JOB_SWORDMAN)).kind, kNothingNoticed);
  Progress progress = watcher.Notice(At(16, JOB_SWORDMAN));
  EXPECT_EQ(progress.kind, kLevelGained);
  EXPECT_EQ(progress.from_level, 15);
}

}  // namespace
}  // namespace ms
