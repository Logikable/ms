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

}  // namespace
}  // namespace ms
