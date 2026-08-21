#include "src/combat/boss_run.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int max_hp, int64_t exp) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(110);
  mob.set_max_hp(max_hp);
  mob.set_exp(exp);
  mob.set_boss(true);
  return mob;
}

// Two arms then a body, small enough that a swing or two clears each.
Boss TwoPhaseBoss(int time_limit = 300) {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(time_limit);
  Spawn* arms = normal->add_phases()->add_spawns();
  arms->set_mob("arm");
  arms->set_count(2);
  Spawn* body = normal->add_phases()->add_spawns();
  body->set_mob("body");
  body->set_count(1);
  return boss;
}

std::unique_ptr<GameState> MakeState(int arm_hp = 1, int body_hp = 1) {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"arm", MakeMob("Zakum's Arm", arm_hp, 0)},
                                 {"body", MakeMob("Zakum", body_hp, 4750740)}},
      std::map<std::string, MapData>{});
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state->character.PickUp(std::make_unique<EquipInstance>(sword));
  state->character.Equip(0);
  return state;
}

// Runs the fight to its end, or until it plainly is not going to end.
void RunToEnd(BossRun& run, GameState& state, double step = 0.1,
              int max_steps = 20000) {
  for (int i = 0; i < max_steps && !run.done(); ++i) {
    run.Advance(state, step);
  }
}

TEST(BossRunTest, NothingHappensUntilTheCountdownIsUp) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  EXPECT_EQ(run.state(), BossRunState::kCountdown);
  EXPECT_DOUBLE_EQ(run.countdown_left(), kBossCountdownSeconds);
  EXPECT_EQ(run.title(), "Normal Zakum");
  EXPECT_EQ(run.phase(), 1);
  EXPECT_EQ(run.phase_count(), 2);

  run.Advance(*state, kBossCountdownSeconds - 0.5);
  EXPECT_EQ(run.state(), BossRunState::kCountdown);
  EXPECT_TRUE(run.slots().empty());
  EXPECT_DOUBLE_EQ(run.seconds_left(), 300.0);

  run.Advance(*state, 0.5);
  EXPECT_EQ(run.state(), BossRunState::kFighting);
  EXPECT_DOUBLE_EQ(run.countdown_left(), 0.0);
}

TEST(BossRunTest, EveryArmGetsItsOwnBar) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_EQ(run.slots().size(), 2u);
  EXPECT_EQ(run.slots()[0].name, "Zakum's Arm");
  EXPECT_TRUE(run.slots()[0].alive);
  EXPECT_NE(run.slots()[0].id, run.slots()[1].id);
  EXPECT_NEAR(run.phase_hp_fraction(), 1.0, 0.001);
}

// A dead bar holds its slot for a beat and then leaves it empty: the arms
// beside it never move.
TEST(BossRunTest, ADeadBarFadesAndItsSlotStaysEmpty) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000000);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_EQ(run.slots().size(), 2u);
  int first_id = run.slots()[0].id;

  for (int i = 0; i < 200 && run.slots()[0].alive; ++i) {
    run.Advance(*state, 0.05);
  }
  ASSERT_EQ(run.slots().size(), 2u);
  EXPECT_EQ(run.slots()[0].id, first_id);
  EXPECT_FALSE(run.slots()[0].alive);
  EXPECT_TRUE(run.slots()[0].visible);
  EXPECT_DOUBLE_EQ(run.slots()[0].hp_fraction, 0.0);

  run.Advance(*state, kBossDeathHoldSeconds);
  EXPECT_FALSE(run.slots()[0].visible);
  EXPECT_EQ(run.slots().size(), 2u);
}

TEST(BossRunTest, TheBodyArrivesAfterTheArmsAndTheClearPaysItsExp) {
  std::unique_ptr<GameState> state = MakeState();
  int64_t before = state->character.proto().exp();
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  EXPECT_TRUE(run.won());
  EXPECT_TRUE(run.done());
  EXPECT_EQ(run.phase(), 2);
  // The arms are worth nothing, so every point of it is the body's.
  EXPECT_GT(state->character.proto().exp() + state->character.proto().level(),
            before);
  EXPECT_EQ(state->character.proto().meso(), 0)
      << "a boss should pay no field meso";
}

// The clock is the only thing that can beat the player, since nothing hits
// back yet.
TEST(BossRunTest, RunningOutOfTimeEndsTheFight) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss(5);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  EXPECT_EQ(run.state(), BossRunState::kTimedOut);
  EXPECT_FALSE(run.won());
  EXPECT_DOUBLE_EQ(run.seconds_left(), 0.0);
  EXPECT_EQ(run.phase(), 1);
}

TEST(BossRunTest, AbortingEndsItAfterTheClosingBeat) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  run.Abort();
  EXPECT_EQ(run.state(), BossRunState::kAborted);
  EXPECT_FALSE(run.done());
  run.Advance(*state, kBossEndHoldSeconds);
  EXPECT_TRUE(run.done());

  // A finished run does not step again.
  double left = run.seconds_left();
  run.Advance(*state, 10.0);
  EXPECT_DOUBLE_EQ(run.seconds_left(), left);
}

TEST(BossRunTest, ADifficultyThatDoesNotExistIsOverBeforeItStarts) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 3);
  EXPECT_EQ(run.state(), BossRunState::kAborted);
  run.Advance(*state, 1.0);
  EXPECT_TRUE(run.done());
}

}  // namespace
}  // namespace ms
