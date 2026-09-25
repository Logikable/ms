#include "src/frontend/screens/boss_fight_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/skill_placement.h"
#include "src/combat/boss_run.h"
#include "src/combat/test_authority.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/marquee.h"
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

// The places a phase lets the player stand, as (x, y) pairs. The first is where
// they start.
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
  // below and a ledge over each end, as the data file lays them out.
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

// One arm in its own column with the player below and nothing on either side:
// an arena where a stack can only go over its monster.
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

// Two arms in one column with the player below. The space over the lower arm is
// the gap between the two bars, which is exactly where a stack under the upper
// one would go.
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
// multibyte, so it is written as a single '#'. These rows are read for which
// column something is in, and a byte offset doesn't give that.
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
  ftxui::Render(screen, BossFightPanel(run, true));
  return RowsOf(screen);
}

// The column where `needle` starts, or -1 if it isn't there.
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

// A character with an attack that hits one enemy eight times, so a stack has
// enough numbers to be crowded out of a corner.
std::unique_ptr<GameState> EightLineState(Skill beside = Skill()) {
  Skill flurry;
  flurry.set_name("Flurry");
  flurry.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(flurry, JOB_ADVANCEMENT_SWORDMAN);
  flurry.set_max_level(1);
  flurry.set_max_enemies(1);
  flurry.set_lines(8);
  // Slower than a stack's lifetime, so only one is on screen at a time.
  flurry.set_base_delay_ms(2000);
  flurry.mutable_base()->set_skill_pct(5.0);
  std::map<std::string, Skill> book = {{"flurry", flurry}};
  if (!beside.name().empty()) {
    book[beside.name()] = beside;
  }
  std::unique_ptr<GameState> state = MakeState(1000000000, 1, std::move(book));
  state->character.AdvanceJob(JOB_SWORDMAN);
  // Up to the arms' own level. Forty levels below a monster, all damage drops
  // to the minimum of one, and every attack would tie with the basic attack.
  for (int i = 0; i < 110; ++i) {
    state->character.LevelUp();
  }
  EXPECT_TRUE(state->character.LearnSkill(flurry, 1));
  return state;
}

// A character whose attack hits four times with two lines each: the same eight
// numbers, shown as four strikes the screen flashes through.
std::unique_ptr<GameState> FourStrikeState() {
  Skill illusion;
  illusion.set_name("Illusion");
  illusion.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(illusion, JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(1);
  illusion.set_max_enemies(1);
  illusion.set_lines(2);
  illusion.set_casts(4);
  illusion.set_base_delay_ms(2000);
  illusion.mutable_base()->set_skill_pct(5.0);
  std::map<std::string, Skill> book = {{"illusion", illusion}};
  std::unique_ptr<GameState> state = MakeState(1000000000, 1, std::move(book));
  state->character.AdvanceJob(JOB_SWORDMAN);
  for (int i = 0; i < 110; ++i) {
    state->character.LevelUp();
  }
  EXPECT_TRUE(state->character.LearnSkill(illusion, 1));
  return state;
}

// The same character with a summon that hits alongside their attack: two
// sources on one monster, which the arena has to keep apart.
std::unique_ptr<GameState> SummonState() {
  Skill phoenix;
  phoenix.set_name("Phoenix");
  phoenix.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(phoenix, JOB_ADVANCEMENT_SWORDMAN);
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
  ftxui::Render(screen, BossFightPanel(run, true));
  return screen;
}

// One number the arena drew, and where: a run of adjacent cells in a damage
// number's colours, read from the screen.
struct DrawnNumber {
  std::string text;
  int row = 0;
  int column = 0;
  bool crit = false;
  // A party member's number rather than the player's, judged by its colour.
  bool faint = false;
};

// Whether this cell is a digit in a damage number's colours. A panel's title
// uses the same blue, so a run of digits ending in a percent sign belongs to
// the title, not an attack.
bool NumberCell(const ftxui::Pixel& px) {
  return px.character.size() == 1 && isdigit(px.character[0]) &&
         (px.foreground_color == kTheme || px.foreground_color == kOrange ||
          px.foreground_color == kFaintTheme ||
          px.foreground_color == kFaintOrange);
}

bool FaintCell(const ftxui::Pixel& px) {
  return px.foreground_color == kFaintTheme ||
         px.foreground_color == kFaintOrange;
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
          number.crit = px->foreground_color == kOrange ||
                        px->foreground_color == kFaintOrange;
          number.faint = FaintCell(*px);
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

// The screen row of a bar's top border, found by the name on it.
int PanelTop(const std::vector<std::string>& rows, const std::string& title) {
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    if (rows[y].find(title) != std::string::npos) {
      return y;
    }
  }
  return -1;
}

// Steps the fight until the player has a critical line (or a plain one) on
// screen, and returns whether it found one.
bool RunUntilLine(BossRun& run, GameState& state, bool crit) {
  for (int step = 0; step < 2000; ++step) {
    run.Advance(state, 0.05);
    for (const DamageWrite& write : run.damage_writes()) {
      for (const DamageNumber& line : write.lines) {
        if (line.crit == crit) {
          return true;
        }
      }
    }
  }
  return false;
}

// The rows over the only monster in a one-bar fight.
std::vector<DamageRow> OnlyColumn(const BossRun& run) {
  return DamageColumn(run.damage_writes(), run.slots().front().id);
}

// The numbers of `column` read down the screen, top row first. That is the
// reverse of the rows, which are read up from the bar. Empty rows are skipped.
std::vector<std::string> ColumnDownwards(const std::vector<DamageRow>& column) {
  std::vector<std::string> text;
  for (std::size_t i = column.size(); i > 0; --i) {
    if (column[i - 1].filled) {
      text.push_back(std::to_string(column[i - 1].number.damage));
    }
  }
  return text;
}

std::string Render(const BossRun& run) {
  // Wide enough for the whole arena: three panels and the gaps between them.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Render(screen, BossFightPanel(run, true));
  return screen.ToString();
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

// Practice comes first in the heading: the end holds the phase and the percent,
// which change, and what the run is worth shouldn't have to be found among
// them.
TEST(BossFightPanelTest, APracticeRunSaysSoBeforeAnythingElse) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0, /*authority=*/nullptr, /*practice=*/true);
  run.Advance(*state, kBossCountdownSeconds);
  EXPECT_EQ(FightHeading(run), "Practice - Normal Zakum - P1 - 100%");

  run.Abort();
  EXPECT_EQ(FightHeading(run), "Practice - Normal Zakum - Left");
}

// A one-room fight has no phase to name: "P1" would only make the player wonder
// what the other phase was.
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
  // Eight arms, four on each side.
  int drawn = 0;
  for (std::size_t at = out.find("Zakum's Arm"); at != std::string::npos;
       at = out.find("Zakum's Arm", at + 1)) {
    ++drawn;
  }
  EXPECT_EQ(drawn, 8);
}

// A dead arm's bar disappears, but its side still has four rows, so the bars
// beside it don't move.
TEST(BossFightPanelTest, ADeadArmLeavesItsSlotEmpty) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000000);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  int before = 0;
  {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(90),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, BossFightPanel(run, true));
    before = screen.dimy();
  }
  for (int i = 0; i < 200 && run.slots()[0].alive; ++i) {
    run.Advance(*state, 0.05);
  }
  run.Advance(*state, kBossDeathHoldSeconds);
  ASSERT_FALSE(run.slots()[0].visible);

  ftxui::Screen after = ftxui::Screen::Create(ftxui::Dimension::Fixed(90),
                                              ftxui::Dimension::Fixed(30));
  ftxui::Render(after, BossFightPanel(run, true));
  EXPECT_EQ(after.dimy(), before);
  // The dead arm isn't drawn, and the living ones are.
  std::string out = after.ToString();
  EXPECT_NE(out.find("Zakum's Arm"), std::string::npos);
}

// The countdown sits where the attack name will be, so the thing about to
// change is where the eye already is.
TEST(BossFightPanelTest, TheCountdownShowsOnThePlayerPanel) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  EXPECT_NE(Render(run).find("3"), std::string::npos);
  run.Advance(*state, 2.5);
  EXPECT_NE(Render(run).find("1"), std::string::npos);
}

// A name too long for one row wraps over the player's two rows instead of
// widening the whole arena.
TEST(BossFightPanelTest, ALongSwingNameWrapsOverThePlayersRows) {
  Skill carnival;
  carnival.set_name("Midnight Carnival");
  carnival.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(carnival, JOB_ADVANCEMENT_SWORDMAN);
  carnival.set_max_level(1);
  carnival.set_max_enemies(6);
  carnival.mutable_base()->set_skill_pct(5.0);
  std::unique_ptr<GameState> state =
      MakeState(1000000000, 1, {{"carnival", carnival}});
  state->character.AdvanceJob(JOB_SWORDMAN);
  // SP comes with levels, and only after level 10.
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

// The body is drawn above the player, not off to one side.
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

// A phase where two parts stand four cells apart, with one cell of margin to
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

// This is what spots are for: a part is drawn where the fight puts it, in the
// order and on the row the phase asked for.
TEST(BossFightPanelTest, EachPartStandsWhereItsSpotSays) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  // Names of the same length, so both are centred in their panels the same way
  // and the columns between them belong to the panels.
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
  // The one in the first cell is against the edge, and the one in the
  // second-to-last cell keeps the cell of margin the arena asked for.
  EXPECT_LT(left, kBossPanelWidth);
  EXPECT_GT(right + kBossPanelWidth, 100);
  // The player is below them, not beside them.
  EXPECT_GT(RowOf(rows, "You"), RowOf(rows, "Left Hand"));
}

// The arena fills the screen: the bars keep their size and the space between
// them takes the rest, so a wider terminal spreads the fight out instead of
// bunching it in the middle.
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
// phase gets the same two so the arena's rows stay even.
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

// How many standing spots are drawn empty. The player is on one of them, so a
// five-spot phase marks four.
int MarkedSpots(const BossRun& run) {
  std::string out = Render(run);
  int found = 0;
  for (std::size_t at = out.find("· · ·"); at != std::string::npos;
       at = out.find("· · ·", at + 1)) {
    ++found;
  }
  return found;
}

// The spots are drawn: every spot the player isn't on is marked, and the one
// they are on shows their panel instead.
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

  // Up to the ledge, a row the player wasn't on a moment ago.
  int floor = RowOf(Rows(run), "You");
  run.MovePlayer(0, -1);
  EXPECT_LT(RowOf(Rows(run), "You"), floor);
}

// A duration in the units the run counts in.
double Seconds(std::chrono::milliseconds ms) {
  return ms.count() / 1000.0;
}

// Steps `run` until its clock reads `seconds`, in steps no longer than a frame,
// so the arena draws what it would have drawn along the way.
void RunTo(BossRun& run, GameState& state, double seconds) {
  const double kFrame = 0.03;
  while (run.elapsed_seconds() < seconds) {
    run.Advance(state, std::min(kFrame, seconds - run.elapsed_seconds()));
  }
}

// A party fight: Zakum's whole arena with three people in it, this player on
// the floor and the other two on the ledges.
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
  // This player isn't named, and everyone else is.
  EXPECT_NE(RowOf(rows, "You"), -1);
  EXPECT_NE(RowOf(rows, "Wand"), -1);
  EXPECT_NE(RowOf(rows, "Claw"), -1);
  EXPECT_EQ(RowOf(rows, "Dagger"), -1);
  // Five spots, three of them occupied.
  EXPECT_EQ(MarkedSpots(run), 2);
  // The two on the ledges are drawn above the one on the floor.
  EXPECT_LT(RowOf(rows, "Wand"), RowOf(rows, "You"));
}

// A name wider than the twelve-column plate isn't just cut: it scrolls on the
// run's own clock, over and over, since there is no cursor here to start it and
// nobody is waiting on it.
TEST(BossFightPanelTest, ALongPartyNameSlidesUnderItsPlateAndComesBack) {
  const std::string kLong = "Twenty Characters Ok";
  const int kPlate = kBossPanelWidth - 4;  // the plate inside the frame
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  Boss boss = Zakum();
  std::unique_ptr<TestAuthority> authority = PartyOfThree();
  authority->fight_.players[1].name = kLong;
  BossRun run("zakum", boss, 0, authority.get());

  // A round trip is a pause at each end plus a step for every offset between.
  double step = Seconds(kMarqueeStep);
  double pause = Seconds(kMarqueePause);
  double slide = step * (static_cast<int>(kLong.size()) - kPlate - 1);

  // The start first, held for the pause so it can be read before it moves.
  RunTo(run, *state, 0.1);
  EXPECT_NE(RowOf(Rows(run), kLong.substr(0, kPlate)), -1);

  // Through the scroll and into the pause at the far end, where the end of the
  // name is.
  RunTo(run, *state, pause + slide + pause / 2);
  EXPECT_EQ(RowOf(Rows(run), kLong.substr(0, kPlate)), -1) << "the head went";
  EXPECT_NE(RowOf(Rows(run), kLong.substr(kLong.size() - kPlate)), -1);

  // Round again: the name starts over instead of staying at its end.
  RunTo(run, *state, 2 * pause + slide + 0.1);
  EXPECT_NE(RowOf(Rows(run), kLong.substr(0, kPlate)), -1);
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

  // Their panel disappears, the rest of the party stays, and the spot they
  // stood on is empty again.
  std::vector<std::string> rows = Rows(run);
  EXPECT_EQ(RowOf(rows, "Wand"), -1);
  EXPECT_NE(RowOf(rows, "You"), -1);
  EXPECT_NE(RowOf(rows, "Claw"), -1);
  EXPECT_EQ(MarkedSpots(run), 3);
}

// Party members' numbers are drawn in their own faint colours, so a fight with
// three people still reads as the player's own.
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

// An attack puts its numbers over the monster it hit, all of them where there
// is room, without commas whatever the number, and stacked upwards: the first
// line to land is at the bottom.
TEST(BossFightPanelTest, ASwingStandsOverWhatItHit) {
  std::unique_ptr<GameState> state = EightLineState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  std::vector<std::string> want = ColumnDownwards(OnlyColumn(run));
  ASSERT_EQ(want.size(), 8u);

  ftxui::Screen screen = RenderScreen(run, 60, 40);
  std::vector<DrawnNumber> drawn = DrawnNumbers(screen);
  ASSERT_EQ(drawn.size(), want.size()) << "a tall arena fits them all";
  int bar = PanelTop(RowsOf(screen), "Zakum's Arm");
  ASSERT_NE(bar, -1);
  for (std::size_t i = 0; i < drawn.size(); ++i) {
    EXPECT_EQ(drawn[i].text, want[i]);
    EXPECT_LT(drawn[i].row, bar) << "over the bar, not on or under it";
    EXPECT_EQ(drawn[i].text.find(","), std::string::npos);
  }
}

// The numbers are right-aligned: a short one and a long one in the same column
// end on the same cell, so the digits line up.
TEST(BossFightPanelTest, TheNumbersOfAColumnShareTheirRightEdge) {
  std::unique_ptr<GameState> state = EightLineState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));

  std::vector<DrawnNumber> drawn = DrawnNumbers(RenderScreen(run, 60, 40));
  ASSERT_GE(drawn.size(), 2u);
  std::set<std::size_t> widths;
  std::set<int> edges;
  for (const DrawnNumber& number : drawn) {
    widths.insert(number.text.size());
    edges.insert(number.column + static_cast<int>(number.text.size()));
  }
  ASSERT_GT(widths.size(), 1u) << "every number is the same width to begin";
  EXPECT_EQ(edges.size(), 1u) << "the numbers do not end on one cell";
}

// An attack with several strikes records one write per strike, and they appear
// one after another, so the two rows it fills flash rather than the whole
// attack appearing at once.
TEST(BossFightPanelTest, ASwingOfSeveralStrikesFlashesThroughThem) {
  std::unique_ptr<GameState> state = FourStrikeState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  ASSERT_EQ(run.damage_writes().size(), 4u) << "four slashes, a write each";
  for (const DamageWrite& write : run.damage_writes()) {
    EXPECT_EQ(write.lines.size(), 2u);
  }

  // Two rows hold the attack, whichever strike is showing.
  std::vector<std::string> first = ColumnDownwards(OnlyColumn(run));
  ASSERT_EQ(first.size(), 2u) << "the whole swing went up at once";
  std::vector<DrawnNumber> drawn = DrawnNumbers(RenderScreen(run, 60, 40));
  ASSERT_EQ(drawn.size(), 2u);
  EXPECT_EQ(drawn[0].text, first[0]);
  EXPECT_EQ(drawn[1].text, first[1]);

  // One frame later the next strike is due and has taken the same two rows.
  run.Advance(*state, kDamageStrikeSeconds);
  std::vector<std::string> next = ColumnDownwards(OnlyColumn(run));
  ASSERT_EQ(next.size(), 2u);
  EXPECT_NE(next, first) << "the strikes flash through the rows";
}

// The strikes advance one per frame, and the last one stays for the rest of the
// stack's lifetime: an attack that has finished flashing doesn't go blank.
TEST(BossFightPanelTest, TheStrikesStepOneAFrameAndTheLastOneHolds) {
  DamageStack stack;
  stack.lines = {{10, false}, {11, false}, {20, false}, {30, false}};
  stack.strike_starts = {0, 2, 3};

  EXPECT_EQ(stack.StrikeAt(0.0), std::make_pair(0, 2));
  EXPECT_EQ(stack.StrikeAt(kDamageStrikeSeconds), std::make_pair(2, 3));
  EXPECT_EQ(stack.StrikeAt(2 * kDamageStrikeSeconds), std::make_pair(3, 4));
  EXPECT_EQ(stack.StrikeAt(kDamageStackSeconds), std::make_pair(3, 4))
      << "the last strike holds rather than the stack going blank";
  EXPECT_EQ(stack.TallestStrike(), 2);

  // An attack that landed once is one strike and stays still for its lifetime.
  DamageStack single;
  single.lines = {{10, false}, {11, false}};
  single.strike_starts = {0};
  EXPECT_EQ(single.StrikeAt(0.0), std::make_pair(0, 2));
  EXPECT_EQ(single.StrikeAt(kDamageStackSeconds), std::make_pair(0, 2));
}

// A row that is already occupied costs only that one number. The rest of the
// stack stays where it was instead of sliding away from over the monster.
TEST(BossFightPanelTest, ABlockedRowCostsItsOwnNumberAndNoOther) {
  std::unique_ptr<GameState> state = EightLineState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  std::vector<std::string> want = ColumnDownwards(OnlyColumn(run));

  // Short enough that the top of the arena cuts off part of the column.
  std::vector<DrawnNumber> drawn = DrawnNumbers(RenderScreen(run, 60, 16));
  ASSERT_FALSE(drawn.empty());
  ASSERT_LT(drawn.size(), want.size());
  // What survives is the end nearest the monster, with each number still on the
  // row it would have had.
  for (std::size_t i = 0; i < drawn.size(); ++i) {
    EXPECT_EQ(drawn[i].text, want[want.size() - drawn.size() + i]);
  }
}

// The numbers stay clear of the bars. Every cell of a bar has the fill's
// background, so a number on one is caught by its background rather than by
// looking for the name it covered.
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
// Found by its text: nothing else on the screen has a digit, a colon and a
// digit in a row.
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

// How many numbers are drawn on the clock's rows, after checking that none of
// them touches the clock itself.
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

// The clock is inside the arena rather than on its own strip: a stack may use
// its rows but may not touch any cell of it.
TEST(BossFightPanelTest, NumbersShareTheClocksRowsButNotItsBox) {
  // Zakum's arms are on either side of the clock, so their numbers climb past
  // it into its rows.
  std::unique_ptr<GameState> state = EightLineState();
  Boss zakum = Zakum();
  BossRun run("zakum", zakum, 0);
  run.Advance(*state, kBossCountdownSeconds);
  ASSERT_TRUE(RunUntilLine(run, *state, false));
  EXPECT_GT(NumbersBesideTheClock(RenderScreen(run)), 0)
      << "no number reached the clock's rows";

  // A bar directly under the clock in an arena one cell wide: its stack reaches
  // the clock head on, and every cell of the clock survives.
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
            " " + FormatClock(below.seconds_left()) + " ");
}

// Orange for a critical line and the theme blue for a plain one. That is all a
// number's colour means.
TEST(BossFightPanelTest, ACriticalLineIsOrangeAndAPlainOneIsBlue) {
  for (bool crit : {false, true}) {
    std::unique_ptr<GameState> state = EightLineState();
    Boss boss = OneArmBoss();
    BossRun run("zakum", boss, 0);
    run.Advance(*state, kBossCountdownSeconds);
    ASSERT_TRUE(RunUntilLine(run, *state, crit)) << "crit: " << crit;
    std::vector<DamageRow> column = OnlyColumn(run);

    std::vector<DrawnNumber> drawn = DrawnNumbers(RenderScreen(run, 60, 40));
    ASSERT_EQ(drawn.size(), column.size());
    int matched = 0;
    for (std::size_t i = 0; i < drawn.size(); ++i) {
      const DamageRow& row = column[drawn.size() - 1 - i];
      EXPECT_EQ(drawn[i].crit, row.number.crit) << drawn[i].text;
      matched += row.number.crit == crit ? 1 : 0;
    }
    EXPECT_GT(matched, 0) << "crit: " << crit;
  }
}

// Every damage source the player has writes into the one column over the
// monster: a summon's numbers take rows there just like an attack's, instead of
// going somewhere of their own.
TEST(BossFightPanelTest, ASummonWritesIntoTheSameColumnAsTheSwing) {
  std::unique_ptr<GameState> state = SummonState();
  Boss boss = OneArmBoss();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  // Until the arm has taken both, which is the case the shared column is for.
  std::set<std::string> summoned;
  for (int step = 0; step < 400 && summoned.empty(); ++step) {
    run.Advance(*state, 0.05);
    for (const DamageWrite& write : run.damage_writes()) {
      if (!write.live()) {
        continue;
      }
      for (const DamageNumber& line : write.lines) {
        summoned.insert(std::to_string(line.damage));
      }
    }
  }
  ASSERT_FALSE(summoned.empty());

  // Everything on screen is over the arm, whatever dealt it.
  ftxui::Screen screen = RenderScreen(run, 60, 40);
  int bar = PanelTop(RowsOf(screen), "Zakum's Arm");
  ASSERT_NE(bar, -1);
  std::vector<DrawnNumber> drawn = DrawnNumbers(screen);
  ASSERT_FALSE(drawn.empty());
  for (const DrawnNumber& number : drawn) {
    EXPECT_LT(number.row, bar) << number.text << " was not over the arm";
    EXPECT_GT(summoned.count(number.text), 0u) << number.text;
  }
  EXPECT_EQ(drawn.size(), ColumnDownwards(OnlyColumn(run)).size());
}

// The column over a monster belongs to this player, and a party member's
// numbers stay out of it, even while the player has no number there.
TEST(BossFightPanelTest, APartyMembersNumbersStayOutOfTheColumn) {
  // Arms nothing can kill, so the party's numbers always have somewhere to go;
  // a dead monster has no space.
  std::unique_ptr<GameState> state = MakeState(1000000000, 1000000000);
  Boss boss = ColumnBoss();
  std::unique_ptr<TestAuthority> authority = PartyOfThree();
  BossRun run("zakum", boss, 0, authority.get());
  run.Advance(*state, 0.1);

  int theirs_drawn = 0;
  for (int step = 0; step < 200; ++step) {
    authority->OtherLanded(0, 424242);
    run.Advance(*state, 0.05);
    ftxui::Screen screen = RenderScreen(run, 80, 40);
    std::vector<std::string> rows = RowsOf(screen);
    // The lower of the two bars, whose column belongs to the player.
    int lower = -1;
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
      if (rows[y].find("Zakum's Arm") != std::string::npos) {
        lower = y;
      }
    }
    ASSERT_NE(lower, -1) << "at step " << step;
    // The bar's own columns, from the borders around its name.
    std::size_t name = rows[lower].find("Zakum's Arm");
    int left = static_cast<int>(rows[lower].rfind('#', name));
    int right = static_cast<int>(rows[lower].find('#', name));
    for (const DrawnNumber& number : DrawnNumbers(screen)) {
      if (number.text != "424242") {
        continue;
      }
      ++theirs_drawn;
      int last = number.column + static_cast<int>(number.text.size()) - 1;
      bool over = number.row < lower && last >= left && number.column <= right;
      EXPECT_FALSE(over) << "theirs stood over the arm at step " << step;
    }
  }
  EXPECT_GT(theirs_drawn, 0) << "their numbers were drawn somewhere";
}

// A phase on the game's grid with the player at the given `spots` and one
// monster out of the way, for a test that checks the layout itself.
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

// Where each empty spot's marker was drawn, as (row, column). The marker is
// multibyte, so RowsOf shows it as "# # #".
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

// A grid one column or row past kArenaColumns and kArenaRows draws off screen
// or over its neighbour, and no data file shows it.
TEST(BossFightPanelTest, TheGridFitsTheSmallestTerminal) {
  // Two border rows and the bar itself: what every panel in the arena takes,
  // whatever is in it.
  constexpr int kPanelRows = 2 + kPlayerBarRows;
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  // A full row of the grid and a full column, on alternate cells across and
  // every cell down, which is the most any fight uses of either.
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

  // The player is on the first spot and drawn as their own panel, so every
  // other spot is empty and all of them are on screen.
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

// Three panels across the arena, each a fixed height but sized sideways to its
// contents, which a long mob name or a large number could fill.
TEST(BossFightPanelTest, NoPanelWeldsARowToItsRightBorder) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  EXPECT_TRUE(RowsTouchingTheRightBorder(BossFightPanel(run, true)).empty());
  run.Advance(*state, 30.0);
  EXPECT_TRUE(RowsTouchingTheRightBorder(BossFightPanel(run, true)).empty());
}
}  // namespace
}  // namespace ms
