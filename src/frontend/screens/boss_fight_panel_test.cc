#include "src/frontend/screens/boss_fight_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/boss_run.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Mob BossMob(const std::string& name, int max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(110);
  mob.set_max_hp(max_hp);
  mob.set_boss(true);
  return mob;
}

// The places a phase lets the player stand, as (x, y) pairs.
void AddSpots(BossPhase* phase, const std::vector<std::pair<int, int>>& spots) {
  for (const std::pair<int, int>& spot : spots) {
    ArenaSpot* at = phase->add_player_spots();
    at->set_x(spot.first);
    at->set_y(spot.second);
  }
}

Boss Zakum() {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(300);
  // The arms in two columns of four down the middle, with the player's floor
  // under them and a ledge over each end, as the data file lays them out: one
  // spawn each, since a spot belongs to one monster.
  BossPhase* arms = normal->add_phases();
  for (int i = 0; i < 8; ++i) {
    Spawn* arm = arms->add_spawns();
    arm->set_mob("arm");
    arm->set_count(1);
    ArenaSpot* spot = arms->add_spots();
    spot->set_x(i < 4 ? 2 : 4);
    spot->set_y(1 + i % 4);
  }
  AddSpots(arms, {{0, 3}, {6, 3}, {0, 5}, {3, 5}, {6, 5}});
  arms->mutable_player()->set_x(3);
  arms->mutable_player()->set_y(5);
  arms->set_arena_width(7);
  arms->set_arena_height(6);
  BossPhase* body = normal->add_phases();
  Spawn* torso = body->add_spawns();
  torso->set_mob("body");
  torso->set_count(1);
  ArenaSpot* torso_spot = body->add_spots();
  torso_spot->set_x(3);
  torso_spot->set_y(2);
  AddSpots(body, {{0, 3}, {3, 3}, {6, 3}});
  body->mutable_player()->set_x(3);
  body->mutable_player()->set_y(3);
  body->set_arena_width(7);
  body->set_arena_height(4);
  return boss;
}

std::unique_ptr<GameState> MakeState(int arm_hp, int body_hp,
                                     std::map<std::string, Skill> skills = {}) {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"arm", BossMob("Zakum's Arm", arm_hp)},
                                 {"body", BossMob("Zakum", body_hp)}},
      std::map<std::string, MapData>{}, std::move(skills));
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

// The screen as one string per row, one character per column. A border is
// multi-byte, so it is written as a single '#': what these rows are read for
// is which column something is in, and a byte offset is not that.
std::vector<std::string> Rows(const BossRun& run, int width = 120) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Render(screen, BossFightPanel(run));
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.size() == 1 ? cell : "#";
    }
    rows.push_back(row);
  }
  return rows;
}

// The column `needle` starts in, or -1 if nothing holds it.
int ColumnOf(const std::vector<std::string>& rows, const std::string& needle) {
  for (const std::string& row : rows) {
    std::size_t at = row.find(needle);
    if (at != std::string::npos) {
      return static_cast<int>(at);
    }
  }
  return -1;
}

// The row `needle` is drawn on, or -1.
int RowOf(const std::vector<std::string>& rows, const std::string& needle) {
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    if (rows[y].find(needle) != std::string::npos) {
      return y;
    }
  }
  return -1;
}

std::string Render(const BossRun& run) {
  // Wide enough for the whole arena: three panels and the gaps between them.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Render(screen, BossFightPanel(run));
  return screen.ToString();
}

TEST(BossFightPanelTest, TheClockCountsInMinutesAndSeconds) {
  EXPECT_EQ(FightClock(300.0), "5:00");
  EXPECT_EQ(FightClock(299.5), "5:00");
  EXPECT_EQ(FightClock(65.0), "1:05");
  EXPECT_EQ(FightClock(9.2), "0:10");
  // Only actually being out of time reads 0:00.
  EXPECT_EQ(FightClock(0.1), "0:01");
  EXPECT_EQ(FightClock(0.0), "0:00");
  EXPECT_EQ(FightClock(-5.0), "0:00");
}

TEST(BossFightPanelTest, TheHeadingNamesThePhaseAndWhatIsLeftOfIt) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  EXPECT_EQ(FightHeading(run), "Normal Zakum - P1 - 100%");

  run.Abort();
  EXPECT_EQ(FightHeading(run), "Normal Zakum - Left");
}

TEST(BossFightPanelTest, EveryArmIsDrawnAndTheClockIsUnderTheHeading) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  std::string out = Render(run);

  EXPECT_NE(out.find("Normal Zakum - P1 - 100%"), std::string::npos);
  EXPECT_NE(out.find("5:00"), std::string::npos);
  EXPECT_NE(out.find("You"), std::string::npos);
  // Eight arms, four to a side.
  int drawn = 0;
  for (std::size_t at = out.find("Zakum's Arm"); at != std::string::npos;
       at = out.find("Zakum's Arm", at + 1)) {
    ++drawn;
  }
  EXPECT_EQ(drawn, 8);
}

// A dead arm's bar leaves the screen, but the four rows on that side stay
// four rows: the bars beside it must not move.
TEST(BossFightPanelTest, ADeadArmLeavesItsSlotEmpty) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000000);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  int before = 0;
  {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(90),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, BossFightPanel(run));
    before = screen.dimy();
  }
  for (int i = 0; i < 200 && run.slots()[0].alive; ++i) {
    run.Advance(*state, 0.05);
  }
  run.Advance(*state, kBossDeathHoldSeconds);
  ASSERT_FALSE(run.slots()[0].visible);

  ftxui::Screen after = ftxui::Screen::Create(ftxui::Dimension::Fixed(90),
                                              ftxui::Dimension::Fixed(30));
  ftxui::Render(after, BossFightPanel(run));
  EXPECT_EQ(after.dimy(), before);
  // The gone arm is not drawn, and the ones still standing are.
  std::string out = after.ToString();
  EXPECT_NE(out.find("Zakum's Arm"), std::string::npos);
}

// The count-in stands where the swing name will, so the one thing about to
// change is where the eye already is.
TEST(BossFightPanelTest, TheCountdownShowsOnThePlayerPanel) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  EXPECT_NE(Render(run).find("3"), std::string::npos);
  run.Advance(*state, 2.5);
  EXPECT_NE(Render(run).find("1"), std::string::npos);
}

// A name too long for one row wraps over the player's two rather than widening
// the whole arena. "Midnight Carnival" is the longest swing in the game.
TEST(BossFightPanelTest, ALongSwingNameWrapsOverThePlayersRows) {
  Skill carnival;
  carnival.set_name("Midnight Carnival");
  carnival.set_kind(SKILL_KIND_ATTACK);
  carnival.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  carnival.set_max_level(1);
  carnival.set_max_enemies(6);
  carnival.mutable_base()->set_skill_pct(5.0);
  std::unique_ptr<GameState> state =
      MakeState(1000000000, 1, {{"carnival", carnival}});
  state->character.AdvanceJob(JOB_SWORDMAN);
  // SP arrives with the levels, and only past level 10.
  for (int i = 0; i < 12; ++i) {
    state->character.LevelUp();
  }
  ASSERT_TRUE(state->character.LearnSkill(carnival, 1));

  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_EQ(run.attack_name(), "Midnight Carnival");

  std::string out = Render(run);
  EXPECT_NE(out.find("Midnight"), std::string::npos);
  EXPECT_NE(out.find("Carnival"), std::string::npos);
  EXPECT_EQ(out.find("Midnight Carnival"), std::string::npos);
}

// The body stands over the player rather than off to one side.
TEST(BossFightPanelTest, TheBodyIsDrawnAboveThePlayer) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000000);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  for (int i = 0; i < 2000 && run.phase() == 1; ++i) {
    run.Advance(*state, 0.05);
  }
  ASSERT_EQ(run.phase(), 2);
  ASSERT_EQ(run.slots().size(), 1u);

  std::string out = Render(run);
  EXPECT_NE(out.find("Zakum"), std::string::npos);
  EXPECT_LT(out.find(" Zakum "), out.find(" You "));
}

// A phase where two parts stand four cells apart, with a cell of margin to
// the right of the second and none to the left of the first.
Boss TwoPartBoss() {
  Boss boss;
  boss.set_name("Horntail");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_time_limit_seconds(300);
  BossPhase* phase = normal->add_phases();
  for (const std::string& mob : {"arm", "body"}) {
    Spawn* spawn = phase->add_spawns();
    spawn->set_mob(mob);
    spawn->set_count(1);
  }
  phase->add_spots()->set_x(0);
  phase->add_spots()->set_x(4);
  phase->mutable_player()->set_x(2);
  phase->mutable_player()->set_y(1);
  phase->set_arena_width(6);
  phase->set_arena_height(2);
  return boss;
}

// The whole point of the spots: a part is drawn where the fight puts it, in
// the order and on the row the phase asked for.
TEST(BossFightPanelTest, EachPartStandsWhereItsSpotSays) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  // Names of the same length, so the two are centred in their panels alike
  // and the columns between them are the panels' own.
  state->mobs["arm"].set_name("Left Hand");
  state->mobs["body"].set_name("Rite Hand");
  Boss boss = TwoPartBoss();
  BossRun run("horntail", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  std::vector<std::string> rows = Rows(run);
  int left = ColumnOf(rows, "Left Hand");
  int right = ColumnOf(rows, "Rite Hand");
  ASSERT_GE(left, 0);
  ASSERT_GE(right, 0);
  EXPECT_LT(left, right);
  // The one in the first cell stands against the edge; the one in the last
  // but one keeps the cell of margin the arena asked for.
  EXPECT_LT(left, kBossPanelWidth);
  EXPECT_GT(right + kBossPanelWidth, 100);
  // And the player stands under them rather than beside them.
  EXPECT_GT(RowOf(rows, "You"), RowOf(rows, "Left Hand"));
}

// The arena is the screen it is drawn on: the bars keep their size and the
// space between them takes the rest, so a wider terminal spreads the fight
// out rather than leaving it in a huddle in the middle.
TEST(BossFightPanelTest, TheArenaSpreadsToFillTheScreen) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  state->mobs["arm"].set_name("Left Hand");
  state->mobs["body"].set_name("Rite Hand");
  Boss boss = TwoPartBoss();
  BossRun run("horntail", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  int narrow = ColumnOf(Rows(run, 100), "Rite Hand") -
               ColumnOf(Rows(run, 100), "Left Hand");
  int wide = ColumnOf(Rows(run, 180), "Rite Hand") -
             ColumnOf(Rows(run, 180), "Left Hand");
  EXPECT_GT(wide, narrow);
  EXPECT_GT(wide - narrow, 50) << "the whole 80 columns went into the gaps";
}

// A part whose name is too long for one row gets two, and every bar in the
// phase takes the same two so the arena's rows stay square.
TEST(BossFightPanelTest, ALongPartNameWrapsOverTwoRows) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  state->mobs["arm"].set_name("Horntail's Left Head");
  Boss boss = TwoPartBoss();
  BossRun run("horntail", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  std::string out = Render(run);
  EXPECT_NE(out.find("Horntail's"), std::string::npos);
  EXPECT_NE(out.find("Left Head"), std::string::npos);
  EXPECT_EQ(out.find("Horntail's Left Head"), std::string::npos);
}

// How many places to stand are drawn empty. The player is on one of them, so
// a five-spot phase marks four.
int MarkedSpots(const BossRun& run) {
  std::string out = Render(run);
  int found = 0;
  for (std::size_t at = out.find("· · ·"); at != std::string::npos;
       at = out.find("· · ·", at + 1)) {
    ++found;
  }
  return found;
}

// The walk is drawn: every spot the player is not on is marked, and the one
// they are on holds their panel instead.
TEST(BossFightPanelTest, TheSpotsThePlayerIsNotOnAreMarked) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  EXPECT_EQ(MarkedSpots(run), 4);

  int middle = ColumnOf(Rows(run), "You");
  run.MovePlayer(-1, 0);
  int left = ColumnOf(Rows(run), "You");
  EXPECT_LT(left, middle) << "the player walked, and their panel with them";
  EXPECT_EQ(MarkedSpots(run), 4) << "the spot they left is marked now";

  // Up the ledge, which is a row the player was not on a moment ago.
  int floor = RowOf(Rows(run), "You");
  run.MovePlayer(0, -1);
  EXPECT_LT(RowOf(Rows(run), "You"), floor);
}

}  // namespace
}  // namespace ms
