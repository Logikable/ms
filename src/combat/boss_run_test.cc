#include "src/combat/boss_run.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/character/honor.h"
#include "src/combat/test_authority.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/testing/prototypes.h"

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
  BossPhase* first = normal->add_phases();
  Spawn* arms = first->add_spawns();
  arms->set_mob("arm");
  for (int i = 0; i < 2; ++i) {
    arms->add_spots()->set_x(i * 4);
  }
  ArenaSpot* first_stand = first->add_player_spots();
  first_stand->set_x(2);
  first_stand->set_y(1);
  BossPhase* second = normal->add_phases();
  Spawn* body = second->add_spawns();
  body->set_mob("body");
  body->add_spots()->set_x(2);
  ArenaSpot* second_stand = second->add_player_spots();
  second_stand->set_x(2);
  second_stand->set_y(1);
  return boss;
}

// The two things a rewarded fight can drop, by the keys its table names.
std::map<std::string, ItemPrototype> DropItems() {
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  shard.set_category(ITEM_CATEGORY_ETC);
  shard.set_kind(ITEM_KIND_SOUL_SHARD);
  shard.set_max_stack(100);
  ItemPrototype token;
  token.set_name("Cygnus Shoulder Token");
  token.set_category(ITEM_CATEGORY_ETC);
  token.set_kind(ITEM_KIND_TOKEN);
  token.set_max_stack(100);
  return {{"shard", shard}, {"token", token}};
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
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_meso(3062500);
  normal->set_exp(4611597);
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
  EquipPrototype sword = PlainSword();
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

// Zakum's own arena: the arms down the middle, a floor of three under them
// and a ledge over each end.
BossPhase ZakumArenaPhase() {
  BossPhase phase;
  phase.set_arena_width(7);
  phase.set_arena_height(6);
  // The floor's middle first: that is where the phase starts them.
  const int kSpots[5][2] = {{3, 5}, {0, 3}, {6, 3}, {0, 5}, {6, 5}};
  for (const int (&spot)[2] : kSpots) {
    ArenaSpot* at = phase.add_player_spots();
    at->set_x(spot[0]);
    at->set_y(spot[1]);
  }
  return phase;
}

// Indices into ZakumArenaPhase's spots, in the order it writes them.
constexpr int kFloorMiddle = 0;
constexpr int kLedgeLeft = 1;
constexpr int kLedgeRight = 2;
constexpr int kFloorLeft = 3;
constexpr int kFloorRight = 4;

// The same arena, as a fight that can be walked around in.
Boss WalkableBoss() {
  Boss boss = TwoPhaseBoss();
  BossDifficulty* normal = boss.mutable_difficulties(0);
  BossPhase arena = ZakumArenaPhase();
  for (int i = 0; i < normal->phases_size(); ++i) {
    BossPhase* phase = normal->mutable_phases(i);
    *phase->mutable_player_spots() = arena.player_spots();
    phase->set_arena_width(arena.arena_width());
    phase->set_arena_height(arena.arena_height());
  }
  return boss;
}

// Left and right walk the floor, and a press with nothing that way stays put.
TEST(BossRunTest, TheFloorWalksLeftAndRightAndStopsAtItsEnds) {
  BossPhase phase = ZakumArenaPhase();
  EXPECT_EQ(NextPlayerSpot(phase, kFloorMiddle, -1, 0), kFloorLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorLeft, -1, 0), kFloorLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorMiddle, 1, 0), kFloorRight);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorRight, 1, 0), kFloorRight);
}

// The ledge over each end is up from the floor beneath it and down again.
TEST(BossRunTest, TheLedgesAreUpFromTheFloorTheyStandOver) {
  BossPhase phase = ZakumArenaPhase();
  EXPECT_EQ(NextPlayerSpot(phase, kFloorLeft, 0, -1), kLedgeLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, 0, 1), kFloorLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorRight, 0, -1), kLedgeRight);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeRight, 0, 1), kFloorRight);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, 0, -1), kLedgeLeft)
      << "nothing over the ledge";
}

// Stepping off a ledge inward is the middle of the floor: it is nearer that
// way than the ledge across the arena, and the arms stand between them.
TEST(BossRunTest, LeavingALedgeSidewaysLandsInTheMiddle) {
  BossPhase phase = ZakumArenaPhase();
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, 1, 0), kFloorMiddle);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeRight, -1, 0), kFloorMiddle);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, -1, 0), kLedgeLeft);
}

// Horntail's arena: six spots down the two edges, around a dragon that fills
// the middle.
BossPhase HorntailArenaPhase() {
  BossPhase phase;
  phase.set_arena_width(9);
  phase.set_arena_height(6);
  // The bottom-left corner first: that is where the phase starts them.
  const int kSpots[6][2] = {{0, 4}, {0, 0}, {8, 0}, {0, 2}, {8, 2}, {8, 4}};
  for (const int (&spot)[2] : kSpots) {
    ArenaSpot* at = phase.add_player_spots();
    at->set_x(spot[0]);
    at->set_y(spot[1]);
  }
  return phase;
}

// A corner behaves like a corner: across the top is the far top corner, not
// the spot under the tail, which is nearer along the arrow but nowhere near
// the way it points.
TEST(BossRunTest, APressIgnoresWhatIsFurtherAcrossThanAlong) {
  BossPhase phase = HorntailArenaPhase();
  constexpr int kBottomLeft = 0;
  constexpr int kTopLeft = 1;
  constexpr int kTopRight = 2;
  constexpr int kMiddleLeft = 3;
  constexpr int kBottomRight = 5;
  EXPECT_EQ(NextPlayerSpot(phase, kTopLeft, 1, 0), kTopRight);
  EXPECT_EQ(NextPlayerSpot(phase, kBottomLeft, 1, 0), kBottomRight);
  EXPECT_EQ(NextPlayerSpot(phase, kTopLeft, 0, 1), kMiddleLeft)
      << "down the edge one spot at a time";
  EXPECT_EQ(NextPlayerSpot(phase, kMiddleLeft, 0, 1), kBottomLeft);
}

// The two ledges are as far from the middle of the floor as each other, and a
// press with no one answer moves nobody.
TEST(BossRunTest, APressWithTwoAnswersMovesNobody) {
  EXPECT_EQ(NextPlayerSpot(ZakumArenaPhase(), kFloorMiddle, 0, -1),
            kFloorMiddle);
}

// The run walks the player, and every phase starts them where it says --
// where they walked to in the last one was in a different arena.
TEST(BossRunTest, WalkingIsRememberedWithinAPhaseAndResetByTheNext) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = WalkableBoss();
  BossRun run("zakum", boss, 0);
  EXPECT_EQ(run.player_spot().x(), 3);
  run.MovePlayer(-1, 0);
  EXPECT_EQ(run.player_spot().x(), 0);
  EXPECT_EQ(run.player_spot().y(), 5);
  run.MovePlayer(0, -1);
  EXPECT_EQ(run.player_spot().y(), 3) << "the ledge, and still there";

  RunToEnd(run, *state);
  EXPECT_TRUE(run.won());
  // Phase 2 stood them back in the middle of its own floor on the way in.
  EXPECT_EQ(run.player_spot().x(), 3);
  EXPECT_EQ(run.player_spot().y(), 5);
}

// A phase that names nowhere to stand keeps the player where it put them.
TEST(BossRunTest, AFightWithNoSpotsDoesNotWalk) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.MovePlayer(-1, 0);
  EXPECT_EQ(run.player_spot().x(), 2);
  EXPECT_EQ(run.player_spot().y(), 1);
}

// The arena is measured off everywhere the player may stand as well as the
// bars, so a phase that asks for no room of its own still holds all of it.
TEST(BossRunTest, TheArenaHoldsEverySpotThePlayerMayStandOn) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  ArenaSpot* far = phase->add_player_spots();
  far->set_x(8);
  far->set_y(3);
  BossRun run("zakum", boss, 0);
  EXPECT_EQ(run.arena_width(), 9);
  EXPECT_EQ(run.arena_height(), 4);
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

// Each arm gets a bar of its own, standing where its own spawn's spot says.
// The arena is measured off the spots when the phase asks for no room of its
// own.
TEST(BossRunTest, EveryArmGetsItsOwnBarWhereItsPhasePutsIt) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  ASSERT_EQ(run.slots().size(), 2u);
  EXPECT_EQ(run.slots()[0].name, "Zakum's Arm");
  EXPECT_TRUE(run.slots()[0].alive);
  EXPECT_NE(run.slots()[0].id, run.slots()[1].id);
  EXPECT_NEAR(run.phase_hp_fraction(), 1.0, 0.001);
  EXPECT_EQ(run.slots()[0].x, 0);
  EXPECT_EQ(run.slots()[1].x, 4);
  EXPECT_EQ(run.player_spot().x(), 2);
  EXPECT_EQ(run.player_spot().y(), 1);
  // No margin: the arena is the cell furthest right, and the row under it the
  // player stands in.
  EXPECT_EQ(run.arena_width(), 5);
  EXPECT_EQ(run.arena_height(), 2);
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
  EXPECT_EQ(run.reward().exp, 4611597);
  EXPECT_EQ(run.reward().honor, kBossClearHonor);
  // The prize, on top of what the levels the fight's EXP paid for are worth.
  EXPECT_EQ(
      state->character.honor(),
      kBossClearHonor + HonorForLevels(1, state->character.proto().level()));
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

// The honor is the day's prize, so a fight the calendar does not hold back
// pays none of it however often it is cleared.
TEST(BossRunTest, AFightWithNoLockoutPaysNoHonor) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  boss.mutable_difficulties(0)->clear_reset();
  boss.mutable_difficulties(0)->clear_exp();  // so no level pays honor either
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  EXPECT_EQ(run.reward().honor, 0);
  EXPECT_EQ(state->character.honor(), 0);
}

// What the clear card reads: the fight's own clock, count-in aside.
TEST(BossRunTest, AClearRemembersHowLongItTook) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  EXPECT_GT(run.clear_seconds(), 0.0);
  EXPECT_DOUBLE_EQ(
      run.clear_seconds(),
      boss.difficulties(0).time_limit_seconds() - run.seconds_left());
}

TEST(BossRunTest, AFightThatRanOutOfTimePaysNothing) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  boss.mutable_difficulties(0)->set_time_limit_seconds(5);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_EQ(run.state(), BossRunState::kTimedOut);
  EXPECT_EQ(run.clear_seconds(), 0.0);
  EXPECT_EQ(state->character.proto().meso(), 0);
  EXPECT_EQ(run.reward().meso, 0);
  EXPECT_EQ(run.reward().exp, 0);
  EXPECT_TRUE(run.reward().items.empty());
}

// Drop rate lifts a chance and never a certainty: the shard the table
// guarantees is one shard, not one and a fifth of a second roll.
TEST(BossRunTest, DropRateDoesNotDoubleACertainDrop) {
  std::unique_ptr<GameState> state = MakeState();
  EquipPrototype hat;
  hat.set_name("Lucky Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.mutable_base_stats()->set_item_drop_rate(200);
  state->character.PickUp(std::make_unique<EquipInstance>(hat));
  state->character.Equip(0);

  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_EQ(run.reward().items[0].count, 1);
  EXPECT_FALSE(run.reward().items[0].prize);
  EXPECT_EQ(state->character.CountStackable(DropItems().at("shard")), 1);
}

// The card lists the prizes apart from the rest, at the rate they fell at, so
// it can say which of its rows is what the player came for and lead with the
// rarest of them.
TEST(BossRunTest, AClearMarksWhichDropsArePrizesAndAtWhatRate) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_EQ(run.reward().items.size(), 2u);
  EXPECT_TRUE(run.reward().items[0].prize);
  EXPECT_DOUBLE_EQ(run.reward().items[0].chance, 1.0);
  EXPECT_FALSE(run.reward().items[1].prize);
  EXPECT_DOUBLE_EQ(run.reward().items[1].chance, 1.0);
}

// A token is a prize too: the shop trades it for a piece of gear, so it is
// listed with the gear rather than with what every clear pays.
TEST(BossRunTest, ATokenIsAPrize) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  boss.mutable_difficulties(0)->mutable_drops(1)->set_item("token");
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_EQ(run.reward().items[0].name, "Cygnus Shoulder Token");
  EXPECT_TRUE(run.reward().items[0].prize);
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

// A landing on a monster leaves one stack against that monster, and nothing
// is left against a monster nothing hit.
TEST(BossRunTest, ASwingLeavesAStackOnWhatItHit) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  EXPECT_TRUE(run.damage_stacks().empty()) << "nothing has swung yet";

  run.Advance(*state, kBossCountdownSeconds + 1.0);
  ASSERT_FALSE(run.damage_stacks().empty());
  ASSERT_FALSE(run.slots().empty());
  for (const DamageStack& stack : run.damage_stacks()) {
    EXPECT_EQ(stack.mob_id, run.slots()[0].id) << "one arm was in reach";
    EXPECT_FALSE(stack.lines.empty());
    for (const DamageNumber& line : stack.lines) {
      EXPECT_GE(line.damage, 1);
    }
  }
}

// One monster holds one stack per source: the swing keeps rewriting its own
// numbers rather than piling a second lot beside them. Two arms stand here and
// the swing takes the healthier of them, so the count to watch is per arm.
TEST(BossRunTest, ASwingReplacesItsOwnNumbers) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds + 1.0);
  ASSERT_EQ(run.damage_stacks().size(), 1u);

  // Three seconds of swinging, which is several swings inside one stack's life:
  // without the rule they would pile up beside each other.
  int landed = 0;
  std::map<int, double> age;  // the age of the stack each arm is holding
  for (int step = 0; step < 60; ++step) {
    run.Advance(*state, 0.05);
    std::set<int> held;
    for (const DamageStack& stack : run.damage_stacks()) {
      ASSERT_TRUE(held.insert(stack.mob_id).second)
          << "two stacks on one arm at step " << step;
      // A stack younger than the one that arm held is a fresh one in its place.
      std::map<int, double>::iterator was = age.find(stack.mob_id);
      landed += was == age.end() || stack.age < was->second ? 1 : 0;
      age[stack.mob_id] = stack.age;
    }
  }
  EXPECT_GT(landed, 1) << "several swings landed";
}

// The numbers are an animation: they age off on their own, whether or not
// anything else is happening.
TEST(BossRunTest, AStackFadesAfterItsTime) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds + 1.0);
  ASSERT_FALSE(run.damage_stacks().empty());

  run.Advance(*state, kDamageStackSeconds);
  for (const DamageStack& stack : run.damage_stacks()) {
    EXPECT_LT(stack.age, kDamageStackSeconds);
  }
}

// A phase turning over takes the numbers with it: the ids they name belong to
// the encounter that handed them out, and the arena is a different one.
TEST(BossRunTest, APhaseChangeClearsTheNumbers) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  for (int i = 0; i < 200 && run.phase() == 1; ++i) {
    run.Advance(*state, 0.1);
  }
  ASSERT_EQ(run.phase(), 2);

  // Whatever is on screen belongs to the phase being fought. The arms left
  // numbers, and none of them survived the turnover.
  ASSERT_FALSE(run.slots().empty());
  for (const DamageStack& stack : run.damage_stacks()) {
    EXPECT_EQ(stack.mob_id, run.slots()[0].id);
  }
}

TEST(BossRunTest, AFollowedRunWaitsToBeToldAnything) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  authority.open_ = false;
  BossRun run("zakum", boss, 0, &authority);

  run.Advance(*state, 10.0);
  EXPECT_EQ(run.state(), BossRunState::kCountdown);
  EXPECT_EQ(run.seconds_left(), 300.0);
  EXPECT_TRUE(authority.reported_.empty());
}

TEST(BossRunTest, AFollowedRunTakesTheClockAndThePhaseItIsGiven) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  authority.fight_.seconds_left = 42.0;
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);

  EXPECT_EQ(run.state(), BossRunState::kFighting);
  EXPECT_EQ(run.seconds_left(), 42.0);
  EXPECT_EQ(run.phase(), 1);
  ASSERT_EQ(run.members().size(), 2u);
  // This player first, whoever the server holds first.
  EXPECT_TRUE(run.members()[0].name.empty());
  EXPECT_EQ(run.members()[1].name, "Wand");
  EXPECT_EQ(run.members()[1].spot, 1);

  authority.fight_.phase = 1;
  authority.fight_.hp_fractions.assign(1, 1.0);
  run.Advance(*state, 0.1);
  EXPECT_EQ(run.phase(), 2);
}

TEST(BossRunTest, AFollowedRunReportsWhatItLanded) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);
  for (int i = 0; i < 40 && authority.reported_.empty(); ++i) {
    run.Advance(*state, 0.1);
  }

  ASSERT_FALSE(authority.reported_.empty());
  EXPECT_EQ(authority.reported_phase_, 0);
  EXPECT_EQ(authority.reported_spot_, 0);
  EXPECT_FALSE(authority.reported_attack_.empty());
  // Slots, not monster ids: an id is handed out per client.
  for (const SharedLine& line : authority.reported_) {
    EXPECT_GE(line.slot, 0);
    EXPECT_LT(line.slot, 2);
    EXPECT_GT(line.damage, 0);
  }
  // The same numbers the player watched, so the two cannot drift.
  int64_t drawn = 0;
  for (const DamageStack& stack : run.damage_stacks()) {
    for (const DamageNumber& number : stack.lines) {
      drawn += number.damage;
    }
  }
  EXPECT_GT(drawn, 0);
}

TEST(BossRunTest, TheSharedRosterSaysWhatIsLeft) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);
  ASSERT_EQ(run.slots().size(), 2u);

  // What the party did to the first monster, which this client never swung at.
  authority.fight_.hp_fractions[0] = 0.25;
  run.Advance(*state, 0.1);
  EXPECT_NEAR(run.slots()[0].hp_fraction, 0.25, 0.001);

  authority.fight_.hp_fractions[0] = 0.0;
  run.Advance(*state, 0.1);
  EXPECT_FALSE(run.slots()[0].alive);
}

TEST(BossRunTest, EverybodysNumbersAreDrawnAndHeldApart) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);
  // Far enough in that this player has landed a swing of their own.
  for (int i = 0; i < 40 && run.damage_stacks().empty(); ++i) {
    run.Advance(*state, 0.1);
  }
  authority.OtherLanded(0, 1234);
  run.Advance(*state, 0.1);

  int mine = 0;
  int theirs = 0;
  for (const DamageStack& stack : run.damage_stacks()) {
    if (stack.owner == 0) {
      ++mine;
    } else {
      ++theirs;
      EXPECT_EQ(stack.owner, 1);
      ASSERT_EQ(stack.lines.size(), 1u);
      EXPECT_EQ(stack.lines[0].damage, 1234);
    }
  }
  EXPECT_GT(mine, 0);
  EXPECT_EQ(theirs, 1);

  // One stack per player per source: theirs replaces theirs, not mine.
  authority.OtherLanded(0, 4321);
  run.Advance(*state, 0.1);
  int still_theirs = 0;
  for (const DamageStack& stack : run.damage_stacks()) {
    if (stack.owner != 0) {
      ++still_theirs;
      EXPECT_EQ(stack.lines[0].damage, 4321);
    }
  }
  EXPECT_EQ(still_theirs, 1);
}

TEST(BossRunTest, AWalkPassesOverSomebodyElsesSpot) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = WalkableBoss();
  TestAuthority authority(2);
  authority.fight_.players[1].spot = kFloorRight;
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);
  ASSERT_EQ(run.members()[0].spot, kFloorMiddle);

  // The floor's right end is taken, so walking right goes past it to the
  // ledge over it rather than stopping short or standing on somebody.
  run.MovePlayer(1, 0);
  EXPECT_EQ(run.members()[0].spot, kLedgeRight);
  run.MovePlayer(-1, 0);
  EXPECT_EQ(run.members()[0].spot, kFloorMiddle);
}

TEST(BossRunTest, APlayerWhoLeavesGoesFromTheArena) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = WalkableBoss();
  TestAuthority authority(2);
  authority.fight_.players[1].spot = kFloorRight;
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);
  ASSERT_EQ(run.members().size(), 2u);

  authority.fight_.players[1].present = false;
  authority.OtherLanded(0, 1234);
  run.Advance(*state, 0.1);

  // Their panel goes, nothing more of theirs is drawn, and the spot they
  // stood on is somewhere to walk to again.
  ASSERT_EQ(run.members().size(), 1u);
  for (const DamageStack& stack : run.damage_stacks()) {
    EXPECT_EQ(stack.owner, 0);
  }
  run.MovePlayer(1, 0);
  EXPECT_EQ(run.members()[0].spot, kFloorRight);
}

TEST(BossRunTest, ASharedClearPaysEachOfThemAShare) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  TestAuthority authority(2);
  authority.fight_.share_count = 2;
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);

  authority.fight_.state = BossRunState::kWon;
  run.Advance(*state, 0.1);
  ASSERT_TRUE(run.won());
  EXPECT_EQ(run.share_count(), 2);
  // Half the purse, and the whole of the EXP and the honor.
  EXPECT_EQ(run.reward().meso, boss.difficulties(0).meso() / 2);
  EXPECT_EQ(run.reward().exp, boss.difficulties(0).exp());
  EXPECT_EQ(run.reward().honor, kBossClearHonor);
  // The drops are the authority's to deal. It dealt none, so none were paid,
  // certain though the table says the shard is.
  EXPECT_TRUE(run.reward().items.empty());
}

// A shared run rolls nothing: it pays what it was dealt, and every unit of it.
TEST(BossRunTest, ASharedClearPaysTheDropsItWasDealt) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  TestAuthority authority(2);
  authority.fight_.share_count = 2;
  SharedAward award;
  award.drop.set_item("shard");
  award.count = 3;
  authority.fight_.awards.push_back(award);
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);

  authority.fight_.state = BossRunState::kWon;
  run.Advance(*state, 0.1);
  ASSERT_TRUE(run.won());
  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_EQ(run.reward().items[0].name, "Zakum's Soul Shard");
  EXPECT_EQ(run.reward().items[0].count, 3);
  EXPECT_EQ(state->character.CountStackable(DropItems().at("shard")), 3);
  // The mark is certain in the table and was not dealt, so it was not paid.
  EXPECT_EQ(state->character.CountOwned(DropEquips().at("mark")), 0);
}

// The run tells the authority its Item Drop Rate, because the clear rolls
// against the best one in the party.
TEST(BossRunTest, ASharedRunReportsItsDropRate) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  EquipPrototype hat;
  hat.set_name("Lucky Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.mutable_base_stats()->set_item_drop_rate(50);
  state->character.PickUp(std::make_unique<EquipInstance>(hat));
  state->character.Equip(0);

  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);

  EXPECT_DOUBLE_EQ(authority.reported_drop_pct_, 0.5);
}

}  // namespace
}  // namespace ms
