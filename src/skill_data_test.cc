// Checks the shipped skill catalog rather than any one function: every job's
// book has to cost exactly the SP that job earns, and that only holds if the
// data says so. Arithmetic done by hand in a textproto is the sort of thing
// that rots the moment a skill is added.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/proto_loader.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// What levels 11-30 pay out: 3 SP a level over 20 levels, with the advancement
// itself granting nothing.
constexpr int kFirstJobSp = 60;

std::map<std::string, Skill> LoadSkills() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<Skill>(runfiles->Rlocation("ms/data/skills"));
}

TEST(SkillDataTest, EverySkillBelongsToAnAdvancement) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_NE(entry.second.job_advancement(), JOB_ADVANCEMENT_UNSPECIFIED)
        << entry.first << " would be unreachable: no tab shows it and no SP "
        << "pool buys it";
  }
}

// The inspect screen has nothing else to say about a skill: its levers are
// numbers, and only this tells the player what the numbers are for.
TEST(SkillDataTest, EverySkillDescribesItself) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_FALSE(entry.second.description().empty())
        << entry.first << " would inspect to a blank panel";
  }
}

TEST(SkillDataTest, EveryFirstJobBookCostsExactlySixty) {
  std::map<int, int> cost_by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    cost_by_advancement[entry.second.job_advancement()] +=
        entry.second.max_level();
  }
  ASSERT_FALSE(cost_by_advancement.empty());
  for (const std::pair<const int, int>& entry : cost_by_advancement) {
    EXPECT_EQ(entry.second, kFirstJobSp)
        << "advancement " << entry.first << " costs " << entry.second
        << "; a character reaching level 30 can neither max it nor is left "
        << "with points to spare";
  }
}

}  // namespace
}  // namespace ms
