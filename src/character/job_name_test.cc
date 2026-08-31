#include "src/character/job_name.h"

#include <gtest/gtest.h>

#include <string>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Nothing else asserts these: every other reader goes through a panel's
// rendered text, where a missing name reads as a blank column rather than a
// failure.
TEST(JobNameTest, EveryJobHasAName) {
  for (int i = Job_MIN; i <= Job_MAX; ++i) {
    if (!Job_IsValid(i) || i == JOB_UNSPECIFIED) {
      continue;
    }
    Job job = static_cast<Job>(i);
    EXPECT_NE(JobName(job), "Unknown") << Job_Name(job) << " is not named";
  }
}

// The short name is the default everywhere a job is shown, so every job has to
// answer it -- and the ones that need no shortening answer their own name.
TEST(JobNameTest, EveryJobHasAShortName) {
  for (int i = Job_MIN; i <= Job_MAX; ++i) {
    if (!Job_IsValid(i) || i == JOB_UNSPECIFIED) {
      continue;
    }
    Job job = static_cast<Job>(i);
    EXPECT_NE(ShortJobName(job), "Unknown") << Job_Name(job) << " is not named";
    EXPECT_LE(static_cast<int>(ShortJobName(job).size()),
              static_cast<int>(JobName(job).size()))
        << Job_Name(job) << " is longer short than long";
  }
}

TEST(JobNameTest, TheWizardsAreSpelledOutInFullAndAbbreviated) {
  EXPECT_EQ(JobName(JOB_ICE_LIGHTNING_WIZARD), "Ice/Lightning Wizard");
  EXPECT_EQ(ShortJobName(JOB_ICE_LIGHTNING_WIZARD), "I/L Wizard");
  EXPECT_EQ(JobName(JOB_FIRE_POISON_WIZARD), "Fire/Poison Wizard");
  EXPECT_EQ(ShortJobName(JOB_FIRE_POISON_WIZARD), "F/P Wizard");
  // The Cleric's name fits either way, so it is the same both times.
  EXPECT_EQ(JobName(JOB_CLERIC), "Cleric");
  EXPECT_EQ(ShortJobName(JOB_CLERIC), "Cleric");
  EXPECT_EQ(ShortJobName(JOB_SPEARMAN), "Spearman");
}

}  // namespace
}  // namespace ms
