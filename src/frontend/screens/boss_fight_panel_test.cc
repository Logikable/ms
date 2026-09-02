#include "src/frontend/screens/boss_fight_panel.h"

#include <gtest/gtest.h>

#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/boss_run.h"
#include "src/combat/test_authority.h"
#include "src/frontend/widgets/colors.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

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

// The places a phase lets the player stand, as (x, y) pairs, the first being
// where they start.
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
  // under them and a ledge over each end, as the data file lays them out.
  BossPhase* arms = normal->add_phases();
  Spawn* arm = arms->add_spawns();
  arm->set_mob("arm");
  for (int i = 0; i < 8; ++i) {
    ArenaSpot* spot = arm->add_spots();
    spot->set_x(i < 4 ? 2 : 4);
    spot->set_y(1 + i % 4);
  }
  AddSpots(arms, {{3, 5}, {0, 3}, {6, 3}, {0, 5}, {6, 5}});
  arms->set_arena_width(7);
  arms->set_arena_height(6);
  BossPhase* body = normal->add_phases();
  Spawn* torso = body->add_spawns();
  torso->set_mob("body");
  ArenaSpot* torso_spot = torso->add_spots();
  torso_spot->set_x(3);
  torso_spot->set_y(2);
  AddSpots(body, {{3, 3}, {0, 3}, {6, 3}});
  body->set_arena_width(7);
  body->set_arena_height(4);
  return boss;
}

// One arm in a column of its own, with the player under it and nothing to
// either side: an arena where a stack can only stand over its monster.
Boss OneArmBoss() {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(300);
  BossPhase* phase = normal->add_phases();
  Spawn* arm = phase->add_spawns();
  arm->set_mob("arm");
  ArenaSpot* spot = arm->add_spots();
  spot->set_x(0);
  spot->set_y(1);
  AddSpots(phase, {{0, 2}});
  phase->set_arena_width(1);
  phase->set_arena_height(3);
  return boss;
}

// Two arms in one column, the player under them: the space over the lower arm
// is the gap between the two bars, which is exactly the space a stack placed
// under the upper one would take.
Boss ColumnBoss() {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(300);
  BossPhase* phase = normal->add_phases();
  Spawn* arms = phase->add_spawns();
  arms->set_mob("arm");
  for (int i = 0; i < 2; ++i) {
    ArenaSpot* spot = arms->add_spots();
    spot->set_x(0);
    spot->set_y(i);
  }
  AddSpots(phase, {{0, 2}});
  phase->set_arena_width(1);
  phase->set_arena_height(3);
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
  EquipPrototype sword = PlainSword();
  state->character.PickUp(std::make_unique<EquipInstance>(sword));
  state->character.Equip(0);
  return state;
}

// The screen as one string per row, one character per column. A border is
// multi-byte, so it is written as a single '#': what these rows are read for
// is which column something is in, and a byte offset is not that.
std::vector<std::string> RowsOf(const ftxui::Screen& screen) {
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

std::vector<std::string> Rows(const BossRun& run,
                              int width = kMinTerminalColumns) {
  ftxui::Screen screen =
      ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                            ftxui::Dimension::Fixed(kMinTerminalRows));
  ftxui::Render(screen, BossFightPanel(run));
  return RowsOf(screen);
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

// A character holding a swing that lands eight times on one enemy, so a stack
// has enough numbers in it to be crowded out of a corner.
std::unique_ptr<GameState> EightLineState(Skill beside = Skill()) {
  Skill flurry;
  flurry.set_name("Flurry");
  flurry.set_kind(SKILL_KIND_ATTACK);
  flurry.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  flurry.set_max_level(1);
  flurry.set_max_enemies(1);
  flurry.set_lines(8);
  // Slower than a stack's life, so only one is ever on screen.
  flurry.set_base_delay_ms(2000);
  flurry.mutable_base()->set_skill_pct(5.0);
  std::map<std::string, Skill> book = {{"flurry", flurry}};
  if (!beside.name().empty()) {
    book[beside.name()] = beside;
  }
  std::unique_ptr<GameState> state = MakeState(1000000000, 1, std::move(book));
  state->character.AdvanceJob(JOB_SWORDMAN);
  // Up to the arms' own level: forty levels under a monster the whole chain
  // floors at a point of damage, and every swing would tie with the poke.
  for (int i = 0; i < 110; ++i) {
    state->character.LevelUp();
  }
  EXPECT_TRUE(state->character.LearnSkill(flurry, 1));
  return state;
}

// The same character, holding a summon that pulses beside their swing: two
// sources landing on one monster, which is what the arena has to keep apart.
std::unique_ptr<GameState> SummonState() {
  Skill phoenix;
  phoenix.set_name("Phoenix");
  phoenix.set_kind(SKILL_KIND_AUTO_ATTACK);
  phoenix.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  phoenix.set_max_level(1);
  phoenix.set_max_enemies(1);
  phoenix.set_lines(2);
  phoenix.set_cast_interval_seconds(0.5);
  phoenix.mutable_base()->set_skill_pct(1.0);
  std::unique_ptr<GameState> state = EightLineState(phoenix);
  EXPECT_TRUE(state->character.LearnSkill(phoenix, 1));
  return state;
}

// The screen itself, for a test that reads colours rather than characters.
ftxui::Screen RenderScreen(const BossRun& run, int width = 120,
                           int height = 30) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, BossFightPanel(run));
  return screen;
}

// One number the arena drew, and where: a run of neighbouring cells in a
// damage number's own colours, read straight off the screen.
struct DrawnNumber {
  std::string text;
  int row = 0;
  int column = 0;
  bool crit = false;
};

// Whether this cell is a digit in a damage number's colours. A panel's own
// title is written in the same blue, so a run of digits a percent sign closes
// is that title's, not a swing's.
bool NumberCell(const ftxui::Pixel& px) {
  return px.character.size() == 1 && isdigit(px.character[0]) &&
         (px.foreground_color == kTheme || px.foreground_color == kOrange);
}

std::vector<DrawnNumber> DrawnNumbers(const ftxui::Screen& screen) {
  std::vector<DrawnNumber> drawn;
  for (int y = 0; y < screen.dimy(); ++y) {
    DrawnNumber number;
    for (int x = 0; x <= screen.dimx(); ++x) {
      const ftxui::Pixel* px =
          x < screen.dimx() ? &screen.PixelAt(x, y) : nullptr;
      if (px != nullptr && NumberCell(*px)) {
        if (number.text.empty()) {
          number.row = y;
          number.column = x;
          number.crit = px->foreground_color == kOrange;
        }
        number.text += px->character;
        continue;
      }
      if (!number.text.empty() && (px == nullptr || px->character != "%")) {
        drawn.push_back(number);
      }
      number = DrawnNumber();
    }
  }
  return drawn;
}

// The screen row a bar's top border is drawn on, found by the name on it.
int PanelTop(const std::vector<std::string>& rows, const std::string& title) {
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    if (rows[y].find(title) != std::string::npos) {
      return y;
    }
  }
  return -1;
}

// Steps the fight until a stack holding a critical line -- or a plain one --
// is on screen, and says whether it found one.
bool RunUntilLine(BossRun& run, GameState& state, bool crit) {
  for (int step = 0; step < 2000; ++step) {
    run.Advance(state, 0.05);
    for (const DamageStack& stack : run.damage_stacks()) {
      for (const DamageNumber& line : stack.lines) {
        if (line.crit == crit) {
          return true;
        }
      }
    }
  }
  return false;
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

// A fight of one room has no phase to name: "P1" would only ask the player
// which other phase there was.
TEST(BossFightPanelTest, AOnePhaseFightNamesNoPhase) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  boss.mutable_difficulties(0)->mutable_phases()->DeleteSubrange(1, 1);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  EXPECT_EQ(FightHeading(run), "Normal Zakum - 100%");
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
  int x = 0;
  for (const std::string& mob : {"arm", "body"}) {
    Spawn* spawn = phase->add_spawns();
    spawn->set_mob(mob);
    spawn->add_spots()->set_x(x);
    x += 4;
  }
  AddSpots(phase, {{2, 1}});
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

// A party's fight: the whole of Zakum's arena with three people in it, this
// player on the floor and the other two on the ledges.
std::unique_ptr<TestAuthority> PartyOfThree() {
  std::unique_ptr<TestAuthority> authority = std::make_unique<TestAuthority>(8);
  authority->fight_.players.resize(3);
  authority->fight_.players[0].name = "Dagger";
  authority->fight_.players[0].spot = 0;
  authority->fight_.players[1].name = "Wand";
  authority->fight_.players[1].spot = 1;
  authority->fight_.players[2].name = "Claw";
  authority->fight_.players[2].spot = 2;
  return authority;
}

TEST(BossFightPanelTest, EverybodyInThePartyStandsInTheArena) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  Boss boss = Zakum();
  std::unique_ptr<TestAuthority> authority = PartyOfThree();
  BossRun run("zakum", boss, 0, authority.get());
  run.Advance(*state, 0.1);

  std::vector<std::string> rows = Rows(run);
  // This player is not named, and everybody else is.
  EXPECT_NE(RowOf(rows, "You"), -1);
  EXPECT_NE(RowOf(rows, "Wand"), -1);
  EXPECT_NE(RowOf(rows, "Claw"), -1);
  EXPECT_EQ(RowOf(rows, "Dagger"), -1);
  // Five spots, three of them stood on.
  EXPECT_EQ(MarkedSpots(run), 2);
  // The two on the ledges are drawn above the one on the floor.
  EXPECT_LT(RowOf(rows, "Wand"), RowOf(rows, "You"));
}

TEST(BossFightPanelTest, APlayerWhoLeavesLeavesAnEmptySpot) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  Boss boss = Zakum();
  std::unique_ptr<TestAuthority> authority = PartyOfThree();
  BossRun run("zakum", boss, 0, authority.get());
  run.Advance(*state, 0.1);
  ASSERT_NE(RowOf(Rows(run), "Wand"), -1);

  authority->fight_.players[1].present = false;
  run.Advance(*state, 0.1);

  // Their panel goes, the rest of the party stays, and the spot they stood on
  // is one of the empty ones again.
  std::vector<std::string> rows = Rows(run);
  EXPECT_EQ(RowOf(rows, "Wand"), -1);
  EXPECT_NE(RowOf(rows, "You"), -1);
  EXPECT_NE(RowOf(rows, "Claw"), -1);
  EXPECT_EQ(MarkedSpots(run), 3);
}

// Their numbers are drawn in their own faint colours, so a fight with three
// people in it still reads as the player's own.
TEST(BossFightPanelTest, APartyMembersNumbersAreDrawnFaint) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  Boss boss = Zakum();
  std::unique_ptr<TestAuthority> authority = PartyOfThree();
  BossRun run("zakum", boss, 0, authority.get());
  run.Advance(*state, 0.1);
  authority->OtherLanded(0, 4242);
  run.Advance(*state, 0.1);

  ftxui::Screen screen = RenderScreen(run, 60, 40);
  int faint = 0;
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const ftxui::Pixel& px = screen.PixelAt(x, y);
      if (px.character == "4" && px.foreground_color == kFaintTheme) {
        ++faint;
      }
    }
  }
  EXPECT_EQ(faint, 2) << "4242 holds two of them";
}

// A swing puts its numbers over the monster it hit, all of them where there is
// room, and written plainly -- no commas, whatever the number.
TEST(BossFightPanelTest, ASwingStandsOverWhatItHit) {
  std::unique_ptr<GameState> state = EightLineState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  ASSERT_EQ(run.damage_stacks().size(), 1u);
  const DamageStack& stack = run.damage_stacks().front();
  ASSERT_EQ(stack.lines.size(), 8u);

  ftxui::Screen screen = RenderScreen(run, 60, 40);
  std::vector<DrawnNumber> drawn = DrawnNumbers(screen);
  ASSERT_EQ(drawn.size(), stack.lines.size()) << "a tall arena fits them all";
  int bar = PanelTop(RowsOf(screen), "Zakum's Arm");
  ASSERT_NE(bar, -1);
  for (std::size_t i = 0; i < drawn.size(); ++i) {
    EXPECT_EQ(drawn[i].text, std::to_string(stack.lines[i].damage));
    EXPECT_LT(drawn[i].row, bar) << "over the bar, not on or under it";
    EXPECT_EQ(drawn[i].text.find(","), std::string::npos);
  }
}

// A row with something already in it costs that one number. The rest of the
// stack stays where it was rather than sliding out from over the monster.
TEST(BossFightPanelTest, ABlockedRowCostsItsOwnNumberAndNoOther) {
  std::unique_ptr<GameState> state = EightLineState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  ASSERT_EQ(run.damage_stacks().size(), 1u);
  const DamageStack& stack = run.damage_stacks().front();

  // Short enough that the top of the arena cuts the stack off partway.
  std::vector<DrawnNumber> drawn = DrawnNumbers(RenderScreen(run, 60, 16));
  ASSERT_FALSE(drawn.empty());
  ASSERT_LT(drawn.size(), stack.lines.size());
  // What survives is the end of the stack nearest the monster, each number
  // still on the row it would have had.
  for (std::size_t i = 0; i < drawn.size(); ++i) {
    std::size_t line = stack.lines.size() - drawn.size() + i;
    EXPECT_EQ(drawn[i].text, std::to_string(stack.lines[line].damage));
  }
}

// The numbers stand clear of the bars: every cell of a bar carries the fill's
// own background, so a number sitting on one is caught by its background
// rather than by hunting for the name it covered.
TEST(BossFightPanelTest, ANumberNeverSitsOnABar) {
  std::unique_ptr<GameState> state = SummonState();
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));

  ftxui::Screen screen = RenderScreen(run);
  int drawn = 0;
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const ftxui::Pixel& px = screen.PixelAt(x, y);
      if (!NumberCell(px)) {
        continue;
      }
      ++drawn;
      EXPECT_NE(px.background_color, kRed) << "on an arm's fill";
      EXPECT_NE(px.background_color, kBarEmpty) << "on an arm's empty bar";
    }
  }
  EXPECT_GT(drawn, 0) << "something was drawn to be tested";
}

// Where the clock is drawn, as {left, right, top, bottom} in screen cells.
// Found by its own text: nothing else on the screen holds a digit, a colon
// and a digit in a row.
ftxui::Box ClockBox(const std::vector<std::string>& rows) {
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    for (int x = 1; x + 1 < static_cast<int>(rows[y].size()); ++x) {
      if (rows[y][x] != ':' || !isdigit(rows[y][x - 1]) ||
          !isdigit(rows[y][x + 1])) {
        continue;
      }
      std::size_t left = rows[y].rfind('#', x);
      std::size_t right = rows[y].find('#', x);
      EXPECT_NE(left, std::string::npos);
      EXPECT_NE(right, std::string::npos);
      return {static_cast<int>(left), static_cast<int>(right), y - 1, y + 1};
    }
  }
  return {-1, -1, -1, -1};
}

// How many numbers are drawn on the clock's rows, having checked that not one
// of them touches the clock itself.
int NumbersBesideTheClock(const ftxui::Screen& screen) {
  ftxui::Box clock = ClockBox(RowsOf(screen));
  EXPECT_GE(clock.x_min, 0) << "the clock was not found";
  int beside = 0;
  for (const DrawnNumber& number : DrawnNumbers(screen)) {
    if (number.row < clock.y_min || number.row > clock.y_max) {
      continue;
    }
    ++beside;
    int last = number.column + static_cast<int>(number.text.size()) - 1;
    EXPECT_TRUE(last < clock.x_min || number.column > clock.x_max)
        << number.text << " is drawn on the clock";
  }
  return beside;
}

// The clock stands inside the arena rather than on a strip of its own: a stack
// may use its rows, and may not touch one cell of it.
TEST(BossFightPanelTest, NumbersShareTheClocksRowsButNotItsBox) {
  // Zakum's arms stand either side of the clock, so their numbers climb past
  // it into its rows.
  std::unique_ptr<GameState> state = EightLineState();
  Boss zakum = Zakum();
  BossRun run("zakum", zakum, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  EXPECT_GT(NumbersBesideTheClock(RenderScreen(run)), 0)
      << "no number reached the clock's rows";

  // A bar directly under the clock, in an arena one cell wide: its stack
  // reaches the clock head on, and every cell of the clock survives it.
  std::unique_ptr<GameState> under = EightLineState();
  Boss column = ColumnBoss();
  BossRun below("zakum", column, 0);
  below.Advance(*under, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(below, *under, false));
  std::vector<std::string> rows = RowsOf(RenderScreen(below, 120, 18));
  ftxui::Box clock = ClockBox(rows);
  ASSERT_GE(clock.x_min, 0);
  int width = clock.x_max - clock.x_min + 1;
  EXPECT_EQ(rows[clock.y_min].substr(clock.x_min, width),
            std::string(width, '#'))
      << "the clock's top border";
  EXPECT_EQ(rows[clock.y_max].substr(clock.x_min, width),
            std::string(width, '#'))
      << "the clock's bottom border";
  std::string middle = rows[clock.y_min + 1].substr(clock.x_min, width);
  EXPECT_EQ(middle.front(), '#');
  EXPECT_EQ(middle.back(), '#');
  EXPECT_EQ(middle.substr(1, width - 2),
            " " + FightClock(below.seconds_left()) + " ");
}

// Orange for a critical line and the theme blue for a plain one, which is the
// whole of what a number's colour says.
TEST(BossFightPanelTest, ACriticalLineIsOrangeAndAPlainOneIsBlue) {
  for (bool crit : {false, true}) {
    std::unique_ptr<GameState> state = EightLineState();
    Boss boss = OneArmBoss();
    BossRun run("zakum", boss, 0);
    run.Advance(*state, kBossCountdownSeconds);
    ASSERT_TRUE(RunUntilLine(run, *state, crit)) << "crit: " << crit;
    ASSERT_EQ(run.damage_stacks().size(), 1u);
    const DamageStack& stack = run.damage_stacks().front();

    std::vector<DrawnNumber> drawn = DrawnNumbers(RenderScreen(run, 60, 40));
    ASSERT_EQ(drawn.size(), stack.lines.size());
    int matched = 0;
    for (std::size_t i = 0; i < drawn.size(); ++i) {
      EXPECT_EQ(drawn[i].crit, stack.lines[i].crit) << drawn[i].text;
      matched += stack.lines[i].crit == crit ? 1 : 0;
    }
    EXPECT_GT(matched, 0) << "crit: " << crit;
  }
}

// A summon's numbers never stand over a monster: that space is the swing's,
// whether or not a swing is holding it just now.
TEST(BossFightPanelTest, OnlyTheSwingStandsOverAMonster) {
  std::unique_ptr<GameState> state = SummonState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  // Until the arm is holding both lots of numbers at once, which is the case
  // the reserved space is for.
  bool together = false;
  for (int step = 0; step < 400 && !together; ++step) {
    run.Advance(*state, 0.05);
    bool swing = false;
    bool summon = false;
    for (const DamageStack& stack : run.damage_stacks()) {
      swing = swing || stack.source.origin == DamageOrigin::kSwing;
      summon = summon || stack.source.origin == DamageOrigin::kOwnClock;
    }
    together = swing && summon;
  }
  ASSERT_TRUE(together);

  std::set<std::string> swung;
  std::set<std::string> summoned;
  for (const DamageStack& stack : run.damage_stacks()) {
    for (const DamageNumber& line : stack.lines) {
      bool swing = stack.source.origin == DamageOrigin::kSwing;
      (swing ? swung : summoned).insert(std::to_string(line.damage));
    }
  }

  ftxui::Screen screen = RenderScreen(run, 60, 40);
  int bar = PanelTop(RowsOf(screen), "Zakum's Arm");
  ASSERT_NE(bar, -1);
  int above = 0;
  int beside = 0;
  for (const DrawnNumber& number : DrawnNumbers(screen)) {
    if (number.row < bar) {
      ++above;
      EXPECT_GT(swung.count(number.text), 0u)
          << number.text << " stood over the arm and was not the swing's";
    } else {
      beside += summoned.count(number.text) > 0 ? 1 : 0;
    }
  }
  EXPECT_GT(above, 0) << "the swing stood over the arm";
  EXPECT_GT(beside, 0) << "the summon was drawn, beside the arm";
}

// The column over a monster stays the swing's even while no swing is holding
// it: a summon's numbers never drift into the gap over the lower of two arms,
// whichever side they happened to reach for.
TEST(BossFightPanelTest, TheColumnOverAMonsterStaysTheSwings) {
  std::unique_ptr<GameState> state = SummonState();
  Boss boss = ColumnBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  int summons_drawn = 0;
  for (int step = 0; step < 300; ++step) {
    run.Advance(*state, 0.05);
    std::set<std::string> swung;
    std::set<std::string> summoned;
    for (const DamageStack& stack : run.damage_stacks()) {
      for (const DamageNumber& line : stack.lines) {
        bool swing = stack.source.origin == DamageOrigin::kSwing;
        (swing ? swung : summoned).insert(std::to_string(line.damage));
      }
    }
    ftxui::Screen screen = RenderScreen(run, 80, 40);
    std::vector<std::string> rows = RowsOf(screen);
    // The lower of the two bars: everything over it is one swing's or the
    // other's, and nothing else may be there.
    int lower = -1;
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
      if (rows[y].find("Zakum's Arm") != std::string::npos) {
        lower = y;
      }
    }
    ASSERT_NE(lower, -1) << "at step " << step;
    // The bar's own columns, off the borders bracketing its name.
    std::size_t name = rows[lower].find("Zakum's Arm");
    int left = static_cast<int>(rows[lower].rfind('#', name));
    int right = static_cast<int>(rows[lower].find('#', name));
    for (const DrawnNumber& number : DrawnNumbers(screen)) {
      summons_drawn += summoned.count(number.text) > 0 ? 1 : 0;
      int last = number.column + static_cast<int>(number.text.size()) - 1;
      if (number.row < lower && last >= left && number.column <= right) {
        EXPECT_EQ(summoned.count(number.text), 0u)
            << number.text << " stood over the arm at step " << step;
      }
    }
  }
  EXPECT_GT(summons_drawn, 0) << "the summon was drawn somewhere";
}

// A phase on the shipped grid with the player standing wherever `spots` says
// and one monster out of the way, for a test that reads the layout itself.
Boss GridBoss(const std::vector<std::pair<int, int>>& spots) {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(300);
  BossPhase* phase = normal->add_phases();
  Spawn* arm = phase->add_spawns();
  arm->set_mob("arm");
  ArenaSpot* at = arm->add_spots();
  at->set_x(kArenaColumns - 1);
  at->set_y(0);
  AddSpots(phase, spots);
  phase->set_arena_width(kArenaColumns);
  phase->set_arena_height(kArenaRows);
  return boss;
}

// Where every empty spot's marker was drawn, as (row, column). The marker is
// multi-byte, so RowsOf leaves it as "# # #".
std::vector<std::pair<int, int>> EmptySpotsIn(
    const std::vector<std::string>& rows) {
  std::vector<std::pair<int, int>> found;
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    std::size_t at = 0;
    while ((at = rows[y].find("# # #", at)) != std::string::npos) {
      found.push_back({y, static_cast<int>(at)});
      at += 5;
    }
  }
  return found;
}

// The grid every arena stands on, measured against the smallest terminal the
// game is laid out for. One column or one row more than kArenaColumns and
// kArenaRows allow is drawn off the screen or on top of its neighbour, and
// neither shows up in a data file -- so the grid is pinned here, where it can
// be seen, rather than trusted where it is written.
TEST(BossFightPanelTest, TheGridFitsTheSmallestTerminal) {
  // Two rows of border and the bar itself: what every panel in the arena
  // takes, whatever is standing in it.
  constexpr int kPanelRows = 2 + kPlayerBarRows;
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  // A full row of the grid and a full column of it, on alternate cells across
  // and every cell down, which is the most any fight asks of either.
  std::vector<std::pair<int, int>> spots;
  for (int x = 0; x < kArenaColumns; x += 2) {
    spots.push_back({x, kArenaRows - 1});
  }
  for (int y = 0; y + 1 < kArenaRows; ++y) {
    spots.push_back({0, y});
  }
  Boss boss = GridBoss(spots);
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);

  // The player stands on the first spot and is drawn as their own panel, so
  // every other spot is an empty one and all of them are on the screen.
  std::vector<std::pair<int, int>> drawn = EmptySpotsIn(Rows(run));
  ASSERT_EQ(drawn.size(), spots.size() - 1);
  for (std::size_t i = 1; i < drawn.size(); ++i) {
    const std::pair<int, int>& a = drawn[i - 1];
    const std::pair<int, int>& b = drawn[i];
    if (a.first == b.first) {
      EXPECT_GE(b.second - a.second, kBossPanelWidth)
          << "two panels overlap on screen row " << a.first;
    } else {
      EXPECT_GE(b.first - a.first, kPanelRows)
          << "two arena rows overlap at screen row " << b.first;
    }
  }
}

}  // namespace
}  // namespace ms
