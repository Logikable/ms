#include "src/character/job_name.h"

#include <gtest/gtest.h>

#include <string>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

// A missing name shows as a blank column in a panel, not a failure. The short
// name is the default wherever a job is shown, so every job needs one.
TEST(JobNameTest, EveryJobHasALongNameAndAShortOne) {
  for (int i = Job_MIN; i <= Job_MAX; ++i) {
    if (!Job_IsValid(i) || i == JOB_UNSPECIFIED) {
      continue;
    }
    Job job = static_cast<Job>(i);
    EXPECT_NE(JobName(job), "Unknown") << Job_Name(job) << " is not named";
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
  // The Cleric's name always fits, so both versions are the same.
  EXPECT_EQ(JobName(JOB_CLERIC), "Cleric");
  EXPECT_EQ(ShortJobName(JOB_CLERIC), "Cleric");
  EXPECT_EQ(ShortJobName(JOB_SPEARMAN), "Spearman");
}

// The 5th advancement is the only one that keeps the job's name, so it's the
// only one that must be distinguished from the job itself.
TEST(JobNameTest, OnlyTheFifthAdvancementTakesAV) {
  EXPECT_EQ(AdvancementName(JOB_NIGHT_LORD, 5), "Night Lord V");
  EXPECT_EQ(AdvancementName(JOB_NIGHT_LORD, 4), "Night Lord");
  EXPECT_EQ(AdvancementName(JOB_SWORDMAN, 1), "Swordman");
  EXPECT_EQ(ShortAdvancementName(JOB_ICE_LIGHTNING_ARCH_MAGE, 5),
            "I/L Arch Mage V");
  EXPECT_EQ(ShortAdvancementName(JOB_ICE_LIGHTNING_ARCH_MAGE, 4),
            "I/L Arch Mage");
}

}  // namespace
}  // namespace ms
