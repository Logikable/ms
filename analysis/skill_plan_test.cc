#include "analysis/skill_plan.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Skill CommonNode(const std::string& name) {
  Skill skill;
  skill.set_name(name);
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_COMMON);
  skill.set_v_node(V_NODE_KIND_COMMON);
  skill.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  return skill;
}

// A 5th job with two common nodes in reach and `v_points` to spend on them.
std::unique_ptr<GameState> FifthJob(int64_t v_points) {
  auto state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{},
      std::map<std::string, Skill>{{"rope_lift", CommonNode("Rope Lift")},
                                   {"blink", CommonNode("Blink")}});
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(5);
  proto.set_v_points(v_points);
  state->character.RestoreFrom(proto, state->equips, state->items);
  return state;
}

// Blink is worth twice what Rope Lift is a level, so a plan from nothing buys
// Blink alone. Seven for a node's first level, four for each after.
TEST(SpendVMatrixTest, AReplanRefundsAndAnUnreplannedPlanStands) {
  auto rate_of = [](GameState& state) {
    return state.character.skill_level(state.skills.at("rope_lift")) +
           2.0 * state.character.skill_level(state.skills.at("blink"));
  };

  std::unique_ptr<GameState> fresh = FifthJob(22);
  ASSERT_TRUE(fresh->character.LearnSkill(fresh->skills.at("rope_lift")));
  SpendVMatrix(*fresh, rate_of);
  EXPECT_EQ(fresh->character.skill_level(fresh->skills.at("rope_lift")), 0);
  EXPECT_EQ(fresh->character.skill_level(fresh->skills.at("blink")), 4);

  // Kept standing: Rope Lift's level stays, and the fifteen left buy three
  // of Blink.
  std::unique_ptr<GameState> kept = FifthJob(22);
  ASSERT_TRUE(kept->character.LearnSkill(kept->skills.at("rope_lift")));
  SpendVMatrix(*kept, rate_of, /*replan=*/false);
  EXPECT_EQ(kept->character.skill_level(kept->skills.at("rope_lift")), 1);
  EXPECT_EQ(kept->character.skill_level(kept->skills.at("blink")), 3);
}

}  // namespace
}  // namespace ms
