#include "src/combat/boss_run.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
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

// The two things a rewarded fight can drop, by the keys its table names.
std::map<std::string, ItemPrototype> DropItems() {
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  shard.set_category(ITEM_CATEGORY_ETC);
  shard.set_max_stack(100);
  return {{"shard", shard}};
}

std::map<std::string, EquipPrototype> DropEquips() {
  EquipPrototype mark;
  mark.set_name("Condensed Power Crystal");
  mark.set_equip_slot(EQUIP_SLOT_FACE_ACCESSORY);
  return {{"mark", mark}};
}

// The same fight with something to pay: a fixed purse, a certain drop and one
// that has to be rolled for.
Boss RewardingBoss(double mark_chance = 0.5) {
  Boss boss = TwoPhaseBoss();
  BossDifficulty* normal = boss.mutable_difficulties(0);
  normal->set_meso(3062500);
  MobDrop* mark = normal->add_drops();
  mark->set_equip("mark");
  mark->set_per_kill(mark_chance);
  MobDrop* shard = normal->add_drops();
  shard->set_item("shard");
  shard->set_per_kill(1.0);
  return boss;
}

std::unique_ptr<GameState> MakeState(int arm_hp = 1, int body_hp = 1) {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      DropEquips(), std::map<std::string, Scroll>{}, DropItems(),
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
  // The monsters are already on screen, at full HP and with the clock unspent:
  // the three seconds are for looking at what is about to be fought.
  ASSERT_EQ(run.slots().size(), 2u);
  EXPECT_DOUBLE_EQ(run.slots()[0].hp_fraction, 1.0);
  EXPECT_DOUBLE_EQ(run.phase_hp_fraction(), 1.0);
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

// A win holds its last beat, but an abort does not: the player asked to leave
// and there is nothing left to watch.
TEST(BossRunTest, AbortingEndsItWithNoClosingBeat) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  run.Abort();
  EXPECT_EQ(run.state(), BossRunState::kAborted);
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

// The whole point of the reward table: a clear pays it, and the run remembers
// what it paid so the card can name it.
TEST(BossRunTest, AClearPaysTheMesoAndTheCertainDrop) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  EXPECT_EQ(state->character.proto().meso(), 3062500);
  EXPECT_EQ(run.reward().meso, 3062500);
  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_EQ(run.reward().items[0].name, "Zakum's Soul Shard");
  EXPECT_EQ(run.reward().items[0].count, 1);
  EXPECT_EQ(state->character.CountStackable(DropItems().at("shard")), 1);
}

// Once, not once per phase and not once per beat held afterwards.
TEST(BossRunTest, TheRewardIsPaidOnlyOnce) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);
  for (int i = 0; i < 20; ++i) {
    run.Advance(*state, 1.0);
  }

  EXPECT_EQ(state->character.proto().meso(), 3062500);
  EXPECT_EQ(run.reward().items.size(), 2u);
  EXPECT_EQ(state->character.CountOwned(DropEquips().at("mark")), 1);
}

TEST(BossRunTest, AFightThatRanOutOfTimePaysNothing) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  boss.mutable_difficulties(0)->set_time_limit_seconds(5);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_EQ(run.state(), BossRunState::kTimedOut);
  EXPECT_EQ(state->character.proto().meso(), 0);
  EXPECT_EQ(run.reward().meso, 0);
  EXPECT_TRUE(run.reward().items.empty());
}

// A drop that misses its roll is not on the card. Over many runs it lands
// about half the time, which is what the table asks for.
TEST(BossRunTest, AChanceDropIsRolledFor) {
  int landed = 0;
  for (int i = 0; i < 200; ++i) {
    std::unique_ptr<GameState> state = MakeState();
    Boss boss = RewardingBoss(/*mark_chance=*/0.5);
    BossRun run("zakum", boss, 0);
    RunToEnd(run, *state);
    ASSERT_TRUE(run.won());
    // The shard is certain, so anything above one row is the accessory.
    if (run.reward().items.size() == 2u) {
      ++landed;
      EXPECT_EQ(run.reward().items[0].name, "Condensed Power Crystal");
    }
  }
  EXPECT_GT(landed, 60);
  EXPECT_LT(landed, 140);
}

}  // namespace
}  // namespace ms
