#include "src/frontend/progress_watcher.h"

#include <gtest/gtest.h>

#include "src/character/character.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// `stage` defaults to 1, which is correct for every job here except Beginner.
// What the tests care about is that it changes when the job does.
Character At(int level, Job job, int stage = 1) {
  Character proto;
  proto.set_level(level);
  proto.set_job(job);
  proto.set_job_stage(job == JOB_BEGINNER ? 0 : stage);
  return proto;
}

// Starts from the character as loaded, so loading a level 13 character doesn't
// count as a level-up.
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
  // Both levels of the climb are paid for, not just the one it ended on.
  EXPECT_EQ(progress.ap, GainsForLevels(10, 12).ap);
  EXPECT_EQ(progress.sp, GainsForLevels(10, 12).sp);
  // The climb is only reported once: checking the same character again finds
  // nothing.
  EXPECT_EQ(watcher.Notice(At(12, JOB_SWORDMAN)).kind, kNothingNoticed);
}

// A Beginner's SP exists but can't be spent, since the skills tab needs a job,
// so the card doesn't mention it.
TEST(ProgressWatcherTest, ABeginnerIsPaidNoSp) {
  // Past level 11, where SP starts. Below it every job earns 0 and the check
  // would pass for the wrong reason.
  ProgressWatcher beginner(At(15, JOB_BEGINNER));
  Progress unpaid = beginner.Notice(At(16, JOB_BEGINNER));
  EXPECT_EQ(unpaid.kind, kLevelGained);
  EXPECT_GT(unpaid.ap, 0);
  EXPECT_EQ(unpaid.sp, 0);

  ProgressWatcher swordman(At(15, JOB_SWORDMAN));
  EXPECT_GT(swordman.Notice(At(16, JOB_SWORDMAN)).sp, 0);
}

// The advancement is the bigger news, and reaching the level that offers one
// doesn't take it, so the two can't happen at the same moment.
TEST(ProgressWatcherTest, AnAdvancementWinsOverTheLevelBesideIt) {
  ProgressWatcher watcher(At(10, JOB_BEGINNER));
  Progress progress = watcher.Notice(At(11, JOB_SWORDMAN));
  EXPECT_EQ(progress.kind, kJobAdvanced);
  EXPECT_EQ(progress.from_job, JOB_BEGINNER);
  EXPECT_EQ(progress.to_job, JOB_SWORDMAN);
  EXPECT_EQ(progress.to_stage, 1);
  // The level it reached is recorded with it, so it isn't reported twice.
  EXPECT_EQ(watcher.Notice(At(11, JOB_SWORDMAN)).kind, kNothingNoticed);
}

// The 5th advancement doesn't change the job's name, so a watcher reading the
// name would miss the biggest thing a level 200 character does.
TEST(ProgressWatcherTest, TheFifthAdvancementIsNoticedThoughTheJobIsNot) {
  ProgressWatcher watcher(At(200, JOB_NIGHT_LORD, 4));
  Progress progress = watcher.Notice(At(200, JOB_NIGHT_LORD, 5));
  EXPECT_EQ(progress.kind, kJobAdvanced);
  EXPECT_EQ(progress.from_job, JOB_NIGHT_LORD);
  EXPECT_EQ(progress.to_job, JOB_NIGHT_LORD);
  EXPECT_EQ(progress.to_stage, 5);
  EXPECT_EQ(watcher.Notice(At(200, JOB_NIGHT_LORD, 5)).kind, kNothingNoticed);
}

// If the level ever goes down, the next real level-up must not report a climb
// that didn't happen.
TEST(ProgressWatcherTest, ALevelThatFallsIsTakenAsTheNewFloor) {
  ProgressWatcher watcher(At(20, JOB_SWORDMAN));
  EXPECT_EQ(watcher.Notice(At(15, JOB_SWORDMAN)).kind, kNothingNoticed);
  Progress progress = watcher.Notice(At(16, JOB_SWORDMAN));
  EXPECT_EQ(progress.kind, kLevelGained);
  EXPECT_EQ(progress.from_level, 15);
}

}  // namespace
}  // namespace ms
