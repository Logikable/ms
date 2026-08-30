#include "src/character/job_branch.h"

#include <gtest/gtest.h>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

TEST(JobBranchTest, EveryStageOfALineAnswersTheSameBranch) {
  EXPECT_EQ(BranchOf(JOB_SWORDMAN), JobBranch::kWarrior);
  EXPECT_EQ(BranchOf(JOB_FIGHTER), JobBranch::kWarrior);
  EXPECT_EQ(BranchOf(JOB_WHITE_KNIGHT), JobBranch::kWarrior);
  EXPECT_EQ(BranchOf(JOB_HERO), JobBranch::kWarrior);
  EXPECT_EQ(BranchOf(JOB_MAGICIAN), JobBranch::kMagician);
  EXPECT_EQ(BranchOf(JOB_BISHOP), JobBranch::kMagician);
  EXPECT_EQ(BranchOf(JOB_ARCHER), JobBranch::kArcher);
  EXPECT_EQ(BranchOf(JOB_MARKSMAN), JobBranch::kArcher);
  EXPECT_EQ(BranchOf(JOB_ROGUE), JobBranch::kRogue);
  EXPECT_EQ(BranchOf(JOB_SHADOWER), JobBranch::kRogue);
}

// The beginner is its own branch: the callers disagree about it, so folding it
// into the warriors would quietly change what a level-1 character wears.
TEST(JobBranchTest, TheBeginnerIsItsOwnBranchAndNothingElseIsNone) {
  EXPECT_EQ(BranchOf(JOB_BEGINNER), JobBranch::kBeginner);
  EXPECT_EQ(BranchOf(JOB_UNSPECIFIED), JobBranch::kNone);
  for (int i = 0; i < Job_ARRAYSIZE; ++i) {
    Job job = static_cast<Job>(i);
    if (!Job_IsValid(i) || job == JOB_UNSPECIFIED) {
      continue;
    }
    EXPECT_NE(BranchOf(job), JobBranch::kNone) << Job_Name(job);
  }
}

}  // namespace
}  // namespace ms
