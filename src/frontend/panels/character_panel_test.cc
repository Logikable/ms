#include "src/frontend/panels/character_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

class CharacterPanelTest : public PanelTest {};

// A stage-1 Warrior carrying `sp` first-job skill points.
CharacterInstance MakeWarrior(std::mt19937& rng, int sp) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[1] = sp;
  return CharacterInstance(rng, std::move(proto));
}

Skill MakeSlashBlast() {
  Skill skill;
  skill.set_name("Slash Blast");
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  return skill;
}

// A stage-2 Spearman with points in both books.
CharacterInstance MakeSpearman(std::mt19937& rng) {
  Character proto;
  proto.set_level(35);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_sp_by_stage())[1] = 3;
  (*proto.mutable_sp_by_stage())[2] = 3;
  return CharacterInstance(rng, std::move(proto));
}

Skill MakeSpearSweep() {
  Skill skill;
  skill.set_name("Spear Sweep");
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(20);
  return skill;
}

Skill MakePowerStrike() {
  Skill skill;
  skill.set_name("Power Strike");
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  return skill;
}

// A one-skill catalog holding the stage-1 Slash Blast.
std::map<std::string, Skill> SkillCatalog() {
  std::map<std::string, Skill> catalog;
  catalog["slash_blast"] = MakeSlashBlast();
  return catalog;
}

// One skill in each of a Spearman's two books.
std::map<std::string, Skill> TwoStageCatalog() {
  std::map<std::string, Skill> catalog = SkillCatalog();  // slash_blast
  catalog["spear_sweep"] = MakeSpearSweep();
  return catalog;
}

// Two stage-1 skills, for a list long enough to walk down.
std::map<std::string, Skill> TwoSkillCatalog() {
  std::map<std::string, Skill> catalog = SkillCatalog();
  catalog["power_strike"] = MakePowerStrike();
  return catalog;
}

// The panel's rows as plain characters, one string per row and one character
// per column. Read off the screen grid because the borders are box-drawing:
// a byte offset into the rendered string does not land where it looks like it
// does.
std::vector<std::string> PanelRows(ftxui::Element element) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(24));
  ftxui::Render(screen, element);
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 1; x < CharacterPanel::kTotalWidth - 1; ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.empty() ? " " : cell;
    }
    rows.push_back(row);
  }
  return rows;
}

std::string Trimmed(const std::string& s) {
  size_t first = s.find_first_not_of(' ');
  if (first == std::string::npos) {
    return "";
  }
  return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

// The value column of the stats row this label opens, or "" if no row has it.
// Reading the two columns by position is the point: it asserts the row is laid
// out in columns, not just that the text is somewhere on the panel.
std::string StatValue(ftxui::Element element, const std::string& label) {
  for (const std::string& row : PanelRows(std::move(element))) {
    if (Trimmed(row.substr(1, 16)) == label) {
      return Trimmed(row.substr(17, 15));
    }
  }
  return "";
}

ftxui::Screen RenderToScreen(ftxui::Component comp) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, comp->Render());
  return screen;
}

// Where `needle` starts on `screen`, or {-1, -1} if it is not there. Walks
// pixels rather than the rendered string because the window border is
// multibyte, so a byte offset is not a column. `needle` must be ASCII: one
// character, one cell.
std::pair<int, int> FindCell(const ftxui::Screen& screen,
                             const std::string& needle) {
  int len = static_cast<int>(needle.size());
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x + len <= screen.dimx(); ++x) {
      std::string got;
      for (int i = 0; i < len; ++i) {
        got += screen.PixelAt(x + i, y).character;
      }
      if (got == needle) {
        return {x, y};
      }
    }
  }
  return {-1, -1};
}

// Where `needle` starts on row `y`, or -1. FindCell answers for the whole
// screen and so only ever finds the topmost row; a claim about two rows
// lining up has to ask each of them separately.
int FindInRow(const ftxui::Screen& screen, int y, const std::string& needle) {
  int len = static_cast<int>(needle.size());
  for (int x = 0; x + len <= screen.dimx(); ++x) {
    std::string got;
    for (int i = 0; i < len; ++i) {
      got += screen.PixelAt(x + i, y).character;
    }
    if (got == needle) {
      return x;
    }
  }
  return -1;
}

// The inverted flag of every cell under `needle`, as a string of '1' and '0' --
// inversion is how the cursor shows itself, and a mask says exactly how far it
// reaches. Returns "" when `needle` is not on screen.
std::string InversionMask(ftxui::Component comp, const std::string& needle) {
  ftxui::Screen screen = RenderToScreen(comp);
  std::pair<int, int> at = FindCell(screen, needle);
  if (at.first < 0) {
    return "";
  }
  std::string mask;
  for (int i = 0; i < static_cast<int>(needle.size()); ++i) {
    mask += screen.PixelAt(at.first + i, at.second).inverted ? '1' : '0';
  }
  return mask;
}

// The dim flag of the cell under the first character of `needle`. Dimming is
// how a row says it cannot be spent on.
bool IsDim(ftxui::Component comp, const std::string& needle) {
  ftxui::Screen screen = RenderToScreen(comp);
  std::pair<int, int> at = FindCell(screen, needle);
  return at.first >= 0 && screen.PixelAt(at.first, at.second).dim;
}

// Whether `needle` is on screen. RenderComponent's string is no use across a
// change of colour: ToString writes escape codes between them, so a coloured
// tag and the name beside it are not adjacent bytes.
bool OnScreen(ftxui::Component comp, const std::string& needle) {
  return FindCell(RenderToScreen(comp), needle).first >= 0;
}

// The foreground colour of the cell under the first character of `needle`.
ftxui::Color ColorOf(ftxui::Component comp, const std::string& needle) {
  ftxui::Screen screen = RenderToScreen(comp);
  std::pair<int, int> at = FindCell(screen, needle);
  if (at.first < 0) {
    return ftxui::Color::Default;
  }
  return screen.PixelAt(at.first, at.second).foreground_color;
}

// Whether the cell under the first character of `needle` is inverted.
bool IsInverted(ftxui::Component comp, const std::string& needle) {
  std::string mask = InversionMask(comp, needle);
  return !mask.empty() && mask[0] == '1';
}

TEST_F(CharacterPanelTest, ShowsLevel) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Lv  1"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsJobName) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Beginner"), std::string::npos);
}

// A level-10 Beginner, standing at the advancement it has not taken.
CharacterInstance MakePendingBeginner(std::mt19937& rng) {
  Character proto;
  proto.set_level(10);
  proto.set_job(JOB_BEGINNER);
  return CharacterInstance(rng, std::move(proto));
}

TEST_F(CharacterPanelTest, HidesTheAdvanceTabWithNothingPending) {
  CharacterPanel panel(c_, panel_focus_);  // c_ is level 1
  EXPECT_EQ(RenderElement(panel.Render()).find("Advance"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsTheAdvanceTabWhenOneIsPending) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Advance"), std::string::npos);
}

// The tab is gone the moment the choice is made, and the cursor cannot be
// left standing on it.
TEST_F(CharacterPanelTest, DropsTheAdvanceTabOnceTheJobIsPicked) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills -> Advance
  ASSERT_NE(RenderComponent(comp).find("Swordman"), std::string::npos);

  c.AdvanceJob(JOB_ROGUE);
  std::string rendered = RenderComponent(comp);
  EXPECT_EQ(rendered.find("Advance"), std::string::npos);
  EXPECT_EQ(rendered.find("Swordman"), std::string::npos);
}

// Taking the advancement rewrites the bar under the cursor: Advance leaves,
// Skills arrives, and the position the cursor was standing on comes to mean
// Skills. The zone it was in belonged to the Advance tab, which left the
// cursor nowhere -- nothing drawn as selected, and arrow keys landing in the
// skill rows rather than on the bar the player was looking at.
TEST_F(CharacterPanelTest, AdvancingLeavesTheCursorOnTheTabBar) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  // A pending Beginner's bar is Stats and Advance: no Skills until they pick.
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Advance
  comp->OnEvent(ftxui::Event::ArrowDown);   // into the job list
  ASSERT_NE(RenderComponent(comp).find("Swordman"), std::string::npos);

  c.AdvanceJob(JOB_SWORDMAN);
  // The bar holds the cursor, so Left walks it back to Stats.
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_NE(RenderComponent(comp).find("STR"), std::string::npos);
}

// And Down from there enters the Skills content, which is what the bar
// holding the cursor means.
TEST_F(CharacterPanelTest, AdvancingLeavesTheSkillsContentOneKeyAway) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Advance
  comp->OnEvent(ftxui::Event::ArrowDown);   // into the job list

  c.AdvanceJob(JOB_SWORDMAN);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> advancement bar
  EXPECT_NE(RenderComponent(comp).find("SP"), std::string::npos);
}

TEST_F(CharacterPanelTest, AdvanceTabListsTheFourJobs) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowRight);
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Swordman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Archer"), std::string::npos);
  EXPECT_NE(rendered.find("Rogue"), std::string::npos);
}

TEST_F(CharacterPanelTest, EnterOnAJobAsksToAdvanceIntoIt) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_);
  Job chosen = JOB_UNSPECIFIED;
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {},
                          [&chosen](Job job) { chosen = job; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills -> Advance
  comp->OnEvent(ftxui::Event::ArrowDown);   // into the job list
  comp->OnEvent(ftxui::Event::ArrowDown);   // Swordman -> Archer
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(chosen, JOB_ARCHER);
  // Asking is all the panel does; performing it belongs to the confirmation.
  EXPECT_EQ(c.proto().job(), JOB_BEGINNER);
}

// The Advance tab's ring, from the other end: Up off the bar lands on the last
// job on offer rather than doing nothing.
TEST_F(CharacterPanelTest, UpFromTheTabBarLandsOnTheLastJob) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_);
  Job chosen = JOB_UNSPECIFIED;
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {},
                          [&chosen](Job job) { chosen = job; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Advance
  comp->OnEvent(ftxui::Event::ArrowUp);     // tab bar -> the last job
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(chosen, JOB_ROGUE) << "the last of the four on offer";
}

TEST_F(CharacterPanelTest, AdvanceTabUpFromTheTopReturnsToTheTabBar) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, panel_focus_);
  Job chosen = JOB_UNSPECIFIED;
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {},
                          [&chosen](Job job) { chosen = job; });
  // A pending Beginner's bar is Stats and Advance: no Skills until they pick.
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Advance
  comp->OnEvent(ftxui::Event::ArrowDown);   // into the job list
  comp->OnEvent(ftxui::Event::ArrowUp);     // back to the tab bar
  comp->OnEvent(ftxui::Event::ArrowLeft);   // which is where Left/Right act
  EXPECT_NE(RenderComponent(comp).find("HP:"),
            std::string::npos);  // the Stats tab
}

// Skills belong to a job. A Beginner has no skill list to look at, so the bar
// is Stats alone until they pick one.
// Named for the level, not the job: at level 1 the tab is held back by the
// level gate whatever the job is, so this says nothing about being a Beginner.
// TheSkillsTabArrivesWithTheAdvancement below is what tests the job condition,
// by standing a Beginner at the gate's level.
TEST_F(CharacterPanelTest, ANewCharacterHasOnlyTheStatsTab) {
  CharacterPanel panel(c_, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Stats"), std::string::npos);
  EXPECT_EQ(rendered.find("Skills"), std::string::npos);
}

TEST_F(CharacterPanelTest, TheSkillsTabArrivesWithTheAdvancement) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  ASSERT_EQ(
      RenderElement(CharacterPanel(c, panel_focus_).Render()).find("Skills"),
      std::string::npos)
      << "level 10 is not enough on its own";

  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_NE(
      RenderElement(CharacterPanel(c, panel_focus_).Render()).find("Skills"),
      std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabIsShownByDefault) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabCountsLearnedPassivesIntoHpAndDef) {
  // Iron Body at level 3: DEF +30, Max HP +3%.
  Skill iron_body;
  iron_body.set_name("Iron Body");
  iron_body.set_kind(SKILL_KIND_PASSIVE);
  iron_body.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  iron_body.set_max_level(20);
  iron_body.mutable_base()->set_def(10);
  iron_body.mutable_base()->set_max_hp_pct(0.01);
  iron_body.mutable_per_level()->set_def(10);
  iron_body.mutable_per_level()->set_max_hp_pct(0.01);
  std::map<std::string, Skill> catalog;
  catalog["iron_body"] = iron_body;

  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_hp(100);
  (*proto.mutable_sp_by_stage())[1] = 3;
  CharacterInstance c(rng_, std::move(proto));
  ASSERT_TRUE(c.LearnSkill(iron_body, 3));

  CharacterPanel panel(c, panel_focus_, catalog);
  EXPECT_NE(RenderElement(panel.Render()).find("HP: 103"), std::string::npos);
  EXPECT_EQ(StatValue(panel.Render(), "Defense"), "30");
}

TEST_F(CharacterPanelTest, ShowsCombatPowerWithThousandsSeparators) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_str(1000);
  CharacterInstance c(rng_, std::move(proto));

  EquipPrototype weapon;
  weapon.set_name("Sword");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.mutable_base_stats()->set_attack(500);
  c.PickUp(std::make_unique<EquipInstance>(weapon));
  c.Equip(0);

  // 4 * 1000 STR * 500 ATT / 100 = 20000, halved toward the mastery floor.
  CharacterPanel panel(c, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Combat Power 11,500"),
            std::string::npos);
}

TEST_F(CharacterPanelTest, CombatPowerShortensItsLabelPastSixFigures) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_str(100000);
  CharacterInstance c(rng_, std::move(proto));

  EquipPrototype weapon;
  weapon.set_name("Sword");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.mutable_base_stats()->set_attack(500);
  c.PickUp(std::make_unique<EquipInstance>(weapon));
  c.Equip(0);

  // Past 999,999 the words go and the number stays.
  CharacterPanel panel(c, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("CP 1,150,0"), std::string::npos);
  EXPECT_EQ(rendered.find("Combat Power"), std::string::npos);
}

TEST_F(CharacterPanelTest, ArrowKeysSwitchTabs) {
  // A Warrior, because a Beginner has only the one tab to sit on.
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel panel(c, panel_focus_);  // panel_focus_ == kCharPanel
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  EXPECT_EQ(RenderComponent(comp).find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowLeft);  // Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabShowsPlusButtons) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("[+]"), std::string::npos);
  EXPECT_EQ(rendered.find("[Max]"), std::string::npos);  // [Max] is gone
}

TEST_F(CharacterPanelTest, ShowsAvailableApInTheMpRow) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("5 AP"), std::string::npos);
}

TEST_F(CharacterPanelTest, DownFromTabBarThenEnterAllocatesStr) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  ftxui::Component comp = panel.MakeComponent([&](StatField f) { field = f; });
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR row
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_STR);
}

TEST_F(CharacterPanelTest, DownMovesTheCursorToTheNextStat) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  ftxui::Component comp = panel.MakeComponent([&](StatField f) { field = f; });
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR
  comp->OnEvent(ftxui::Event::ArrowDown);  // STR -> DEX
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_DEX);
}

TEST_F(CharacterPanelTest, UpFromStrReturnsToTheTabBar) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  bool fired = false;
  ftxui::Component comp = panel.MakeComponent([&](StatField) { fired = true; });
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR
  comp->OnEvent(ftxui::Event::ArrowUp);    // STR -> tab bar
  // Enter allocates only from a stat row, so its silence is what says the
  // cursor left one.
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(fired);
}

// --- the tab bar is the top of every tab's ring ---

// Up off the bar lands on the last row of the tab's content, which for Stats
// is View All Stats. Enter names the row, which is how the test says where the
// cursor is standing.
TEST_F(CharacterPanelTest, UpFromTheTabBarLandsOnViewAllStats) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  bool opened = false;
  ftxui::Component comp = panel.MakeComponent(
      [&](StatField f) { field = f; }, {}, {}, {}, [&] { opened = true; });
  comp->OnEvent(ftxui::Event::ArrowUp);
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
  EXPECT_EQ(field, STAT_FIELD_UNSPECIFIED);
}

TEST_F(CharacterPanelTest, DownFromLukReachesViewAllStatsThenTheTabBar) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  int opened = 0;
  ftxui::Component comp = panel.MakeComponent([&](StatField f) { field = f; },
                                              {}, {}, {}, [&] { ++opened; });
  for (int i = 0; i < 4; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR -> ... -> LUK
  }
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_LUK);

  comp->OnEvent(ftxui::Event::ArrowDown);  // LUK -> View All Stats
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, 1);

  comp->OnEvent(ftxui::Event::ArrowDown);  // View All Stats -> tab bar
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, 1);
}

TEST_F(CharacterPanelTest, ViewAllStatsOpensWithNoApToSpend) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, panel_focus_);
  bool opened = false;
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, {}, {}, {}, [&] { opened = true; });
  comp->OnEvent(ftxui::Event::ArrowUp);  // tab bar -> View All Stats
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
}

TEST_F(CharacterPanelTest, EnterWithoutApDoesNotAllocate) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, panel_focus_);
  bool fired = false;
  ftxui::Component comp = panel.MakeComponent([&](StatField) { fired = true; });
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(fired);
}

TEST_F(CharacterPanelTest, NoApStillEntersTheStatRows) {
  // The rows are worth reading whether or not there is AP to spend, so Down
  // descends regardless. Prove the cursor left the tab bar: Right no longer
  // switches tabs, because Left/Right belong to the bar alone.
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> STR row
  comp->OnEvent(ftxui::Event::ArrowRight);  // on the rows: does nothing
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// Right has nowhere to go on a Beginner's one-tab bar, so the stats stay put
// rather than the cursor landing on a tab that is not drawn.
TEST_F(CharacterPanelTest, RightStaysOnStatsForABeginner) {
  CharacterPanel panel(c_, panel_focus_);  // c_ is a stage-0 Beginner
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, WarriorSkillsTabShowsAdvancementTabAndSp) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);  // job_stage 1
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find(" I "), std::string::npos);  // stage-1 tab
  EXPECT_NE(rendered.find(" SP"), std::string::npos);
}

TEST_F(CharacterPanelTest, SkillsAdvBarUpReturnsToOuterTabs) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // outer tabs: Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // enter the advancement bar
  comp->OnEvent(ftxui::Event::ArrowUp);     // back to the outer tabs
  comp->OnEvent(ftxui::Event::ArrowLeft);   // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, SkillsTabListsTheStagesSkills) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Slash Blast"), std::string::npos);
  EXPECT_NE(rendered.find("0/20"), std::string::npos);
}

// The Skills tab opens on the first book. A character reads their books in
// the order they earned them, and the newest one is a Right away.
TEST_F(CharacterPanelTest, TheSkillsTabOpensOnTheFirstBook) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, panel_focus_, TwoStageCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Slash Blast"), std::string::npos);
  EXPECT_EQ(rendered.find("Spear Sweep"), std::string::npos);
}

// And it stays where the player left it. Down out of the tab bar used to snap
// the advancement bar to the newest stage, so a second-job character could
// never get back down onto their first book.
TEST_F(CharacterPanelTest, TheAdvancementBarKeepsThePageItWasLeftOn) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, panel_focus_, TwoStageCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowRight);  // page I -> page II
  ASSERT_NE(RenderComponent(comp).find("Spear Sweep"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowLeft);  // page II -> page I
  comp->OnEvent(ftxui::Event::ArrowUp);    // back up to the outer tabs
  comp->OnEvent(ftxui::Event::ArrowDown);  // and down onto the bar again
  EXPECT_NE(RenderComponent(comp).find("Slash Blast"), std::string::npos);
}

// The book's own order decides the list, and nothing else does. GMS lists a
// class's skills in an order of its own -- it does not gather the attacks
// above the passives -- so the catalog carries skill_order and the panel
// obeys it. Built here so that stem order, kind order and skill_order all
// disagree, which is the only way to tell which one the list is following.
TEST_F(CharacterPanelTest, TheListFollowsSkillOrderAndNotKind) {
  const char* stems[] = {"a_iron_body", "b_evil_eye", "c_slash_blast"};
  const char* names[] = {"Iron Body", "Evil Eye Shock", "Slash Blast"};
  SkillKind kinds[] = {SKILL_KIND_PASSIVE, SKILL_KIND_AUTO_ATTACK,
                       SKILL_KIND_ATTACK};
  std::map<std::string, Skill> catalog;
  for (int i = 0; i < 3; ++i) {
    Skill skill;
    skill.set_name(names[i]);
    skill.set_kind(kinds[i]);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(20);
    skill.set_skill_order(i + 1);  // the reverse of what KindOrder wanted
    catalog[stems[i]] = skill;
  }

  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  size_t iron = rendered.find("Iron Body");
  size_t eye = rendered.find("Evil Eye Shock");
  size_t slash = rendered.find("Slash Blast");
  ASSERT_NE(iron, std::string::npos);
  ASSERT_NE(eye, std::string::npos);
  ASSERT_NE(slash, std::string::npos);
  EXPECT_LT(iron, eye);
  EXPECT_LT(eye, slash);
}

// A book holding the longest name the game ships, which is four columns wider
// than the column it has to sit in.
std::map<std::string, Skill> WordyCatalog() {
  Skill wordy;
  wordy.set_name("Final Attack: Crossbow");
  wordy.set_kind(SKILL_KIND_PASSIVE);
  wordy.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  wordy.set_max_level(20);
  wordy.set_skill_order(1);
  return {{"wordy", wordy}};
}

// The panel is laid out beside three others at a fixed width, so a name wider
// than its column has to slide inside the column rather than push the border
// out -- which is what "Final Attack: Crossbow" did to the whole main screen.
TEST_F(CharacterPanelTest, ALongSkillNameDoesNotWidenThePanel) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, WordyCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  // The width the panel asks its layout for, which is what a long name used to
  // inflate. Held against a book whose names all fit: the two must agree.
  ftxui::Element wordy = panel.Render();
  CharacterPanel narrow(c, panel_focus_, SkillCatalog());
  ftxui::Component narrow_comp = narrow.MakeComponent([](StatField) {});
  narrow_comp->OnEvent(ftxui::Event::ArrowRight);
  ftxui::Element brief = narrow.Render();

  EXPECT_EQ(ftxui::Dimension::Fit(wordy).dimx,
            ftxui::Dimension::Fit(brief).dimx);
  EXPECT_LE(ftxui::Dimension::Fit(wordy).dimx, CharacterPanel::kTotalWidth);
}

// A row nobody is looking at shows the head of its name and stops. The rest
// arrives by selecting the row; see the marquee's own tests for the slide.
TEST_F(CharacterPanelTest, AnUnselectedLongNameIsCutToItsColumn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, WordyCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  EXPECT_TRUE(OnScreen(comp, "Final Attack: Cro"));
  EXPECT_FALSE(OnScreen(comp, "Final Attack: Cross"));
}

// The name column is a fixed width rather than each name's own, so the levels
// beside them stay a column too -- which is the whole reason the long name is
// cut instead of the row being allowed to grow.
TEST_F(CharacterPanelTest, NamesAndLevelsStayColumnsWhateverTheNameLength) {
  Skill brief;
  brief.set_name("Rush");
  brief.set_kind(SKILL_KIND_PASSIVE);
  brief.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  brief.set_max_level(20);
  brief.set_skill_order(2);
  std::map<std::string, Skill> catalog = WordyCatalog();
  catalog["rush"] = brief;

  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  ftxui::Screen screen = RenderToScreen(comp);
  std::pair<int, int> wordy = FindCell(screen, "Final Attack: Cro");
  std::pair<int, int> rush = FindCell(screen, "Rush");
  ASSERT_GE(wordy.first, 0);
  ASSERT_GE(rush.first, 0);
  ASSERT_NE(wordy.second, rush.second) << "expected two rows, not one";
  EXPECT_EQ(wordy.first, rush.first);
  EXPECT_EQ(FindInRow(screen, wordy.second, "0/20"),
            FindInRow(screen, rush.second, "0/20"));
  // And the level does not run into the [+] beside it.
  EXPECT_GE(FindInRow(screen, wordy.second, "0/20 "), 0);
}

// A catalog with one skill of each kind, plus a kind-less one.
std::map<std::string, Skill> AllKindsCatalog() {
  std::map<std::string, Skill> catalog;
  const char* names[] = {"Slash Blast", "War Leap", "Evil Eye Shock",
                         "Iron Body", "Nameless"};
  SkillKind kinds[] = {SKILL_KIND_ATTACK, SKILL_KIND_ACTIVE,
                       SKILL_KIND_AUTO_ATTACK, SKILL_KIND_PASSIVE,
                       SKILL_KIND_UNSPECIFIED};
  for (int i = 0; i < 5; ++i) {
    Skill skill;
    skill.set_name(names[i]);
    skill.set_kind(kinds[i]);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(20);
    skill.set_skill_order(i + 1);
    catalog[names[i]] = skill;
  }
  return catalog;
}

// The tag says what the player does with a skill without their having to know
// what the name means. Four columns whichever tag it is, so the names below
// still line up; a kind-less skill gets the blanks rather than a wrong tag.
TEST_F(CharacterPanelTest, EachSkillRowOpensWithItsKindTag) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, AllKindsCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  EXPECT_TRUE(OnScreen(comp, "A:  Slash Blast"));
  EXPECT_TRUE(OnScreen(comp, "A:  War Leap"));
  EXPECT_TRUE(OnScreen(comp, "AA: Evil Eye Shock"));
  EXPECT_TRUE(OnScreen(comp, "P:  Iron Body"));
  EXPECT_TRUE(OnScreen(comp, "    Nameless"));

  EXPECT_EQ(ColorOf(comp, "A:  Slash Blast"), kRed);
  EXPECT_EQ(ColorOf(comp, "AA: Evil Eye Shock"), kMutedYellow);
  EXPECT_EQ(ColorOf(comp, "P:  Iron Body"), kGreen);
}

// The tag is a fact about the skill, not a second thing to press: Enter opens
// the skill, so the cursor covers the name and stops there at both ends.
TEST_F(CharacterPanelTest, TheHighlightLeavesTheTagAlone) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, AllKindsCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows

  // The name and nothing else: not the tag before it, and not the padding
  // that holds the column open after it.
  EXPECT_EQ(InversionMask(comp, "A:  Slash Blast  "),
            "000011111111111"
            "00");
}

// A requirement is a condition the player has to be able to act on, so what it
// names has to be above it -- and next to it, so the pair reads as one thing.
TEST_F(CharacterPanelTest, ASkillIsListedUnderTheOneItWaitsOn) {
  Skill hyper_body;
  hyper_body.set_name("Hyper Body");
  hyper_body.set_kind(SKILL_KIND_PASSIVE);
  hyper_body.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  hyper_body.set_max_level(10);
  hyper_body.mutable_required_skill()->set_skill_name("Iron Wall");
  hyper_body.mutable_required_skill()->set_level(3);
  Skill iron_wall;
  iron_wall.set_name("Iron Wall");
  iron_wall.set_kind(SKILL_KIND_PASSIVE);
  iron_wall.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  iron_wall.set_max_level(10);
  // A second link, so the chain is deeper than one hop -- the Cleric's Bless
  // waits on Invincible, which waits on Heal.
  iron_wall.mutable_required_skill()->set_skill_name("Endure");
  iron_wall.mutable_required_skill()->set_level(3);
  Skill endure;
  endure.set_name("Endure");
  endure.set_kind(SKILL_KIND_PASSIVE);
  endure.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  endure.set_max_level(10);
  Skill physical_training;
  physical_training.set_name("Physical Training");
  physical_training.set_kind(SKILL_KIND_PASSIVE);
  physical_training.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  physical_training.set_max_level(5);
  // Stem order puts the gated skill first and something unrelated between the
  // pair, so both halves of the claim have somewhere to fail.
  std::map<std::string, Skill> catalog;
  catalog["a_hyper_body"] = hyper_body;
  catalog["b_physical_training"] = physical_training;
  catalog["c_iron_wall"] = iron_wall;
  catalog["d_endure"] = endure;

  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  size_t endure_at = rendered.find("Endure");
  size_t wall = rendered.find("Iron Wall");
  size_t hyper = rendered.find("Hyper Body");
  size_t training = rendered.find("Physical Training");
  ASSERT_NE(endure_at, std::string::npos);
  ASSERT_NE(wall, std::string::npos);
  ASSERT_NE(hyper, std::string::npos);
  ASSERT_NE(training, std::string::npos);
  EXPECT_LT(endure_at, wall);
  EXPECT_LT(wall, hyper);
  EXPECT_LT(hyper, training);
}

// A requirement naming a skill from another book cannot be ordered around, and
// must not take the skill that carries it out of the list.
TEST_F(CharacterPanelTest, AnOffPageRequirementStillListsItsSkill) {
  Skill gated = MakeSpearSweep();
  gated.mutable_required_skill()->set_skill_name("Slash Blast");
  gated.mutable_required_skill()->set_level(3);
  std::map<std::string, Skill> catalog = TwoStageCatalog();
  catalog["spear_sweep"] = gated;

  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowRight);  // page I -> page II
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Spear Sweep"), std::string::npos);
  EXPECT_EQ(rendered.find("Slash Blast"), std::string::npos);
}

TEST_F(CharacterPanelTest, DownIntoSkillRowsThenEnterFiresLearn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  std::string learned;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&](const Skill& s) { learned = s.name(); });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(learned, "Slash Blast");
}

TEST_F(CharacterPanelTest, DownIntoSkillRowsLandsOnTheNameNotThePlus) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  bool learned = false;
  std::string inspected;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&](const Skill&) { learned = true; }, [](Job) {},
      [&](const Skill& s) { inspected = s.name(); });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(inspected, "Slash Blast");
  EXPECT_FALSE(learned);
}

// The two columns are one Left/Right apart, and the cursor stays where it was
// put -- walking to the [+] and back must not strand the row on the wrong one.
TEST_F(CharacterPanelTest, LeftAndRightWalkBetweenTheTwoColumns) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  bool learned = false;
  bool inspected = false;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&](const Skill&) { learned = true; }, [](Job) {},
      [&](const Skill&) { inspected = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::ArrowLeft);   // [+] -> name
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(inspected);
  EXPECT_FALSE(learned);
}

// The cursor has to be visible on whichever column it is on, and on only that
// one -- two highlights at once would read as two cursors.
TEST_F(CharacterPanelTest, TheHighlightFollowsTheSelectedColumn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows

  EXPECT_TRUE(IsInverted(comp, "Slash Blast"));
  EXPECT_FALSE(IsInverted(comp, "[+]"));

  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  EXPECT_FALSE(IsInverted(comp, "Slash Blast"));
  EXPECT_TRUE(IsInverted(comp, "[+]"));
}

// Enter on the name opens the skill, so the highlight stops at the skill. The
// level beside it is a fact about the row, not a second thing to press.
TEST_F(CharacterPanelTest, TheHighlightStopsAtTheEndOfTheName) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows

  // Eleven for the name, then the padding holding the column open and the
  // level beyond it -- neither of which Enter reaches.
  EXPECT_EQ(InversionMask(comp, "Slash Blast       0/20"),
            "11111111111"
            "00000000000");
}

// A maxed skill with no SP behind it dims its [+], but the name is still a
// live target -- the description is the whole reason to look at it.
TEST_F(CharacterPanelTest, InspectIsNotGatedBySpOrMaxLevel) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_skill_levels())["Slash Blast"] = 20;  // maxed, and 0 SP
  CharacterInstance c(rng_, std::move(proto));
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  std::string inspected;
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {}, [](Job) {},
                          [&](const Skill& s) { inspected = s.name(); });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(inspected, "Slash Blast");
}

TEST_F(CharacterPanelTest, NoSpEntersTheSkillRowsButEnterDoesNothing) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  bool fired = false;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&](const Skill&) { fired = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows, SP or not
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::Return);      // nothing to spend
  EXPECT_FALSE(fired);
  // The cursor is on the rows, not the bar: Left/Right pick the column here,
  // so an Up is what it takes to get back and switch tabs.
  comp->OnEvent(ftxui::Event::ArrowUp);    // rows -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowUp);    // advancement bar -> outer tabs
  comp->OnEvent(ftxui::Event::ArrowLeft);  // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, EnterOnAMaxedSkillDoesNotFireLearn) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[1] = 5;
  (*proto.mutable_skill_levels())["Slash Blast"] = 20;  // already maxed
  CharacterInstance c(rng_, std::move(proto));
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  bool fired = false;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&](const Skill&) { fired = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows (has SP, so entered)
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  EXPECT_NE(RenderComponent(comp).find("20/20"), std::string::npos);
  comp->OnEvent(ftxui::Event::Return);  // maxed: nothing to learn
  EXPECT_FALSE(fired);
}

TEST_F(CharacterPanelTest, UpFromSkillRowsReturnsToTheAdvancementBar) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows
  comp->OnEvent(ftxui::Event::ArrowUp);     // back to the advancement bar
  comp->OnEvent(ftxui::Event::ArrowUp);     // advancement bar -> outer tabs
  comp->OnEvent(ftxui::Event::ArrowLeft);   // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// The Skills tab's ring is three deep: the outer tab bar, the advancement bar
// under it, then the skills. Up off the top of it arrives at the bottom.
TEST_F(CharacterPanelTest, UpFromTheTabBarLandsOnTheLastSkill) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  bool fired = false;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&fired](const Skill&) { fired = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowUp);     // tab bar -> the last skill row
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(fired) << "Enter learns only from a skill row";
}

TEST_F(CharacterPanelTest, DownFromTheLastSkillReturnsToTheBar) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // the one skill row
  comp->OnEvent(ftxui::Event::ArrowDown);   // off the bottom -> outer tab bar
  // Left switches outer tabs only from the bar, so Stats coming back is where
  // the cursor went.
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// A stage with no skills is a ring of two: the outer bar and the advancement
// bar. Down from the advancement bar carries on round rather than descending
// into a list that is not there.
TEST_F(CharacterPanelTest, DownFromTheAdvBarSkipsAnEmptySkillList) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);
  CharacterPanel panel(c, panel_focus_);  // no catalog: no skills at all
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // -> outer tab bar, not a row
  comp->OnEvent(ftxui::Event::ArrowLeft);   // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// Walking from one skill row to the next keeps whichever column the cursor was
// in. Only arriving from outside the rows puts it back on the name, so a player
// spending SP down a list does not have to re-cross to the [+] on every row.
TEST_F(CharacterPanelTest, WalkingBetweenSkillRowsKeepsTheColumn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, TwoSkillCatalog());
  bool learned = false;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&learned](const Skill&) { learned = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // first skill row, on the name
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::ArrowDown);   // second row, still on the [+]
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(learned) << "Enter over the name would inspect, not learn";
}

TEST_F(CharacterPanelTest, SpendingTheLastSpLeavesTheCursorOnTheRows) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/1);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  bool fired = false;
  ftxui::Component comp = panel.MakeComponent(
      [](StatField) {}, [&](const Skill&) { fired = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows (SP == 1)
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  c.LearnSkill(MakeSlashBlast(), 1);        // drains stage-1 SP to 0
  comp->OnEvent(ftxui::Event::Return);      // no SP left: nothing to learn
  EXPECT_FALSE(fired);
  // The cursor stayed put -- one Up reaches the bar, not the outer tabs.
  comp->OnEvent(ftxui::Event::ArrowUp);    // rows -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowLeft);  // on the bar: only one stage
  EXPECT_NE(RenderComponent(comp).find(" I "), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsEquipAttackFromEquippedItem) {
  sword_.mutable_base_stats()->set_attack(10);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_EQ(StatValue(panel.Render(), "Attack"), "10");
}

TEST_F(CharacterPanelTest, ShowsStrWithBreakdownWhenGearContributes) {
  sword_.mutable_base_stats()->set_str(5);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  CharacterPanel panel(c_, panel_focus_);
  // base AP STR is 0 for the test character; gear adds 5; total = 5.
  EXPECT_NE(RenderElement(panel.Render()).find("STR: 5 (0+5)"),
            std::string::npos);
}

// A passive granting one level's worth of every damage lever the stats tab
// reports, plus two stages of attack speed.
Skill MakeLeverPassive() {
  Skill skill;
  skill.set_name("Levers");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.mutable_base()->set_damage_pct(0.075);
  skill.mutable_base()->set_final_dmg_pct(0.05);
  skill.mutable_base()->set_crit_rate(0.2);
  skill.mutable_base()->set_crit_dmg(0.025);
  skill.mutable_base()->set_attack_speed(2);
  return skill;
}

// The labels of the rows below the AP stats, in order: the extra stats the
// budget left standing, and the View All Stats row under them. Rules and blank
// rows are not labels; a rule's first character is box-drawing, not ASCII.
std::vector<std::string> ExtrasShown(ftxui::Element element) {
  std::vector<std::string> labels;
  bool past_the_stats = false;
  for (const std::string& row : PanelRows(std::move(element))) {
    std::string text = Trimmed(row);
    if (text.compare(0, 4, "LUK:") == 0) {
      past_the_stats = true;
      continue;
    }
    if (!past_the_stats || text.empty() ||
        static_cast<unsigned char>(text[0]) > 127) {
      continue;
    }
    // A stat row is a label, a run of spaces, then its value. The centred row
    // has no run of spaces in it at all.
    size_t gap = text.find("  ");
    labels.push_back(gap == std::string::npos ? text : text.substr(0, gap));
  }
  return labels;
}

TEST_F(CharacterPanelTest, ARowBudgetDropsTheLeastImportantStatsFirst) {
  CharacterPanel panel(c_, panel_focus_);
  // 14 rows of chrome and stats above, then four rows for the extras: three
  // stats and the row that leads to the rest of them.
  panel.SetMaxRows(18);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"Attack", "Magic Attack", "Damage",
                                      "View All Stats"}));
}

TEST_F(CharacterPanelTest, TheViewAllStatsRowIsTheLastToGo) {
  CharacterPanel panel(c_, panel_focus_);
  // Room for nothing but the way out, and then for less than that.
  panel.SetMaxRows(15);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"View All Stats"}));
  panel.SetMaxRows(4);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"View All Stats"}));
}

TEST_F(CharacterPanelTest, NoBudgetShowsEveryStat) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_EQ(ExtrasShown(panel.Render()).size(), 9u);  // 8 stats and the row
}

TEST_F(CharacterPanelTest, ShowsTheDamageLeversAsPercentages) {
  Skill levers = MakeLeverPassive();
  std::map<std::string, Skill> catalog;
  catalog["levers"] = levers;
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/1);
  ASSERT_TRUE(c.LearnSkill(levers, 1));

  CharacterPanel panel(c, panel_focus_, catalog);
  EXPECT_EQ(StatValue(panel.Render(), "Damage"), "7.50%");
  EXPECT_EQ(StatValue(panel.Render(), "Final Damage"), "5.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Critical Rate"), "20.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Critical Damage"), "2.50%");
}

TEST_F(CharacterPanelTest, TheLeversReadZeroWithNoSkillsBehindThem) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_EQ(StatValue(panel.Render(), "Damage"), "0.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Critical Rate"), "0.00%");
}

TEST_F(CharacterPanelTest, AttackSpeedNamesTheStageTheWeaponIsSwungAt) {
  sword_.set_attack_speed(ATTACK_SPEED_AVERAGE);
  Skill levers = MakeLeverPassive();  // +2 stages
  std::map<std::string, Skill> catalog;
  catalog["levers"] = levers;
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/1);
  ASSERT_TRUE(c.LearnSkill(levers, 1));

  CharacterPanel panel(c, panel_focus_, catalog);
  EXPECT_EQ(StatValue(panel.Render(), "Attack Speed"), "-");

  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  EXPECT_EQ(StatValue(panel.Render(), "Attack Speed"), "Fast 2");
}

TEST_F(CharacterPanelTest, AttackSpeedStopsAtTheFastestStage) {
  sword_.set_attack_speed(ATTACK_SPEED_FASTEST_3);
  Skill levers = MakeLeverPassive();  // +2 stages, with nowhere to go
  std::map<std::string, Skill> catalog;
  catalog["levers"] = levers;
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/1);
  ASSERT_TRUE(c.LearnSkill(levers, 1));
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);

  CharacterPanel panel(c, panel_focus_, catalog);
  EXPECT_EQ(StatValue(panel.Render(), "Attack Speed"), "Fastest 3");
}

TEST_F(CharacterPanelTest, StressTestStatRowWidth) {
  // Exercises the widest realistic stat strings to verify kContentWidth holds.
  // " LUK: 999999 (1300+998699)" is the longest at 26 chars.
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  proto.mutable_allocated_stats()->set_str(4);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(1300);
  CharacterInstance c(rng_, std::move(proto));
  EquipPrototype gear;
  gear.set_name("StressTest");
  gear.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  gear.mutable_base_stats()->set_str(995);
  gear.mutable_base_stats()->set_dex(9995);
  gear.mutable_base_stats()->set_int_(99995);
  gear.mutable_base_stats()->set_luk(998699);
  c.PickUp(std::make_unique<EquipInstance>(gear));
  c.Equip(0);
  CharacterPanel panel(c, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("STR: 999 (4+995)"), std::string::npos);
  EXPECT_NE(rendered.find("DEX: 9999 (4+9995)"), std::string::npos);
  EXPECT_NE(rendered.find("INT: 99999 (4+99995)"), std::string::npos);
  EXPECT_NE(rendered.find("LUK: 999999 (1300+998699)"), std::string::npos);
}

// --- highlighting ---

// The card across the screen says what happened; the gold border is
// what says where to look for it. This panel is where the AP a level paid out
// is spent, so it is the one lit on every level-up.
TEST_F(CharacterPanelTest, LightsItsBorderGoldWhenHighlighted) {
  CharacterPanel panel(c_, panel_focus_);
  ASSERT_EQ(BorderColor(panel.Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(panel.Render()), kYellow);
}

// The panel keeps no clock: whoever lit it is the one that puts it out, and
// the border has to actually go back.
TEST_F(CharacterPanelTest, ClearingTheHighlightRestoresTheTheme) {
  CharacterPanel panel(c_, panel_focus_);
  panel.SetHighlighted(true);
  panel.SetHighlighted(false);
  EXPECT_EQ(BorderColor(panel.Render()), kTheme);
}

// This panel is the one with rules through the middle of it -- under the
// title, under the tab bar, and between the allocated stats and the derived
// ones. A gold box around steel-blue seams is not a lit panel.
TEST_F(CharacterPanelTest, LightsItsInnerRulesGoldToo) {
  CharacterPanel panel(c_, panel_focus_);
  ASSERT_EQ(InnerRuleColor(panel.Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(InnerRuleColor(panel.Render()), kYellow);
  panel.SetHighlighted(false);
  EXPECT_EQ(InnerRuleColor(panel.Render()), kTheme);
}

// --- a newly unlocked tab announces itself ---
//
// These read the chip colour with the panel unfocused. A focused, active chip
// is drawn black on white -- correct, and nothing to do with whether the tab
// is new -- so leaving focus here would test the wrong thing.

// The Skills tab is new, and still says nothing. Advancing swaps it in at the
// exact index the Advance tab vacates, so the player is left standing on it --
// gold on a tab they are already reading announces nothing, and having never
// been arrowed onto, nothing would clear it either.
TEST_F(CharacterPanelTest, AdvancingLeavesTheSkillsTabUngilded) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  ASSERT_TRUE(c.CanAdvanceJob());
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component component = panel.MakeComponent([](StatField) {});
  panel_focus_ = kCharPanel;
  component->OnEvent(ftxui::Event::ArrowRight);  // onto Advance
  ASSERT_NE(RenderComponent(component).find("Advance"), std::string::npos);

  c.AdvanceJob(JOB_SWORDMAN);
  ASSERT_EQ(RenderComponent(component).find("Advance"), std::string::npos)
      << "the tab is spent, so Skills has taken its place on the bar";

  // Read unfocused from here: a focused active chip is drawn black on white
  // rather than inverted, and gold on white rather than gold.
  panel_focus_ = kInventoryPanel;
  EXPECT_TRUE(IsInverted(component, "Skills"))
      << "the cursor did not move, so Skills is the tab now under it";
  EXPECT_EQ(LabelColor(panel.Render(), "Skills"), kTheme)
      << "so there is nothing for gold to tell them";
}

// The Advance tab is not a Feature and not permanent -- it appears at the
// threshold and is gone once a job is picked -- so it gets the same treatment
// from its own path through TabKey.
TEST_F(CharacterPanelTest, ANewAdvanceTabIsWrittenInGold) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  ASSERT_TRUE(c.CanAdvanceJob()) << "the tab has to be on the bar at all";
  CharacterPanel panel(c, panel_focus_);
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Advance"), kYellow);
}

TEST_F(CharacterPanelTest, OpeningTheAdvanceTabClearsItsGold) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component component = panel.MakeComponent([](StatField) {});
  panel_focus_ = kCharPanel;
  component->OnEvent(ftxui::Event::ArrowRight);

  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Advance"), kTheme);
  EXPECT_TRUE(c.TabSeen(AdvanceTabKey(1)))
      << "recorded against the stage being advanced into, so the next "
         "advancement is news again";
}

// --- skills waiting on another skill ---

// Hyper Body's shape: three points in Iron Wall come first.
Skill MakeGatedSkill() {
  Skill skill;
  skill.set_name("Hyper Body");
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_required_skill()->set_skill_name("Slash Blast");
  skill.mutable_required_skill()->set_level(3);
  return skill;
}

std::map<std::string, Skill> GatedCatalog() {
  std::map<std::string, Skill> catalog;
  catalog["hyper_body"] = MakeGatedSkill();
  return catalog;
}

// A skill the character cannot buy yet is not a skill they have. The whole row
// goes dim, not only the [+], because what is missing is the skill rather than
// the points.
TEST_F(CharacterPanelTest, ASkillWaitingOnAnotherDimsItsWholeRow) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/20);
  CharacterPanel panel(c, panel_focus_, GatedCatalog());
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  EXPECT_TRUE(IsDim(comp, "Hyper Body"));
  EXPECT_TRUE(IsDim(comp, "[+]"));
}

TEST_F(CharacterPanelTest, MeetingTheRequirementUndimsTheRow) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[1] = 20;
  (*proto.mutable_skill_levels())["Slash Blast"] = 3;
  CharacterInstance c(rng_, std::move(proto));
  CharacterPanel panel(c, panel_focus_, GatedCatalog());
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  EXPECT_FALSE(IsDim(comp, "Hyper Body"));
  EXPECT_FALSE(IsDim(comp, "[+]"));
}

// An ordinary skill with SP behind it must not be dimmed by the check.
TEST_F(CharacterPanelTest, ASkillDemandingNothingIsNotDimmed) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/20);
  CharacterPanel panel(c, panel_focus_, SkillCatalog());
  ftxui::Component comp =
      panel.MakeComponent([](StatField) {}, [](const Skill&) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  EXPECT_FALSE(IsDim(comp, "Slash Blast"));
}

}  // namespace
}  // namespace ms
