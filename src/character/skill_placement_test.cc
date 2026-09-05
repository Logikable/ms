#include "src/character/skill_placement.h"

#include "gtest/gtest.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Skill SharedSkill() {
  Skill skill;
  skill.set_name("Physical Training");
  SkillPlacement* fighter = skill.add_placement();
  fighter->set_job_advancement(JOB_ADVANCEMENT_FIGHTER);
  fighter->set_skill_order(8);
  SkillPlacement* page = skill.add_placement();
  page->set_job_advancement(JOB_ADVANCEMENT_PAGE);
  page->set_skill_order(6);
  return skill;
}

TEST(SkillPlacementTest, ASharedSkillSitsWhereEachBookPutsIt) {
  Skill skill = SharedSkill();
  EXPECT_TRUE(ListedIn(skill, JOB_ADVANCEMENT_FIGHTER));
  EXPECT_TRUE(ListedIn(skill, JOB_ADVANCEMENT_PAGE));
  EXPECT_FALSE(ListedIn(skill, JOB_ADVANCEMENT_SPEARMAN));
  EXPECT_EQ(SkillOrderIn(skill, JOB_ADVANCEMENT_FIGHTER), 8);
  EXPECT_EQ(SkillOrderIn(skill, JOB_ADVANCEMENT_PAGE), 6);
  EXPECT_EQ(SkillOrderIn(skill, JOB_ADVANCEMENT_SPEARMAN), 0);
  EXPECT_EQ(BookOf(skill), JOB_ADVANCEMENT_FIGHTER);
}

TEST(SkillPlacementTest, ASkillWithNoBookIsListedNowhere) {
  Skill skill;
  EXPECT_FALSE(ListedIn(skill, JOB_ADVANCEMENT_FIGHTER));
  EXPECT_EQ(SkillOrderIn(skill, JOB_ADVANCEMENT_FIGHTER), 0);
  EXPECT_EQ(BookOf(skill), JOB_ADVANCEMENT_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
