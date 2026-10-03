#include "src/character/noblesse_plan.h"

#include <map>
#include <random>
#include <string>

#include "gtest/gtest.h"
#include "src/character/character.h"
#include "src/character/skill_placement.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Skill Noblesse(const std::string& name) {
  Skill skill;
  skill.set_name(name);
  PlaceIn(skill, JOB_ADVANCEMENT_BEGINNER);
  skill.set_guild(GUILD_SKILL_NOBLESSE);
  skill.set_max_level(3);
  return skill;
}

// The rating decides, a level at a time: the first skill is worth more until
// it is full, and what the pool can't put there goes to the second. A refund
// first lets the plan move an earlier choice.
TEST(NoblessePlanTest, SpendsEachLevelWhereItPaysMost) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(200);
  CharacterInstance c(rng, std::move(proto));
  std::map<std::string, Skill> skills = {{"a", Noblesse("A")},
                                         {"b", Noblesse("B")}};
  c.set_noblesse_sp_earned(4);
  ASSERT_TRUE(c.LearnSkill(skills["b"], 4 - 1));

  auto rate = [&skills](CharacterInstance& ch) {
    return 10.0 * ch.skill_level(skills.at("a")) +
           1.0 * ch.skill_level(skills.at("b"));
  };
  EXPECT_EQ(SpendNoblesseSp(c, skills, rate), 1);
  EXPECT_EQ(c.skill_level(skills["a"]), 1) << "only the point left over";

  RefundNoblesseSp(c, skills);
  EXPECT_EQ(c.noblesse_sp(), 4);
  EXPECT_EQ(SpendNoblesseSp(c, skills, rate), 4);
  EXPECT_EQ(c.skill_level(skills["a"]), 3);
  EXPECT_EQ(c.skill_level(skills["b"]), 1);
  EXPECT_EQ(c.noblesse_sp(), 0);
}

}  // namespace
}  // namespace ms
