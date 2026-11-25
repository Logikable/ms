#include "analysis/sim_boss.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/combat/boss/liberation.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int64_t max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(110);
  mob.set_max_hp(max_hp);
  mob.set_boss(true);
  return mob;
}

BossPhase* AddPhase(BossDifficulty* difficulty, const std::string& mob) {
  BossPhase* phase = difficulty->add_phases();
  Spawn* spawn = phase->add_spawns();
  spawn->set_mob(mob);
  spawn->add_spots()->set_x(2);
  ArenaSpot* stand = phase->add_player_spots();
  stand->set_x(2);
  stand->set_y(1);
  return phase;
}

// A one-HP opener, then a body a plain sword can't dent in the time limit.
std::unique_ptr<GameState> WallState() {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"opener", MakeMob("Opener", 1)},
                                 {"wall", MakeMob("Wall", 1000000000000)}},
      std::map<std::string, MapData>{});
  Boss boss;
  boss.set_name("Wall");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_time_limit_seconds(1800);
  AddPhase(normal, "opener");
  AddPhase(normal, "wall");
  state->bosses["wall"] = boss;
  state->character.PickUp(std::make_unique<EquipInstance>(PlainSword()));
  state->character.Equip(0);
  return state;
}

// Clearing the opener is half the phases but none of the HP, so the fight is
// nearly all left. The player walks out early, and the projection carries the
// pace they left at to the time limit.
TEST(FightBossTest, AWalkOutReadsByHpAndAtItsPace) {
  std::unique_ptr<GameState> state = WallState();
  BossOutcome outcome = FightBoss(*state, "wall", 0);
  EXPECT_FALSE(outcome.won);
  EXPECT_LT(outcome.seconds, 1800.0);
  EXPECT_GT(outcome.left, 0.999);
  EXPECT_LT(outcome.left, 1.0);
  EXPECT_NEAR(1.0 - outcome.left_at_clock,
              (1.0 - outcome.left) * 1800.0 / outcome.seconds, 1e-9);
}

// A phase's own clock ends the fight before any walk-out is considered, and
// the fight's clock isn't there to carry the pace to.
TEST(FightBossTest, APhaseClockLossReadsWhatWasLeft) {
  std::unique_ptr<GameState> state = WallState();
  state->bosses["wall"]
      .mutable_difficulties(0)
      ->mutable_phases(1)
      ->set_time_limit_seconds(30);
  BossOutcome outcome = FightBoss(*state, "wall", 0);
  EXPECT_FALSE(outcome.won);
  EXPECT_LT(outcome.seconds, 60.0);
  EXPECT_GT(outcome.left, 0.999);
  EXPECT_EQ(outcome.left_at_clock, outcome.left);
}

// The Liberation's bosses, one HP each but `wall`'s, and a Genesis weapon of
// the plain sword's type.
std::unique_ptr<GameState> LiberationState(const std::string& wall) {
  std::unique_ptr<GameState> state = WallState();
  for (const LiberationMission& mission : kLiberationMissions) {
    Boss boss;
    boss.set_name(mission.boss);
    BossDifficulty* difficulty = boss.add_difficulties();
    difficulty->set_name(mission.difficulty);
    difficulty->set_time_limit_seconds(600);
    AddPhase(difficulty, mission.boss == wall ? "wall" : "opener");
    state->bosses[mission.boss] = boss;
  }
  EquipPrototype genesis = PlainSword();
  genesis.set_name("Genesis Sword");
  genesis.set_set_family("Genesis Warrior Weapon");
  state->equips["genesis_sword"] = genesis;
  return state;
}

TEST(FightLiberationTest, WinningEveryMissionPaysTheWeapon) {
  std::unique_ptr<GameState> state = LiberationState("");
  BossOutcome outcome = FightLiberation(*state);
  EXPECT_TRUE(outcome.won);
  EXPECT_TRUE(Liberated(state->character));
  ASSERT_EQ(outcome.reward.items.size(), 1u);
  EXPECT_EQ(outcome.reward.items[0].name, "Genesis Sword");
}

// Lost at the fifth mission: the two after it and nearly all of the fifth are
// left.
TEST(FightLiberationTest, ALossEndsTheRunAndCountsWhatWasLeft) {
  std::unique_ptr<GameState> state = LiberationState("will");
  BossOutcome outcome = FightLiberation(*state);
  EXPECT_FALSE(outcome.won);
  EXPECT_FALSE(Liberated(state->character));
  EXPECT_GT(outcome.left, 2.9 / kLiberationMissionCount);
  EXPECT_LT(outcome.left, 3.0 / kLiberationMissionCount);
}

}  // namespace
}  // namespace ms
