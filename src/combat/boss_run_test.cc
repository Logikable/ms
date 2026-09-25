#include "src/combat/boss_run.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

// `count` lines with damage counting up from `base`, so a test can tell which
// row it is reading and which write put the number there.
std::vector<DamageNumber> Numbers(int count, int64_t base) {
  std::vector<DamageNumber> lines;
  for (int i = 0; i < count; ++i) {
    lines.push_back({base + i, false});
  }
  return lines;
}

Mob MakeMob(const std::string& name, int max_hp, int64_t exp) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(110);
  mob.set_max_hp(max_hp);
  mob.set_exp(exp);
  mob.set_boss(true);
  return mob;
}

// Two arms then a body, weak enough that an attack or two clears each.
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

// The two items a rewarding fight can drop, keyed as its table names them.
std::map<std::string, ItemPrototype> DropItems() {
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  shard.set_kind(ITEM_KIND_SOUL_SHARD);
  shard.set_max_stack(100);
  ItemPrototype token;
  token.set_name("Cygnus Shoulder Token");
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

// The same fight with rewards: fixed meso, a certain drop, and a rolled one.
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

// Gives the character `rate` percent Item Drop Rate on their hat.
void WearDropGear(GameState& state, int rate) {
  EquipPrototype hat;
  hat.set_name("Lucky Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.mutable_base_stats()->set_item_drop_rate(rate);
  state.character.PickUp(std::make_unique<EquipInstance>(hat));
  state.character.Equip(0);
}

// Runs the fight to its end, or until it is clearly never going to end.
void RunToEnd(BossRun& run, GameState& state, double step = 0.1,
              int max_steps = 20000) {
  for (int i = 0; i < max_steps && !run.done(); ++i) {
    run.Advance(state, step);
  }
}

// Zakum's arena: the arms down the middle, a floor of three spots under them,
// and a ledge above each end.
BossPhase ZakumArenaPhase() {
  BossPhase phase;
  phase.set_arena_width(7);
  phase.set_arena_height(6);
  // The middle of the floor first, since the phase starts the player there.
  const int kSpots[5][2] = {{3, 5}, {0, 3}, {6, 3}, {0, 5}, {6, 5}};
  for (const int (&spot)[2] : kSpots) {
    ArenaSpot* at = phase.add_player_spots();
    at->set_x(spot[0]);
    at->set_y(spot[1]);
  }
  return phase;
}

// Indices into ZakumArenaPhase's spots, in the order it lists them.
constexpr int kFloorMiddle = 0;
constexpr int kLedgeLeft = 1;
constexpr int kLedgeRight = 2;
constexpr int kFloorLeft = 3;
constexpr int kFloorRight = 4;

// The same arena, as a fight the player can walk around in.
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

// Left and right walk along the floor. A press with nothing in that direction
// does nothing.
TEST(BossRunTest, TheFloorWalksLeftAndRightAndStopsAtItsEnds) {
  BossPhase phase = ZakumArenaPhase();
  EXPECT_EQ(NextPlayerSpot(phase, kFloorMiddle, -1, 0), kFloorLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorLeft, -1, 0), kFloorLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorMiddle, 1, 0), kFloorRight);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorRight, 1, 0), kFloorRight);
}

// Each ledge is up from the floor spot beneath it, and down leads back.
TEST(BossRunTest, TheLedgesAreUpFromTheFloorTheyStandOver) {
  BossPhase phase = ZakumArenaPhase();
  EXPECT_EQ(NextPlayerSpot(phase, kFloorLeft, 0, -1), kLedgeLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, 0, 1), kFloorLeft);
  EXPECT_EQ(NextPlayerSpot(phase, kFloorRight, 0, -1), kLedgeRight);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeRight, 0, 1), kFloorRight);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, 0, -1), kLedgeLeft)
      << "nothing over the ledge";
}

// Stepping sideways off a ledge toward the centre lands in the middle of the
// floor. It is nearer than the far ledge, which is across the arms.
TEST(BossRunTest, LeavingALedgeSidewaysLandsInTheMiddle) {
  BossPhase phase = ZakumArenaPhase();
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, 1, 0), kFloorMiddle);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeRight, -1, 0), kFloorMiddle);
  EXPECT_EQ(NextPlayerSpot(phase, kLedgeLeft, -1, 0), kLedgeLeft);
}

// Horntail's arena: six spots down the two edges, around a dragon filling the
// middle.
BossPhase HorntailArenaPhase() {
  BossPhase phase;
  phase.set_arena_width(9);
  phase.set_arena_height(6);
  // The bottom-left corner first, since the phase starts the player there.
  const int kSpots[6][2] = {{0, 4}, {0, 0}, {8, 0}, {0, 2}, {8, 2}, {8, 4}};
  for (const int (&spot)[2] : kSpots) {
    ArenaSpot* at = phase.add_player_spots();
    at->set_x(spot[0]);
    at->set_y(spot[1]);
  }
  return phase;
}

// From a corner, pressing across goes to the far corner, not the spot under the
// tail. That spot is nearer, but not in the direction pressed.
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

// The two ledges are the same distance from the middle of the floor, so a press
// that could mean either moves nobody.
TEST(BossRunTest, APressWithTwoAnswersMovesNobody) {
  EXPECT_EQ(NextPlayerSpot(ZakumArenaPhase(), kFloorMiddle, 0, -1),
            kFloorMiddle);
}

// Walking is remembered within a phase. Each new phase starts the player where
// it says, since the last phase was a different arena.
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
  // Phase 2 put them back in the middle of its own floor.
  EXPECT_EQ(run.player_spot().x(), 3);
  EXPECT_EQ(run.player_spot().y(), 5);
}

// A phase with no spots keeps the player where it put them.
TEST(BossRunTest, AFightWithNoSpotsDoesNotWalk) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.MovePlayer(-1, 0);
  EXPECT_EQ(run.player_spot().x(), 2);
  EXPECT_EQ(run.player_spot().y(), 1);
}

// The arena's size covers every spot the player can stand on as well as the
// bars, so a phase that asks for no room still fits them all.
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
  // The monsters are already on screen at full HP, and the clock hasn't
  // started. The three seconds are for looking at what's about to be fought.
  ASSERT_EQ(run.slots().size(), 2u);
  EXPECT_DOUBLE_EQ(run.slots()[0].hp_fraction, 1.0);
  EXPECT_DOUBLE_EQ(run.phase_hp_fraction(), 1.0);
  EXPECT_DOUBLE_EQ(run.seconds_left(), 300.0);

  run.Advance(*state, 0.5);
  EXPECT_EQ(run.state(), BossRunState::kFighting);
  EXPECT_DOUBLE_EQ(run.countdown_left(), 0.0);
}

// Each arm gets its own bar, placed where its spawn's spot says. When the phase
// asks for no room, the arena is sized to fit the spots.
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
  // No margin: the arena ends at the rightmost cell, with the player's row
  // under it.
  EXPECT_EQ(run.arena_width(), 5);
  EXPECT_EQ(run.arena_height(), 2);
}

// Vellum's walk: a monster with an interval moves along its row on the run's
// clock. It never lands where it already stands, and never leaves the arena or
// its row. The bar beside it, with no interval, stays put.
TEST(BossRunTest, AMonsterWithAnIntervalWalksItsRowAndTheRestStandStill) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  Spawn* arms =
      boss.mutable_difficulties(0)->mutable_phases(0)->mutable_spawns(0);
  arms->mutable_walk()->set_interval_ms(30000);
  arms->mutable_walk()->set_range(ArenaWalk::RANGE_ROW);
  // A second spawn with no walk settings, which should stand still.
  Spawn* still = boss.mutable_difficulties(0)->mutable_phases(0)->add_spawns();
  still->set_mob("arm");
  still->add_spots()->set_x(3);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_EQ(run.slots().size(), 3u);
  int home = run.slots()[0].x;
  int width = run.arena_width();

  std::vector<int> seen;
  int last = home;
  for (int step = 0; step < 8; ++step) {
    run.Advance(*state, 30.0);
    int now = run.slots()[0].x;
    EXPECT_NE(now, last) << "step " << step << " left it where it was";
    EXPECT_GE(now, 0);
    EXPECT_LT(now, width);
    EXPECT_EQ(run.slots()[0].y, 0) << "it left its row";
    EXPECT_EQ(run.slots()[2].x, 3) << "the bar with no interval moved";
    seen.push_back(now);
    last = now;
  }
  EXPECT_GT(std::set<int>(seen.begin(), seen.end()).size(), 1u)
      << "it walked between only one cell and its home";
}

// Steps are derived from the clock, not rolled. Two runs of the same fight put
// the monster on the same cell at the same time, so party members see it in the
// same place without sending anything.
TEST(BossRunTest, TheWalkIsTheSameOnEveryClientAndOnlyMovesOnTheBeat) {
  std::unique_ptr<GameState> first_state = MakeState(1000000000, 1);
  std::unique_ptr<GameState> second_state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  boss.mutable_difficulties(0)
      ->mutable_phases(0)
      ->mutable_spawns(0)
      ->mutable_walk()
      ->set_interval_ms(30000);
  BossRun first("zakum", boss, 0);
  BossRun second("zakum", boss, 0);
  first.Advance(*first_state, kBossCountdownSeconds);
  second.Advance(*second_state, kBossCountdownSeconds);
  int home = first.slots()[0].x;

  // Just short of a whole interval: still where it started.
  first.Advance(*first_state, 29.0);
  EXPECT_EQ(first.slots()[0].x, home);
  first.Advance(*first_state, 1.5);
  EXPECT_NE(first.slots()[0].x, home);

  // The other run gets there in one step and lands on the same cell.
  second.Advance(*second_state, 30.5);
  EXPECT_EQ(second.slots()[0].x, first.slots()[0].x);
  EXPECT_EQ(second.slots()[1].x, first.slots()[1].x);
}

// Papulatus's roam: a monster whose walk steps instead of pacing moves one cell
// at a time in any of the four directions, and never onto a cell the player can
// stand on.
TEST(BossRunTest, ASteppingMonsterRoamsTheRoomAndKeepsOffThePlayersCells) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(5);
  phase->set_arena_height(3);
  phase->clear_player_spots();
  for (int x = 0; x < 5; x += 2) {
    ArenaSpot* stand = phase->add_player_spots();
    stand->set_x(x);
    stand->set_y(2);
  }
  Spawn* arms = phase->mutable_spawns(0);
  arms->mutable_walk()->set_interval_ms(500);
  arms->mutable_walk()->set_range(ArenaWalk::RANGE_STEP);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  std::set<std::pair<int, int>> seen;
  int last_x = run.slots()[0].x;
  int last_y = run.slots()[0].y;
  for (int step = 0; step < 200; ++step) {
    run.Advance(*state, 0.5);
    const BossSlot& slot = run.slots()[0];
    EXPECT_EQ(std::abs(slot.x - last_x) + std::abs(slot.y - last_y), 1)
        << "step " << step << " was not one cell";
    EXPECT_GE(slot.x, 0);
    EXPECT_LT(slot.x, 5);
    EXPECT_GE(slot.y, 0);
    EXPECT_LT(slot.y, 3);
    EXPECT_FALSE(slot.y == 2 && slot.x % 2 == 0)
        << "it stood where the player stands";
    seen.insert({slot.x, slot.y});
    last_x = slot.x;
    last_y = slot.y;
  }
  // Every cell except the three the player can stand on.
  EXPECT_EQ(seen.size(), 12u);
}

// A walk kept to its own row: one cell left or right, never up or down, so a
// monster pacing above the players never comes down among them.
TEST(BossRunTest, ARowStepMonsterKeepsToItsOwnRow) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(5);
  phase->set_arena_height(3);
  phase->clear_player_spots();
  Spawn* arms = phase->mutable_spawns(0);
  arms->mutable_walk()->set_interval_ms(500);
  arms->mutable_walk()->set_range(ArenaWalk::RANGE_ROW_STEP);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  int row = run.slots()[0].y;
  int last_x = run.slots()[0].x;
  std::set<int> seen;
  for (int step = 0; step < 100; ++step) {
    run.Advance(*state, 0.5);
    const BossSlot& slot = run.slots()[0];
    EXPECT_EQ(slot.y, row) << "step " << step << " left the row";
    EXPECT_EQ(std::abs(slot.x - last_x), 1) << "step " << step;
    seen.insert(slot.x);
    last_x = slot.x;
  }
  EXPECT_EQ(seen.size(), 5u) << "it paced the whole row";
}

// Two runs of the same stepping fight move the monster the same way. A run
// advanced in one go ends where one advanced beat by beat does.
TEST(BossRunTest, TheRoamIsTheSameOnEveryClientHoweverItIsStepped) {
  std::unique_ptr<GameState> beat_state = MakeState(1000000000, 1);
  std::unique_ptr<GameState> leap_state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(5);
  phase->set_arena_height(3);
  Spawn* arms = phase->mutable_spawns(0);
  arms->mutable_walk()->set_interval_ms(500);
  arms->mutable_walk()->set_range(ArenaWalk::RANGE_STEP);
  BossRun beat("zakum", boss, 0);
  BossRun leap("zakum", boss, 0);
  beat.Advance(*beat_state, kBossCountdownSeconds);
  leap.Advance(*leap_state, kBossCountdownSeconds);

  for (int step = 0; step < 40; ++step) {
    beat.Advance(*beat_state, 0.5);
  }
  leap.Advance(*leap_state, 20.0);
  EXPECT_EQ(leap.slots()[0].x, beat.slots()[0].x);
  EXPECT_EQ(leap.slots()[0].y, beat.slots()[0].y);
}

// Damien's dash: every dash interval, the monster skips its walk step and runs
// a cell at a time, the full length, in one direction. A run advanced in one go
// ends where one advanced beat by beat does.
TEST(BossRunTest, ADashRunsItsCellsOneWayOnItsOwnClock) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  std::unique_ptr<GameState> leap_state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(9);
  phase->set_arena_height(2);
  ArenaWalk* walk = phase->mutable_spawns(0)->mutable_walk();
  walk->set_interval_ms(30000);
  walk->set_range(ArenaWalk::RANGE_ROW);
  walk->mutable_dash()->set_interval_ms(10000);
  walk->mutable_dash()->set_cells(4);
  walk->mutable_dash()->set_step_ms(120);
  BossRun run("zakum", boss, 0);
  BossRun leap("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  leap.Advance(*leap_state, kBossCountdownSeconds);

  // The next walk step is a long way off, so only the dash moves him.
  run.Advance(*state, 9.9);
  ASSERT_EQ(run.slots()[0].x, 0) << "he moved before his dash was due";
  // He's against the left wall, so the dash turns around instead of standing
  // still, and moves him a cell every 120ms.
  for (int cell = 1; cell <= 4; ++cell) {
    run.Advance(*state, 0.12);
    EXPECT_EQ(run.slots()[0].x, cell) << "cell " << cell;
  }
  run.Advance(*state, 5.0);
  EXPECT_EQ(run.slots()[0].x, 4) << "the dash ran past its length";

  leap.Advance(*leap_state, 15.38);
  EXPECT_EQ(leap.slots()[0].x, run.slots()[0].x);
  EXPECT_EQ(leap.slots()[1].x, run.slots()[1].x);
}

// A dash into a wall stops there. The remaining cells are lost, not spent
// elsewhere, and the walk continues from where he stopped.
TEST(BossRunTest, ADashStopsAtTheWallAndStaysThere) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(3);
  phase->set_arena_height(2);
  Spawn* arms = phase->mutable_spawns(0);
  arms->clear_spots();
  arms->add_spots()->set_x(0);
  ArenaWalk* walk = arms->mutable_walk();
  walk->set_interval_ms(30000);
  walk->set_range(ArenaWalk::RANGE_ROW);
  walk->mutable_dash()->set_interval_ms(10000);
  walk->mutable_dash()->set_cells(4);
  walk->mutable_dash()->set_step_ms(120);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  // Two cells of room and four cells of dash: at the fight's tenth second he's
  // against the far wall.
  run.Advance(*state, 11.0);
  EXPECT_EQ(run.slots()[0].x, 2);
  run.Advance(*state, 5.0);
  EXPECT_EQ(run.slots()[0].x, 2) << "the wall did not stop the dash";
}

// The Guardian Angel Slime's jump: she leaves her row, stays up for the whole
// hang time, then lands back on the cell she left. A run advanced in one go
// ends where one advanced beat by beat does.
TEST(BossRunTest, AJumpLeavesTheRowAndLandsOnTheCellItLeft) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  std::unique_ptr<GameState> leap_state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(9);
  phase->set_arena_height(6);
  ArenaWalk* walk = phase->mutable_spawns(0)->mutable_walk();
  walk->set_interval_ms(10000);
  walk->set_range(ArenaWalk::RANGE_ROW_STEP);
  walk->mutable_jump()->set_interval_ms(30000);
  walk->mutable_jump()->set_y(3);
  walk->mutable_jump()->set_hang_ms(660);
  BossRun run("zakum", boss, 0);
  BossRun leap("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  leap.Advance(*leap_state, kBossCountdownSeconds);

  run.Advance(*state, 29.9);
  ASSERT_EQ(run.slots()[0].y, 0) << "she left the row before the jump was due";
  int column = run.slots()[0].x;
  run.Advance(*state, 0.2);
  EXPECT_EQ(run.slots()[0].y, 3) << "the jump did not reach its row";
  EXPECT_EQ(run.slots()[0].x, column) << "she walked while in the air";
  run.Advance(*state, 0.5);
  EXPECT_EQ(run.slots()[0].y, 3) << "she came down inside the hang";
  run.Advance(*state, 0.2);
  EXPECT_EQ(run.slots()[0].y, 0) << "she stayed up past the hang";
  EXPECT_EQ(run.slots()[0].x, column) << "she landed off the cell she left";
  // The step due at 30s was used up by the jump, instead of firing the moment
  // she landed.
  run.Advance(*state, 5.0);
  EXPECT_EQ(run.slots()[0].x, column) << "the walk did not give up its beat";

  leap.Advance(*leap_state, 35.8);
  EXPECT_EQ(leap.slots()[0].y, run.slots()[0].y);
  EXPECT_EQ(leap.slots()[0].x, run.slots()[0].x);
}

// A jump runs on its own clock, so a monster that only jumps (with no walk
// interval) still jumps.
TEST(BossRunTest, AMonsterThatOnlyJumpsStillJumps) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossPhase* phase = boss.mutable_difficulties(0)->mutable_phases(0);
  phase->set_arena_width(9);
  phase->set_arena_height(6);
  ArenaJump* jump = phase->mutable_spawns(0)->mutable_walk()->mutable_jump();
  jump->set_interval_ms(5000);
  jump->set_y(3);
  jump->set_hang_ms(660);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  int column = run.slots()[0].x;

  run.Advance(*state, 5.1);
  EXPECT_EQ(run.slots()[0].y, 3);
  run.Advance(*state, 0.6);
  EXPECT_EQ(run.slots()[0].y, 0);
  EXPECT_EQ(run.slots()[0].x, column) << "a monster with no walk moved";
  // The next jump comes on its own interval, not timed from the landing.
  run.Advance(*state, 4.5);
  EXPECT_EQ(run.slots()[0].y, 3);
}

// A dead bar keeps its slot for a moment, then leaves it empty. The arms beside
// it never move.
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
  // The arms give no EXP, so all of it is from the body.
  EXPECT_GT(state->character.proto().exp() + state->character.proto().level(),
            before);
  EXPECT_EQ(state->character.proto().meso(), 0)
      << "a boss should pay no field meso";
}

// A phase with no track plays the last one named at or above it. A fight with
// none, or a difficulty that doesn't exist, plays nothing.
TEST(BossRunTest, APhasePlaysTheLastTrackNamedAtOrAboveIt) {
  Boss boss = TwoPhaseBoss();
  EXPECT_EQ(BossRun("zakum", boss, 0).bgm(), "");
  EXPECT_EQ(BossRun("zakum", boss, 5).bgm(), "");

  boss.mutable_difficulties(0)->mutable_phases(0)->set_bgm("arms");
  std::unique_ptr<GameState> state = MakeState();
  BossRun carried("zakum", boss, 0);
  EXPECT_EQ(carried.bgm(), "arms");
  RunToEnd(carried, *state);
  EXPECT_EQ(carried.bgm(), "arms");  // the body names none

  boss.mutable_difficulties(0)->mutable_phases(1)->set_bgm("body");
  std::unique_ptr<GameState> again = MakeState();
  BossRun own("zakum", boss, 0);
  RunToEnd(own, *again);
  EXPECT_EQ(own.bgm(), "body");
}

// Running out of time is the only way the player can lose.
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

// A win holds its last moment on screen, but an abort doesn't. The player chose
// to leave, so there's nothing left to watch.
TEST(BossRunTest, AbortingEndsItWithNoClosingBeat) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  run.Abort();
  EXPECT_EQ(run.state(), BossRunState::kAborted);
  EXPECT_TRUE(run.done());

  // A finished run doesn't advance.
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

// A clear pays the reward table, and the run remembers what it paid so the
// clear card can list it.
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
  // The reward, on top of what the levels gained from the fight's EXP are
  // worth.
  EXPECT_EQ(
      state->character.honor(),
      kBossClearHonor + HonorForLevels(1, state->character.proto().level()));
  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_EQ(run.reward().items[0].name, "Zakum's Soul Shard");
  EXPECT_EQ(run.reward().items[0].count, 1);
  EXPECT_EQ(state->character.CountItem(DropItems().at("shard")), 1);
}

// A practice run is only the fight. It still keeps time, since beating the
// clock is the point.
TEST(BossRunTest, APracticeClearPaysNothingAndStillTimesItself) {
  std::unique_ptr<GameState> state = MakeState();
  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  int64_t exp = state->character.proto().exp();
  BossRun run("zakum", boss, 0, /*authority=*/nullptr, /*practice=*/true);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  EXPECT_TRUE(run.practice());
  EXPECT_GT(run.clear_seconds(), 0.0);
  EXPECT_EQ(run.reward().meso, 0);
  EXPECT_EQ(run.reward().exp, 0);
  EXPECT_EQ(run.reward().honor, 0);
  EXPECT_TRUE(run.reward().items.empty());
  EXPECT_EQ(state->character.proto().meso(), 0);
  EXPECT_EQ(state->character.proto().exp(), exp);
  EXPECT_EQ(state->character.honor(), 0);
  EXPECT_EQ(state->character.CountOwned(DropEquips().at("mark")), 0);
}

// Paid once: not once per phase, and not again while the win is on screen.
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

// Honor is a daily reward, so a fight with no daily lockout pays none, however
// often it is cleared.
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

// The clear card shows the fight's own time, not counting the countdown.
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

// The breakdown counts every line the player landed, including overkill, over
// the seconds they fought. The countdown and the gaps between phases don't
// count.
TEST(BossRunTest, TheBreakdownCountsEveryLineOverTheFightingSeconds) {
  std::unique_ptr<GameState> state = MakeState(40, 200);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  const DamageBreakdown& breakdown = run.breakdown();
  std::vector<BreakdownRow> rows = breakdown.Rows();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].skill, "Attack");
  EXPECT_GE(rows[0].damage, 2 * 40 + 200);
  EXPECT_GT(rows[0].casts, 0);
  EXPECT_GE(rows[0].lines, rows[0].casts);
  EXPECT_NEAR(breakdown.seconds(), run.clear_seconds() - kBossPhaseGapSeconds,
              0.2);
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

// Drop rate raises the chance of gear but never adds a copy. The mark the table
// guarantees is still one mark, however much drop gear is worn.
TEST(BossRunTest, DropRateDoesNotDoubleACertainPieceOfGear) {
  std::unique_ptr<GameState> state = MakeState();
  WearDropGear(*state, 200);

  Boss boss = RewardingBoss(/*mark_chance=*/1.0);
  boss.mutable_difficulties(0)->mutable_drops(1)->set_per_kill(0.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_EQ(run.reward().items[0].count, 1);
  EXPECT_EQ(state->character.CountOwned(DropEquips().at("mark")), 1);
}

// A stackable does get extra copies: at 250% drop rate, the certain shard is
// two outright and a coin flip for a third.
TEST(BossRunTest, DropRateStacksACertainShard) {
  std::unique_ptr<GameState> state = MakeState();
  WearDropGear(*state, 150);

  Boss boss = RewardingBoss(/*mark_chance=*/0.0);
  BossRun run("zakum", boss, 0);
  RunToEnd(run, *state);

  ASSERT_TRUE(run.won());
  ASSERT_EQ(run.reward().items.size(), 1u);
  EXPECT_GE(run.reward().items[0].count, 2);
  EXPECT_LE(run.reward().items[0].count, 3);
  EXPECT_FALSE(run.reward().items[0].prize);
  EXPECT_EQ(state->character.CountItem(DropItems().at("shard")),
            run.reward().items[0].count);
}

// The card lists prizes separately from other drops, with their drop rate, so
// it can mark which rows the player came for and show the rarest first.
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

// A token counts as a prize too, since the shop trades it for gear. It's listed
// with the gear, not with the drops every clear pays.
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

// A drop that fails its roll isn't on the card. Over many runs it lands about
// half the time, as the table says.
TEST(BossRunTest, AChanceDropIsRolledFor) {
  int landed = 0;
  for (int i = 0; i < 200; ++i) {
    std::unique_ptr<GameState> state = MakeState();
    Boss boss = RewardingBoss(/*mark_chance=*/0.5);
    BossRun run("zakum", boss, 0);
    RunToEnd(run, *state);
    ASSERT_TRUE(run.won());
    // The shard is certain, so a second row must be the accessory.
    if (run.reward().items.size() == 2u) {
      ++landed;
      EXPECT_EQ(run.reward().items[0].name, "Condensed Power Crystal");
    }
  }
  EXPECT_GT(landed, 60);
  EXPECT_LT(landed, 140);
}

// Hitting a monster shows damage numbers over it, and a monster nothing hit
// shows none.
TEST(BossRunTest, ASwingWritesNumbersOverWhatItHit) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  EXPECT_TRUE(run.damage_writes().empty()) << "nothing has swung yet";

  run.Advance(*state, kBossCountdownSeconds + 1.0);
  ASSERT_FALSE(run.damage_writes().empty());
  ASSERT_FALSE(run.slots().empty());
  for (const DamageWrite& write : run.damage_writes()) {
    EXPECT_EQ(write.mob_id, run.slots()[0].id) << "one arm was in reach";
    EXPECT_FALSE(write.lines.empty());
    for (const DamageNumber& line : write.lines) {
      EXPECT_GE(line.damage, 1);
    }
  }
  EXPECT_FALSE(DamageColumn(run.damage_writes(), run.slots()[0].id).empty());
  EXPECT_TRUE(DamageColumn(run.damage_writes(), run.slots()[1].id).empty())
      << "the other arm was never hit";
}

// Each row above a monster keeps the newest number written to it. A short
// attack landing after a tall one takes the bottom rows and leaves the rest of
// the tall one showing. This is the whole rule, in the form the screen reads
// it.
TEST(BossRunTest, AShortAttackTakesTheBottomRowsAndLeavesTheRest) {
  std::vector<DamageWrite> writes;
  writes.push_back({7, Numbers(10, 100), 0.0, 0.25});
  writes.push_back({7, Numbers(6, 200), 0.0, 0.15});
  writes.push_back({7, Numbers(2, 300), 0.0, 0.05});

  std::vector<DamageRow> column = DamageColumn(writes, 7);
  ASSERT_EQ(column.size(), 10u) << "as tall as the tallest live write";
  for (int row = 0; row < 10; ++row) {
    ASSERT_TRUE(column[row].filled) << "row " << row;
    // The bottom two rows are from the last attack, the next four from the one
    // before it, and the top four still from the first.
    int64_t want = row < 2 ? 300 + row : row < 6 ? 200 + row : 100 + row;
    EXPECT_EQ(column[row].number.damage, want) << "row " << row;
  }

  // The first attack's time runs out first, and only the rows no later attack
  // reached disappear.
  writes[0].age = kDamageStackSeconds;
  column = DamageColumn(writes, 7);
  ASSERT_EQ(column.size(), 6u);
  EXPECT_EQ(column[0].number.damage, 300);
  EXPECT_EQ(column[5].number.damage, 205);
}

// A hit's numbers wait their turn. All the writes from one attack are queued
// together and appear one after another, so an attack with twelve hits flashes.
TEST(BossRunTest, AStrikeShowsNothingUntilItIsDue) {
  std::vector<DamageWrite> writes;
  writes.push_back({7, Numbers(1, 100), 0.0, 0.01});
  writes.push_back({7, Numbers(1, 200), kDamageStrikeSeconds, 0.01});

  std::vector<DamageRow> column = DamageColumn(writes, 7);
  ASSERT_EQ(column.size(), 1u);
  EXPECT_EQ(column[0].number.damage, 100) << "the second strike is not due";

  writes[0].age = writes[1].age = kDamageStrikeSeconds + 0.01;
  column = DamageColumn(writes, 7);
  ASSERT_EQ(column.size(), 1u);
  EXPECT_EQ(column[0].number.damage, 200) << "and now it is the fresher one";
}

// The numbers are an animation: they expire on their own, whether or not
// anything else happens.
TEST(BossRunTest, AWriteFadesAfterItsTime) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds + 1.0);
  ASSERT_FALSE(run.damage_writes().empty());

  run.Advance(*state, kDamageStackSeconds);
  for (const DamageWrite& write : run.damage_writes()) {
    EXPECT_LT(write.showing(), kDamageStackSeconds);
  }
}

// A phase change clears the numbers. Their ids belong to the previous phase's
// encounter, and the new arena is a different one.
TEST(BossRunTest, APhaseChangeClearsTheNumbers) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000);
  Boss boss = TwoPhaseBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  for (int i = 0; i < 200 && run.phase() == 1; ++i) {
    run.Advance(*state, 0.1);
  }
  ASSERT_EQ(run.phase(), 2);

  // Everything on screen belongs to the current phase. The arms left numbers,
  // and none survived the change.
  ASSERT_FALSE(run.slots().empty());
  for (const DamageWrite& write : run.damage_writes()) {
    EXPECT_EQ(write.mob_id, run.slots()[0].id);
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

// A followed run reports its whole damage table as it goes, and ends showing
// everyone's: its own first and unnamed, then the rest as the authority lists
// them.
TEST(BossRunTest, AFollowedRunReportsItsBreakdownAndEndsOnEveryones) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  authority.fight_.players[0].account_id = "me";
  authority.fight_.players[1].account_id = "them";
  BossRun run("zakum", boss, 0, &authority);
  for (int step = 0; step < 20; ++step) {
    run.Advance(*state, 0.1);
  }
  ASSERT_EQ(authority.reported_breakdown_.size(), 1u);
  EXPECT_EQ(authority.reported_breakdown_[0].skill, "Attack");

  authority.fight_.state = BossRunState::kWon;
  authority.fight_.breakdowns = {{"me", "Dagger", {{"Attack", 1.0, 1, 1}}},
                                 {"them", "Wand", {{"Blizzard", 5.0, 1, 2}}}};
  run.Advance(*state, 0.1);
  std::vector<PlayerBreakdown> all = run.breakdowns();
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].name, "");
  EXPECT_DOUBLE_EQ(TotalDamage(all[0].rows), run.breakdown().total());
  EXPECT_EQ(all[1].name, "Wand");
  EXPECT_EQ(all[1].rows[0].skill, "Blizzard");
}

TEST(BossRunTest, AFollowedRunShowsTheMonstersThroughTheCountIn) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  authority.fight_.state = BossRunState::kCountdown;
  authority.fight_.countdown_left = 2.0;
  BossRun run("zakum", boss, 0, &authority);

  run.Advance(*state, 0.1);
  run.Advance(*state, 0.1);
  EXPECT_EQ(run.state(), BossRunState::kCountdown);
  ASSERT_EQ(run.slots().size(), 2u);
  EXPECT_DOUBLE_EQ(run.slots()[0].hp_fraction, 1.0);
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
  // This player comes first, whoever the server lists first.
  EXPECT_TRUE(run.members()[0].name.empty());
  EXPECT_EQ(run.members()[1].name, "Wand");
  EXPECT_EQ(run.members()[1].spot, 1);

  authority.fight_.phase = 1;
  authority.fight_.hp_fractions.assign(1, 1.0);
  run.Advance(*state, 0.1);
  EXPECT_EQ(run.phase(), 2);
}

// The total of everything a run has reported to the party.
int64_t Landed(const std::vector<SharedLine>& lines) {
  int64_t total = 0;
  for (const SharedLine& line : lines) {
    total += line.damage;
  }
  return total;
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
  // Match by slot, not monster id, since each client hands out its own ids.
  for (const SharedLine& line : authority.reported_) {
    EXPECT_GE(line.slot, 0);
    EXPECT_LT(line.slot, 2);
    EXPECT_GT(line.damage, 0);
  }
  // The same numbers the player saw, so the two can't drift apart.
  int64_t drawn = 0;
  for (const DamageWrite& write : run.damage_writes()) {
    for (const DamageNumber& number : write.lines) {
      drawn += number.damage;
    }
  }
  EXPECT_GT(drawn, 0);
}

// The screen runs at kBossFightStep and the network at kFightPublishInterval,
// so a fight advanced at the screen's rate must still report at the network's.
TEST(BossRunTest, AFollowedRunReportsOnTheWiresBeatAndLosesNoLine) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);

  const double kStep = kBossFightStep.count() / 1000.0;
  const double kSeconds = 3.0;
  for (int i = 0; i < static_cast<int>(kSeconds / kStep); ++i) {
    run.Advance(*state, kStep);
  }

  // One report per network beat, not one per step (a step is shorter).
  int beats = static_cast<int>(kSeconds * 1000 / kFightPublishInterval.count());
  EXPECT_NEAR(authority.reports_, beats, 1);
  EXPECT_LT(authority.reports_, static_cast<int>(kSeconds / kStep));

  // No lines are lost between reports: the same fight advanced one beat at a
  // time reports every line the faster-stepped one did.
  TestAuthority slow(2);
  BossRun paced("zakum", boss, 0, &slow);
  const double kBeat = kFightPublishInterval.count() / 1000.0;
  for (int i = 0; i < static_cast<int>(kSeconds / kBeat); ++i) {
    paced.Advance(*state, kBeat);
  }
  EXPECT_EQ(Landed(authority.reported_), Landed(slow.reported_));
  EXPECT_GT(Landed(authority.reported_), 0);
}

TEST(BossRunTest, TheSharedRosterSaysWhatIsLeft) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);
  ASSERT_EQ(run.slots().size(), 2u);

  // The party's damage to the first monster, which this client never attacked.
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
  // Far enough in that this player has landed an attack of their own.
  for (int i = 0; i < 40 && run.damage_writes().empty(); ++i) {
    run.Advance(*state, 0.1);
  }
  authority.OtherLanded(0, 1234);
  run.Advance(*state, 0.1);

  EXPECT_FALSE(run.damage_writes().empty()) << "mine are written rows";
  ASSERT_EQ(run.damage_stacks().size(), 1u) << "and theirs a stack beside";
  EXPECT_EQ(run.damage_stacks()[0].owner, 1);
  ASSERT_EQ(run.damage_stacks()[0].lines.size(), 1u);
  EXPECT_EQ(run.damage_stacks()[0].lines[0].damage, 1234);

  // One stack per player per source: a player's new numbers replace their own
  // old ones, and never touch this player's.
  authority.OtherLanded(0, 4321);
  run.Advance(*state, 0.1);
  ASSERT_EQ(run.damage_stacks().size(), 1u);
  EXPECT_EQ(run.damage_stacks()[0].lines[0].damage, 4321);
  for (const DamageWrite& write : run.damage_writes()) {
    for (const DamageNumber& number : write.lines) {
      EXPECT_NE(number.damage, 4321);
    }
  }
}

TEST(BossRunTest, AWalkPassesOverSomebodyElsesSpot) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  Boss boss = WalkableBoss();
  TestAuthority authority(2);
  authority.fight_.players[1].spot = kFloorRight;
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);
  ASSERT_EQ(run.members()[0].spot, kFloorMiddle);

  // The right end of the floor is taken, so walking right skips past it to the
  // ledge above instead of stopping short or standing on someone.
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

  // Their panel is removed, nothing more of theirs is drawn, and the spot they
  // stood on is free to walk to again.
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
  // Half the meso, and all of the EXP and honor.
  EXPECT_EQ(run.reward().meso, boss.difficulties(0).meso() / 2);
  EXPECT_EQ(run.reward().exp, boss.difficulties(0).exp());
  EXPECT_EQ(run.reward().honor, kBossClearHonor);
  // The authority hands out the drops. It gave none, so none were paid, even
  // though the table says the shard is certain.
  EXPECT_TRUE(run.reward().items.empty());
}

// A shared run rolls nothing. It pays exactly the drops it was given.
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
  EXPECT_EQ(state->character.CountItem(DropItems().at("shard")), 3);
  // The mark is certain in the table but wasn't given, so it wasn't paid.
  EXPECT_EQ(state->character.CountOwned(DropEquips().at("mark")), 0);
}

// The run tells the authority its Item Drop Rate, because the clear rolls
// against the best one in the party.
TEST(BossRunTest, ASharedRunReportsItsDropRate) {
  std::unique_ptr<GameState> state = MakeState(1000000, 1000000);
  WearDropGear(*state, 50);

  Boss boss = TwoPhaseBoss();
  TestAuthority authority(2);
  BossRun run("zakum", boss, 0, &authority);
  run.Advance(*state, 0.1);

  EXPECT_DOUBLE_EQ(authority.reported_drop_pct_, 0.5);
}

}  // namespace
}  // namespace ms
