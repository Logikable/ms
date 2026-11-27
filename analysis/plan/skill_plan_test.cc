#include "analysis/plan/skill_plan.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/build_config.h"
#include "src/character/matrix/hexa_matrix.h"
#include "src/character/matrix/hexa_stat.h"
#include "src/character/matrix/sol_erda.h"
#include "src/character/matrix/v_matrix.h"
#include "src/character/skill_placement.h"
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

// A 5th job character with two common nodes available and `v_points` to spend.
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

// Blink is worth twice as much as Rope Lift per level, so a plan from scratch
// buys only Blink. A node's first level costs seven points, each later level
// four.
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

  // Without a replan, Rope Lift keeps its level and the remaining fifteen
  // points buy three levels of Blink.
  std::unique_ptr<GameState> kept = FifthJob(22);
  ASSERT_TRUE(kept->character.LearnSkill(kept->skills.at("rope_lift")));
  SpendVMatrix(*kept, rate_of, /*replan=*/false);
  EXPECT_EQ(kept->character.skill_level(kept->skills.at("rope_lift")), 1);
  EXPECT_EQ(kept->character.skill_level(kept->skills.at("blink")), 3);
}

Skill MasteryNode(const std::string& name) {
  Skill skill;
  skill.set_name(name);
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_DARK_KNIGHT_VI);
  skill.set_hexa_node(HEXA_NODE_KIND_MASTERY);
  skill.set_max_level(kHexaNodeMaxLevel);
  return skill;
}

// A whole Mastery costs 83 Sol Erda and 2,252 Fragments. The spender maxes
// the node that measures, leaves the one that doesn't, and opens and rolls
// all three HEXA Stat nodes, each on a primary of its own.
TEST(SpendHexaMatrixTest, BuysWhatMeasuresAndNothingThatDoesnt) {
  if (!kSixthJobEnabled) {
    GTEST_SKIP() << "the 6th job is behind --config=sixth";
  }
  auto state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{},
      std::map<std::string, Skill>{{"good", MasteryNode("Good")},
                                   {"idle", MasteryNode("Idle")}});
  Character proto;
  proto.set_level(270);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(6);
  proto.set_sol_erda_energy(200 * kEnergyPerSolErda);
  proto.set_sol_erda_fragments(20000);
  state->character.RestoreFrom(proto, state->equips, state->items);
  SpendHexaMatrix(*state, [](GameState& inner) {
    return 1.0 + inner.character.skill_level(inner.skills.at("good"));
  });
  EXPECT_EQ(state->character.skill_level(state->skills.at("good")), 30);
  EXPECT_EQ(state->character.skill_level(state->skills.at("idle")), 0);
  const HexaStats& stats = state->character.hexa_stats();
  ASSERT_EQ(stats.nodes_size(), kHexaStatNodes);
  for (const HexaStatNode& node : stats.nodes()) {
    EXPECT_EQ(HexaStatNodeLevels(node), kHexaStatNodeLevels);
  }
  EXPECT_TRUE(HexaStatsAllowed(stats));
  EXPECT_GE(state->character.sol_erda_fragments(), 0);
}

// A node that measured nothing once isn't written off: when it measures, a
// later call with the same memo prices and buys it, though another node's
// score leads it.
TEST(SpendHexaMatrixTest, ANodeThatMeasuredNothingIsPricedAgain) {
  if (!kSixthJobEnabled) {
    GTEST_SKIP() << "the 6th job is behind --config=sixth";
  }
  auto state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{},
      std::map<std::string, Skill>{{"good", MasteryNode("Good")},
                                   {"late", MasteryNode("Late")}});
  Character proto;
  proto.set_level(270);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(6);
  proto.set_sol_erda_energy(200 * kEnergyPerSolErda);
  proto.set_sol_erda_fragments(20000);
  state->character.RestoreFrom(proto, state->equips, state->items);
  MatrixMemo memo;
  SpendHexaMatrix(
      *state,
      [](GameState& inner) {
        return 1.0 + inner.character.skill_level(inner.skills.at("good"));
      },
      &memo);
  ASSERT_EQ(state->character.skill_level(state->skills.at("good")), 30);
  ASSERT_EQ(state->character.skill_level(state->skills.at("late")), 0);
  SpendHexaMatrix(
      *state,
      [](GameState& inner) {
        return 1.0 + inner.character.skill_level(inner.skills.at("good")) +
               inner.character.skill_level(inner.skills.at("late"));
      },
      &memo);
  EXPECT_EQ(state->character.skill_level(state->skills.at("late")), 30);
}

// A node worth nothing until its last level is still started: the purse buys
// what it can toward it, and the next top-up finishes it.
TEST(SpendHexaMatrixTest, ANodeThatPaysOnlyWholeIsBoughtTowardIt) {
  if (!kSixthJobEnabled) {
    GTEST_SKIP() << "the 6th job is behind --config=sixth";
  }
  auto state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{},
      std::map<std::string, Skill>{{"late", MasteryNode("Late")}});
  Character proto;
  proto.set_level(260);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(6);
  proto.set_sol_erda_energy(200 * kEnergyPerSolErda);
  proto.set_sol_erda_fragments(1000);
  state->character.RestoreFrom(proto, state->equips, state->items);
  // Each level short of the top costs a little; the top pays for them all.
  const SkillRate rate = [](GameState& inner) {
    const int level = inner.character.skill_level(inner.skills.at("late"));
    return level == kHexaNodeMaxLevel ? 100.0 : 1.0 - 0.001 * level;
  };
  SpendHexaMatrix(*state, rate);
  const int started = state->character.skill_level(state->skills.at("late"));
  EXPECT_GT(started, 0);
  EXPECT_LT(started, kHexaNodeMaxLevel);
  state->character.AddSolErdaFragments(5000);
  SpendHexaMatrix(*state, rate);
  EXPECT_EQ(state->character.skill_level(state->skills.at("late")),
            kHexaNodeMaxLevel);
}

// Wherever the rolls land a node's levels, the reselect moves the one stat
// that measures onto the slot they made worth most.
TEST(SpendHexaMatrixTest, ReselectsEachNodeOntoItsBestSlots) {
  if (!kSixthJobEnabled) {
    GTEST_SKIP() << "the 6th job is behind --config=sixth";
  }
  auto state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{}, std::map<std::string, Skill>{});
  Character proto;
  proto.set_level(270);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(6);
  proto.set_sol_erda_energy(200 * kEnergyPerSolErda);
  proto.set_sol_erda_fragments(20000);
  state->character.RestoreFrom(proto, state->equips, state->items);
  const SkillRate boss = [](GameState& inner) {
    return 1.0 + HexaStatTotalsFor(inner.character.hexa_stats(),
                                   inner.character.proto().level())
                     .boss_pct;
  };
  SpendHexaMatrix(*state, boss);
  const HexaStats held = state->character.hexa_stats();
  ASSERT_EQ(held.nodes_size(), kHexaStatNodes);
  const double kept = boss(*state);
  const HexaStat choices[] = {HEXA_STAT_CRIT_DAMAGE,    HEXA_STAT_BOSS_DAMAGE,
                              HEXA_STAT_IGNORE_DEFENSE, HEXA_STAT_DAMAGE,
                              HEXA_STAT_ATTACK,         HEXA_STAT_MAIN_STAT};
  for (int node = 0; node < kHexaStatNodes; ++node) {
    for (HexaStat primary : choices) {
      for (HexaStat first : choices) {
        for (HexaStat second : choices) {
          HexaStats trial = held;
          trial.mutable_nodes(node)->set_primary(primary);
          trial.mutable_nodes(node)->set_first(first);
          trial.mutable_nodes(node)->set_second(second);
          if (state->character.SetHexaStats(trial)) {
            EXPECT_LE(boss(*state), kept) << "node " << node;
          }
        }
      }
    }
  }
}

}  // namespace
}  // namespace ms
