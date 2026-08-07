// Checks the shipped skill catalog rather than any one function: every job's
// book has to cost exactly the SP that job earns, and that only holds if the
// data says so. Arithmetic done by hand in a textproto is the sort of thing
// that rots the moment a skill is added.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/character/character.h"
#include "src/proto_loader.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// What a job stage's levels pay out, indexed by stage: 3 SP a level over the
// span that feeds it, with the advancement itself granting nothing. Levels
// 11-30 feed stage 1 and 31-60 feed stage 2, so 60 and then 90.
constexpr int kSpByStage[] = {0, 60, 90};

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

// Every book costs exactly what its own levels pay out, so reaching the end of
// a stage means having bought the whole of it -- neither short nor with points
// left over.
TEST(SkillDataTest, EveryBookCostsExactlyWhatItsLevelsPayOut) {
  std::map<int, int> cost_by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    cost_by_advancement[entry.second.job_advancement()] +=
        entry.second.max_level();
  }
  // Named one by one rather than counted: the skills sit in a folder per job,
  // and a folder that stopped being read would take a whole advancement out of
  // the map, leaving the rest to sum correctly and the check to pass on the
  // others while one job had no book at all.
  const JobAdvancement kWritten[] = {
      JOB_ADVANCEMENT_SWORDMAN, JOB_ADVANCEMENT_ARCHER,
      JOB_ADVANCEMENT_MAGICIAN, JOB_ADVANCEMENT_ROGUE,
      JOB_ADVANCEMENT_SPEARMAN};
  for (JobAdvancement advancement : kWritten) {
    EXPECT_TRUE(cost_by_advancement.count(advancement))
        << "advancement " << advancement << " has no skills at all";
  }
  for (const std::pair<const int, int>& entry : cost_by_advancement) {
    int stage = StageForAdvancement(static_cast<JobAdvancement>(entry.first));
    ASSERT_GT(stage, 0) << "advancement " << entry.first << " has no stage";
    ASSERT_LT(stage,
              static_cast<int>(sizeof(kSpByStage) / sizeof(kSpByStage[0])))
        << "stage " << stage << " has no SP figure to be held to";
    EXPECT_EQ(entry.second, kSpByStage[stage])
        << "advancement " << entry.first << " costs " << entry.second
        << " against the " << kSpByStage[stage] << " its levels pay out";
  }
}

// An auto-attack with no interval never fires, so a skill that means to be one
// and forgets to say how often is a skill that silently does nothing.
TEST(SkillDataTest, EveryAutoAttackSaysHowOftenItFires) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.kind() != SKILL_KIND_AUTO_ATTACK) {
      EXPECT_EQ(entry.second.cast_interval_seconds(), 0.0)
          << entry.first << " sets an interval it will never be asked for";
      continue;
    }
    EXPECT_GT(entry.second.cast_interval_seconds(), 0.0)
        << entry.first << " would never fire";
    EXPECT_GT(entry.second.base().skill_pct(), 0.0)
        << entry.first << " would fire for nothing";
  }
}

// Damage belongs to the things that swing, and the levers belong to the
// passives. A skill filed on the wrong side of that carries data nothing will
// ever read.
TEST(SkillDataTest, DamageAndPassiveLeversDoNotCross) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() == SKILL_KIND_PASSIVE) {
      EXPECT_EQ(skill.base().skill_pct(), 0.0)
          << entry.first << " is a passive carrying a swing's damage";
    } else {
      EXPECT_EQ(skill.base().final_attack_chance(), 0.0)
          << entry.first << " is not a passive, so its levers go unread";
      EXPECT_EQ(skill.base().mastery(), 0.0) << entry.first;
      EXPECT_EQ(skill.base().str(), 0) << entry.first;
    }
  }
}

// The catalog is keyed by file stem, so two skills can share a display name --
// the three 2nd-job warriors each get their own Weapon Mastery. A character's
// learned levels are keyed by that display name, though, so two skills one
// character can reach under the same name are one level between them: buying
// either buys both, and each folds the other's levers into the stats.
//
// Nothing in the model stops that; what keeps it from happening is that the
// branches are exclusive, so only one book of any pair is ever the
// character's. This is the check that the data stays that way -- the trap is a
// later stage repeating a name from an earlier one, where both books belong to
// the same character.
TEST(SkillDataTest, OneSkillPerNamePerCharacter) {
  std::map<std::string, Skill> skills = LoadSkills();
  const Job kJobs[] = {JOB_SWORDMAN, JOB_FIGHTER,  JOB_PAGE, JOB_SPEARMAN,
                       JOB_ARCHER,   JOB_MAGICIAN, JOB_ROGUE};
  for (Job job : kJobs) {
    std::map<std::string, std::string> stem_by_name;
    // Every stage the job has a book at. Two is what exists; a stage past the
    // last one simply answers with no advancement.
    for (int stage = 1; stage <= 2; ++stage) {
      JobAdvancement advancement = AdvancementForJobStage(job, stage);
      if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& entry : skills) {
        if (entry.second.job_advancement() != advancement) {
          continue;
        }
        std::pair<std::map<std::string, std::string>::iterator, bool> added =
            stem_by_name.insert({entry.second.name(), entry.first});
        EXPECT_TRUE(added.second)
            << Job_Name(job) << " reaches both " << entry.first << " and "
            << added.first->second << ", which are both called \""
            << entry.second.name() << "\" and so share one learned level";
      }
    }
  }
}

// A requirement naming a skill that is not in the catalog locks its skill
// forever and says so in words the player cannot act on. Learned levels are
// keyed by display name, so that is what has to match.
TEST(SkillDataTest, EveryRequirementNamesASkillThatExists) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::map<std::string, const Skill*> by_name;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    by_name[entry.second.name()] = &entry.second;
  }
  for (const std::pair<const std::string, Skill>& entry : skills) {
    if (!entry.second.has_required_skill()) {
      continue;
    }
    const SkillRequirement& required = entry.second.required_skill();
    std::map<std::string, const Skill*>::const_iterator it =
        by_name.find(required.skill_name());
    ASSERT_NE(it, by_name.end())
        << entry.first << " waits on \"" << required.skill_name()
        << "\", which no skill is called";
    EXPECT_GT(required.level(), 0) << entry.first;
    EXPECT_LE(required.level(), it->second->max_level())
        << entry.first << " waits on a level of " << required.skill_name()
        << " that cannot be reached";
    // The two have to share a book, or the requirement is unbuyable until an
    // advancement the player may never take.
    EXPECT_EQ(it->second->job_advancement(), entry.second.job_advancement())
        << entry.first << " waits on a skill from another advancement";
  }
}

}  // namespace
}  // namespace ms
