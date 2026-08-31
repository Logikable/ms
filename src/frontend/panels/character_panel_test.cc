#include "src/frontend/panels/character_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/dom/requirement.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panel_widths.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

class CharacterPanelTest : public PanelTest {};

// A stage-1 Warrior carrying `sp` first-job skill points and `ap` to spend.
// The combat stats are gated on the advancement, so a test that wants to read
// them starts from a character who has taken one rather than from c_.
CharacterInstance MakeWarrior(std::mt19937& rng, int sp, int ap = 0) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.set_ap(ap);
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

// A Dark Knight at `level` with a point of Hyper SP and nothing else.
CharacterInstance MakeDarkKnight(std::mt19937& rng, int level) {
  Character proto;
  proto.set_level(level);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(4);
  proto.set_hyper_sp(1);
  return CharacterInstance(rng, std::move(proto));
}

// A Dark Knight's 4th book and one Hyper Skill over it, opening at 150.
std::map<std::string, Skill> HyperCatalog() {
  std::map<std::string, Skill> catalog;
  Skill impale;
  impale.set_name("Dark Impale");
  impale.set_kind(SKILL_KIND_ATTACK);
  impale.set_job_advancement(JOB_ADVANCEMENT_DARK_KNIGHT);
  impale.set_max_level(30);
  catalog["dark_impale"] = impale;

  Skill reinforce;
  reinforce.set_name("Gungnir's Reinforce");
  reinforce.set_job_advancement(JOB_ADVANCEMENT_DARK_KNIGHT);
  reinforce.set_max_level(1);
  reinforce.set_hyper(true);
  reinforce.set_required_level(150);
  catalog["gungnirs_reinforce"] = reinforce;

  // A second one further up the ladder, for the rows a level still holds shut.
  Skill guardbreak = reinforce;
  guardbreak.set_name("Gungnir's Guardbreak");
  guardbreak.set_required_level(165);
  catalog["gungnirs_guardbreak"] = guardbreak;
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
  // Taller than any panel this draws. A screen only as tall as a real terminal
  // clips the tail, which reads as a stat the panel chose to drop -- and the
  // budget tests set their own limit anyway.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(40));
  ftxui::Render(screen, element);
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 1; x < kLeftColumnMin - 1; ++x) {
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

// `rows` is the screen's height, which a panel taller than that is cut to:
// the Hyper tab's fourteen stats put it past the twenty most of these want.
ftxui::Screen RenderToScreen(ftxui::Component comp, int rows = 20) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(rows));
  ftxui::Render(screen, comp->Render());
  return screen;
}

// A rendered screen as plain characters, one per column, a newline a row.
std::string TextOf(const ftxui::Screen& screen) {
  std::string out;
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      out += cell.empty() ? " " : cell;
    }
    out += '\n';
  }
  return out;
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

// Whether any cell of `screen` is exactly `glyph`. FindCell walks a needle a
// byte a cell, so it can only look for ASCII; this is for the one-cell
// multibyte marks -- the arrow a tab bar puts where it runs off its edge.
bool HasCell(const ftxui::Screen& screen, const std::string& glyph) {
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      if (screen.PixelAt(x, y).character == glyph) {
        return true;
      }
    }
  }
  return false;
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
bool IsDim(ftxui::Component comp, const std::string& needle, int rows = 20) {
  ftxui::Screen screen = RenderToScreen(comp, rows);
  std::pair<int, int> at = FindCell(screen, needle);
  return at.first >= 0 && screen.PixelAt(at.first, at.second).dim;
}

// Whether `needle` is on screen. RenderComponent's string is no use across a
// change of colour: ToString writes escape codes between them, so a coloured
// tag and the name beside it are not adjacent bytes.
// The rightmost painted column of row `y`, the window's own border aside.
// Two rows whose right-aligned cells line up end on the same one.
int RowEnd(const ftxui::Screen& screen, int y) {
  for (int x = screen.dimx() - 2; x > 0; --x) {
    const std::string& cell = screen.PixelAt(x, y).character;
    if (!cell.empty() && cell != " ") {
      return x;
    }
  }
  return -1;
}

// A panel rendered onto a screen its own width, so its border lands where
// the main layout would put it rather than at the edge of the test screen.
ftxui::Screen PanelScreen(const CharacterPanel& panel, int width) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Element card = panel.Render();
  ftxui::Render(screen, card);
  return screen;
}

// The same, for the row `needle` is on.
int RowEndOf(const ftxui::Screen& screen, const std::string& needle) {
  return RowEnd(screen, FindCell(screen, needle).second);
}

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

TEST_F(CharacterPanelTest, ShowsTheLevelAndJob) {
  CharacterPanel panel(c_, account_, panel_focus_);
  std::string drawn = RenderElement(panel.Render());
  EXPECT_NE(drawn.find("Lv  1"), std::string::npos);
  EXPECT_NE(drawn.find("Beginner"), std::string::npos);
}

// A level-10 Beginner, standing at the advancement it has not taken.
CharacterInstance MakePendingBeginner(std::mt19937& rng) {
  Character proto;
  proto.set_level(10);
  proto.set_job(JOB_BEGINNER);
  return CharacterInstance(rng, std::move(proto));
}

TEST_F(CharacterPanelTest, TheAdvanceTabAppearsOnlyWithOnePending) {
  CharacterPanel level_one(c_, account_, panel_focus_);  // c_ is level 1
  EXPECT_EQ(RenderElement(level_one.Render()).find("Advance"),
            std::string::npos);

  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel pending(c, account_, panel_focus_);
  EXPECT_NE(RenderElement(pending.Render()).find("Advance"), std::string::npos);
}

// The tab is gone the moment the choice is made, and the cursor cannot be
// left standing on it.
TEST_F(CharacterPanelTest, DropsTheAdvanceTabOnceTheJobIsPicked) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Advance
  comp->OnEvent(ftxui::Event::ArrowDown);   // into the job list

  c.AdvanceJob(JOB_SWORDMAN);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> advancement bar
  EXPECT_NE(RenderComponent(comp).find("SP"), std::string::npos);
}

TEST_F(CharacterPanelTest, AdvanceTabListsTheFourJobs) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_);
  Job chosen = JOB_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.advance = [&chosen](Job job) { chosen = job; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterPanel panel(c, account_, panel_focus_);
  Job chosen = JOB_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.advance = [&chosen](Job job) { chosen = job; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Advance
  comp->OnEvent(ftxui::Event::ArrowUp);     // tab bar -> the username row
  comp->OnEvent(ftxui::Event::ArrowUp);     // username -> the last job
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(chosen, JOB_ROGUE) << "the last of the four on offer";
}

TEST_F(CharacterPanelTest, AdvanceTabUpFromTheTopReturnsToTheTabBar) {
  CharacterInstance c = MakePendingBeginner(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  Job chosen = JOB_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.advance = [&chosen](Job job) { chosen = job; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Stats"), std::string::npos);
  EXPECT_EQ(rendered.find("Skills"), std::string::npos);
}

TEST_F(CharacterPanelTest, TheSkillsTabArrivesWithTheAdvancement) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  ASSERT_EQ(RenderElement(CharacterPanel(c, account_, panel_focus_).Render())
                .find("Skills"),
            std::string::npos)
      << "level 10 is not enough on its own";

  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_NE(RenderElement(CharacterPanel(c, account_, panel_focus_).Render())
                .find("Skills"),
            std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabIsShownByDefault) {
  CharacterPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabCountsLearnedPassivesIntoHp) {
  // Iron Body at level 3: Max HP +3%.
  Skill iron_body;
  iron_body.set_name("Iron Body");
  iron_body.set_kind(SKILL_KIND_PASSIVE);
  iron_body.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  iron_body.set_max_level(20);
  iron_body.mutable_base()->set_max_hp_pct(0.01);
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

  CharacterPanel panel(c, account_, panel_focus_, catalog);
  EXPECT_NE(RenderElement(panel.Render()).find("HP: 103"), std::string::npos);
}

// Attack is written "(base+bonus) total", so it can outgrow its value column.
// When it did, it ran into the gutter and took the row -- and the panel --
// wider than every other row.
TEST_F(CharacterPanelTest, ALongValueKeepsTheRowWidth) {
  Skill marks;
  marks.set_name("Marksmanship");
  marks.set_kind(SKILL_KIND_PASSIVE);
  marks.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  marks.set_max_level(1);
  marks.mutable_base()->set_attack_pct(1.0);
  std::map<std::string, Skill> catalog = {{"marksmanship", marks}};

  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[1] = 1;
  CharacterInstance c(rng_, std::move(proto));
  ASSERT_TRUE(c.LearnSkill(marks, 1));

  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.mutable_base_stats()->set_attack(123456);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.Equip(0);

  CharacterPanel panel(c, account_, panel_focus_, catalog);
  std::vector<std::string> rows = PanelRows(panel.Render());
  std::string attack;
  std::string other;
  for (const std::string& row : rows) {
    if (row.find("(123456+123456) 246912") != std::string::npos) {
      attack = row;
    }
    if (row.find("Attack Speed") != std::string::npos) {
      other = row;
    }
  }
  ASSERT_FALSE(attack.empty()) << "the Attack row did not render as expected";
  ASSERT_FALSE(other.empty());
  EXPECT_EQ(attack.find_last_not_of(' '), other.find_last_not_of(' '))
      << "[" << attack << "] against [" << other << "]";
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
  CharacterPanel panel(c, account_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Combat Power 12,210"),
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
  CharacterPanel panel(c, account_, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("CP 1,221,0"), std::string::npos);
  EXPECT_EQ(rendered.find("Combat Power"), std::string::npos);
}

TEST_F(CharacterPanelTest, ArrowKeysSwitchTabs) {
  // A Warrior, because a Beginner has only the one tab to sit on.
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel panel(c, account_,
                       panel_focus_);  // panel_focus_ == kCharPanel
  ftxui::Component comp = panel.MakeComponent();
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  EXPECT_EQ(RenderComponent(comp).find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowLeft);  // Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabShowsPlusButtons) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("[+]"), std::string::npos);
  EXPECT_EQ(rendered.find("[Max]"), std::string::npos);  // [Max] is gone
}

TEST_F(CharacterPanelTest, ShowsAvailableApInTheMpRow) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("5 AP"), std::string::npos);
}

TEST_F(CharacterPanelTest, DownFromTabBarThenEnterAllocatesStr) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField f) { field = f; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR row
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_STR);
}

TEST_F(CharacterPanelTest, DownMovesTheCursorToTheNextStat) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField f) { field = f; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR
  comp->OnEvent(ftxui::Event::ArrowDown);  // STR -> DEX
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_DEX);
}

TEST_F(CharacterPanelTest, UpFromStrReturnsToTheTabBar) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  bool fired = false;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField) { fired = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  bool opened = false;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField f) { field = f; };
  actions.all_stats = [&] { opened = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the username row
  comp->OnEvent(ftxui::Event::ArrowUp);  // username -> View All Stats
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
  EXPECT_EQ(field, STAT_FIELD_UNSPECIFIED);
}

TEST_F(CharacterPanelTest, DownFromLukReachesViewAllStatsThenTheTabBar) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  int opened = 0;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField f) { field = f; };
  actions.all_stats = [&] { ++opened; };
  ftxui::Component comp = panel.MakeComponent(actions);
  for (int i = 0; i < 4; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR -> ... -> LUK
  }
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_LUK);

  comp->OnEvent(ftxui::Event::ArrowDown);  // LUK -> View All Stats
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, 1);

  comp->OnEvent(ftxui::Event::ArrowDown);  // View All Stats -> the username
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, 1);
}

TEST_F(CharacterPanelTest, ViewAllStatsOpensWithNoApToSpend) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  bool opened = false;
  CharacterPanelActions actions;
  actions.all_stats = [&] { opened = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the username row
  comp->OnEvent(ftxui::Event::ArrowUp);  // username -> View All Stats
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
}

TEST_F(CharacterPanelTest, EnterWithoutApDoesNotAllocate) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  bool fired = false;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField) { fired = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(fired);
}

TEST_F(CharacterPanelTest, NoApStillEntersTheStatRows) {
  // The rows are worth reading whether or not there is AP to spend, so Down
  // descends regardless. Prove the cursor left the tab bar: Right no longer
  // switches tabs, because Left/Right belong to the bar alone.
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> STR row
  comp->OnEvent(ftxui::Event::ArrowRight);  // on the rows: does nothing
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// Right has nowhere to go on a Beginner's one-tab bar, so the stats stay put
// rather than the cursor landing on a tab that is not drawn.
TEST_F(CharacterPanelTest, RightStaysOnStatsForABeginner) {
  CharacterPanel panel(c_, account_, panel_focus_);  // c_ is a stage-0 Beginner
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, WarriorSkillsTabShowsAdvancementTabAndSp) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);  // job_stage 1
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find(" I "), std::string::npos);  // stage-1 tab
  EXPECT_NE(rendered.find(" SP"), std::string::npos);
}

TEST_F(CharacterPanelTest, SkillsAdvBarUpReturnsToOuterTabs) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // outer tabs: Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // enter the advancement bar
  comp->OnEvent(ftxui::Event::ArrowUp);     // back to the outer tabs
  comp->OnEvent(ftxui::Event::ArrowLeft);   // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, SkillsTabListsTheStagesSkills) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Slash Blast"), std::string::npos);
  // The level the player has it at, and no maximum beside it: the [+] going
  // dim is what says a skill is finished.
  EXPECT_NE(rendered.find("Slash Blast           0"), std::string::npos);
  EXPECT_EQ(rendered.find("0/20"), std::string::npos);
}

// The Skills tab opens on the first book. A character reads their books in
// the order they earned them, and the newest one is a Right away.
TEST_F(CharacterPanelTest, TheSkillsTabOpensOnTheFirstBook) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_, TwoStageCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Slash Blast"), std::string::npos);
  EXPECT_EQ(rendered.find("Spear Sweep"), std::string::npos);
}

// --- the Hyper page ---

// The H chip sits after the numerals, and the page behind it holds the Hyper
// Skills rather than the 4th book they upgrade.
TEST_F(CharacterPanelTest, TheHyperPageComesAfterTheAdvancements) {
  CharacterInstance c = MakeDarkKnight(rng_, /*level=*/150);
  CharacterPanel panel(c, account_, panel_focus_, HyperCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> page bar
  EXPECT_NE(RenderComponent(comp).find(" H "), std::string::npos);
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);  // page I -> IV
  }
  std::string book = RenderComponent(comp);
  EXPECT_NE(book.find("Dark Impale"), std::string::npos);
  EXPECT_EQ(book.find("Gungnir's Reinforce"), std::string::npos)
      << "a hyper is not listed among the book it upgrades";

  comp->OnEvent(ftxui::Event::ArrowRight);  // IV -> H
  std::string hyper = RenderComponent(comp);
  EXPECT_NE(hyper.find("Gungnir's Reinforce"), std::string::npos);
  EXPECT_EQ(hyper.find("Dark Impale"), std::string::npos);
  EXPECT_NE(hyper.find("1 SP"), std::string::npos) << "the Hyper pool";
}

// A hyper above the character's level is on the page but shut: the [+] does
// nothing, and the point stays in the pool.
TEST_F(CharacterPanelTest, AHyperAboveItsLevelCannotBeBought) {
  CharacterInstance c = MakeDarkKnight(rng_, /*level=*/150);
  CharacterPanel panel(c, account_, panel_focus_, HyperCatalog());
  bool learned = false;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill&) { learned = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> page bar
  for (int i = 0; i < 4; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);  // page I -> H
  }
  comp->OnEvent(ftxui::Event::ArrowDown);   // page bar -> skill rows
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  // Guardbreak is listed second: skill_order is unset on both, so they fall in
  // catalog order and "gungnirs_guardbreak" sorts first.
  ASSERT_NE(RenderComponent(comp).find("Gungnir's Guardbreak"),
            std::string::npos);
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(learned) << "level 165 is still ahead of them";
  // The one they have reached still buys.
  comp->OnEvent(ftxui::Event::ArrowDown);
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(learned);
}

// Nothing on it is within reach yet, so there is no chip: a page that could
// only ever list what the player cannot have says less than no page.
TEST_F(CharacterPanelTest, TheHyperPageWaitsForTheFirstSkillOnIt) {
  CharacterInstance c = MakeDarkKnight(rng_, /*level=*/149);
  CharacterPanel panel(c, account_, panel_focus_, HyperCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> page bar
  EXPECT_EQ(RenderComponent(comp).find(" H "), std::string::npos);
  for (int i = 0; i < 6; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);  // and Right cannot reach one
  }
  EXPECT_EQ(RenderComponent(comp).find("Gungnir's Reinforce"),
            std::string::npos);
}

// And it stays where the player left it. Down out of the tab bar used to snap
// the advancement bar to the newest stage, so a second-job character could
// never get back down onto their first book.
TEST_F(CharacterPanelTest, TheAdvancementBarKeepsThePageItWasLeftOn) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_, TwoStageCatalog());
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, WordyCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  // The width the panel asks its layout for, which is what a long name used to
  // inflate. Held against a book whose names all fit: the two must agree.
  ftxui::Element wordy = panel.Render();
  CharacterPanel narrow(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component narrow_comp = narrow.MakeComponent();
  narrow_comp->OnEvent(ftxui::Event::ArrowRight);
  ftxui::Element brief = narrow.Render();

  EXPECT_EQ(ftxui::Dimension::Fit(wordy).dimx,
            ftxui::Dimension::Fit(brief).dimx);
  EXPECT_LE(ftxui::Dimension::Fit(wordy).dimx, kLeftColumnMin);
}

// A row nobody is looking at shows the head of its name and stops. The rest
// arrives by selecting the row; see the marquee's own tests for the slide.
TEST_F(CharacterPanelTest, AnUnselectedLongNameIsCutToItsColumn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, WordyCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  EXPECT_TRUE(OnScreen(comp, "Final Attack: Crossbo"));
  EXPECT_FALSE(OnScreen(comp, "Final Attack: Crossbow"));
}

// ...and on a terminal with room for it, the same name is whole: the column a
// wide screen buys goes to the names, which is what it was bought for.
TEST_F(CharacterPanelTest, AWideColumnHoldsTheWholeName) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, WordyCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  panel.SetWidth(kLeftColumnMax);

  EXPECT_TRUE(OnScreen(comp, "Final Attack: Crossbow"));
  ftxui::Element card = panel.Render();
  EXPECT_EQ(ftxui::Dimension::Fit(card).dimx, kLeftColumnMax)
      << "and the panel takes the width it was given, no more";
}

// The Stats tab does not spread with the panel -- a value chasing the border
// would leave its own label at the other end of the row -- so the room a wide
// panel brings sits blank either side of the block.
TEST_F(CharacterPanelTest, TheStatsBlockIsCentredInThePanel) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  for (int width : {kLeftColumnMax, kLeftColumnMin + 1}) {
    CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
    panel.SetWidth(width);
    ftxui::Screen screen = PanelScreen(panel, width);
    // Both measured from inside the border: the blank in front of the block
    // against the blank behind it.
    int left = FindCell(screen, "HP:").first - 1;
    int right = width - 2 - RowEndOf(screen, "[+]");
    // An odd column falls to the left, so the right-hand column keeps the
    // gutter it would have on the narrowest panel.
    EXPECT_EQ(left - right, (width - kLeftColumnMin) % 2)
        << "the block sits " << left << " in and " << right << " short at "
        << width;
  }
}

// The stat block is one block: what the extra stats right-align ends in the
// same column as the [+] of the rows above them, on any panel width.
TEST_F(CharacterPanelTest, TheExtraStatsLineUpWithThePlusColumn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  for (int width : {kLeftColumnMin, kLeftColumnMin + 1, kLeftColumnMax}) {
    CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
    panel.SetWidth(width);
    ftxui::Screen stats = PanelScreen(panel, width);
    EXPECT_EQ(RowEndOf(stats, "[+]"), RowEndOf(stats, "Attack Speed"))
        << "the stat rows end apart at width " << width;
  }
}

// And on a panel with a column or less to spare, the block lands in the
// Skills tab's [+] column too -- the odd column of the centring goes to the
// left so that it does.
TEST_F(CharacterPanelTest, TheTabsRightColumnsAgreeOnANarrowPanel) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  for (int width : {kLeftColumnMin, kLeftColumnMin + 1}) {
    CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
    panel.SetWidth(width);
    ftxui::Screen stats = PanelScreen(panel, width);
    ftxui::Component comp = panel.MakeComponent();
    comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
    ftxui::Screen skills = PanelScreen(panel, width);
    EXPECT_EQ(RowEndOf(skills, "[+]"), RowEndOf(stats, "[+]"))
        << "the two tabs end apart at width " << width;
  }
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
  CharacterPanel panel(c, account_, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  ftxui::Screen screen = RenderToScreen(comp);
  std::pair<int, int> wordy = FindCell(screen, "Final Attack: Crossbo");
  std::pair<int, int> rush = FindCell(screen, "Rush");
  ASSERT_GE(wordy.first, 0);
  ASSERT_GE(rush.first, 0);
  ASSERT_NE(wordy.second, rush.second) << "expected two rows, not one";
  EXPECT_EQ(wordy.first, rush.first);
  ASSERT_GE(FindInRow(screen, wordy.second, " 0 "), 0);
  EXPECT_EQ(FindInRow(screen, wordy.second, " 0 "),
            FindInRow(screen, rush.second, " 0 "));
  // And so does the [+] past them, which is the far side of the column.
  EXPECT_EQ(FindInRow(screen, wordy.second, "[+]"),
            FindInRow(screen, rush.second, "[+]"));
}

// Combat Orders' shape: two levels to every other skill in the book, by the
// time it is maxed itself.
std::map<std::string, Skill> LendingCatalog() {
  Skill orders;
  orders.set_name("Combat Orders");
  orders.set_kind(SKILL_KIND_PASSIVE);
  orders.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  orders.set_max_level(10);
  orders.set_skill_order(2);
  orders.mutable_base()->set_skill_level_bonus(1.0);
  orders.mutable_per_level()->set_skill_level_bonus(0.11111111111);
  std::map<std::string, Skill> catalog = SkillCatalog();  // slash_blast
  catalog["combat_orders"] = orders;
  return catalog;
}

CharacterInstance MakeLender(std::mt19937& rng, int slash_blast) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[1] = 3;
  (*proto.mutable_skill_levels())["Combat Orders"] = 10;
  if (slash_blast > 0) {
    (*proto.mutable_skill_levels())["Slash Blast"] = slash_blast;
  }
  return CharacterInstance(rng, std::move(proto));
}

// The level the rest of the game runs on, with the borrowed part in brackets
// after it -- GMS prints a 10 with two lent as "12 (+2)", not "10 (+2)".
TEST_F(CharacterPanelTest, ALentLevelIsCountedInAndThenNamed) {
  CharacterInstance c = MakeLender(rng_, /*slash_blast=*/10);
  CharacterPanel panel(c, account_, panel_focus_, LendingCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  EXPECT_TRUE(OnScreen(comp, "12 (+2)"));
  EXPECT_FALSE(OnScreen(comp, "10 (+2)")) << "the lent levels are counted in";
}

// Two rules at once, both about who is owed nothing: a skill nobody has bought
// stays at 0 with no brackets, and the skill lending the levels lends none to
// itself.
TEST_F(CharacterPanelTest, NothingIsLentToTheUnlearnedOrToTheLender) {
  CharacterInstance c = MakeLender(rng_, /*slash_blast=*/0);
  CharacterPanel panel(c, account_, panel_focus_, LendingCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  ftxui::Screen screen = RenderToScreen(comp);
  int blast = FindCell(screen, "Slash Blast").second;
  int orders = FindCell(screen, "Combat Orders").second;
  ASSERT_GE(blast, 0);
  ASSERT_GE(orders, 0);
  EXPECT_EQ(FindInRow(screen, blast, "(+"), -1);
  EXPECT_EQ(FindInRow(screen, orders, "(+"), -1);
  EXPECT_GE(FindInRow(screen, orders, " 10 "), 0) << "its own level, unlifted";
}

// The column is as wide as the widest level ON THE PAGE, not the widest one
// the character could ever reach: an unopened book is a thin column of 0s,
// and the first point spent widens it and narrows the names beside it.
TEST_F(CharacterPanelTest, TheLevelColumnIsAsWideAsThePageNeeds) {
  CharacterInstance closed = MakeLender(rng_, /*slash_blast=*/0);
  CharacterPanel thin(closed, account_, panel_focus_, LendingCatalog());
  ftxui::Component thin_comp = thin.MakeComponent();
  thin_comp->OnEvent(ftxui::Event::ArrowRight);
  ftxui::Screen thin_screen = RenderToScreen(thin_comp);

  CharacterInstance opened = MakeLender(rng_, /*slash_blast=*/1);
  CharacterPanel wide(opened, account_, panel_focus_, LendingCatalog());
  ftxui::Component wide_comp = wide.MakeComponent();
  wide_comp->OnEvent(ftxui::Event::ArrowRight);
  ftxui::Screen wide_screen = RenderToScreen(wide_comp);

  int closed_row = FindCell(thin_screen, "Slash Blast").second;
  int opened_row = FindCell(wide_screen, "Slash Blast").second;
  ASSERT_GE(closed_row, 0);
  ASSERT_GE(opened_row, 0);
  EXPECT_GT(FindInRow(thin_screen, closed_row, "0"),
            FindInRow(wide_screen, opened_row, "3 (+2)"))
      << "one point in, the column widens and the names give up the room";
}

// Right-aligned, so the gap a short level leaves sits between the name and the
// level instead of trailing off after it.
TEST_F(CharacterPanelTest, TheLevelIsRightAlignedInItsColumn) {
  CharacterInstance c = MakeLender(rng_, /*slash_blast=*/1);
  CharacterPanel panel(c, account_, panel_focus_, LendingCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);

  ftxui::Screen screen = RenderToScreen(comp);
  int blast = FindCell(screen, "3 (+2)").second;
  int orders = FindCell(screen, "Combat Orders").second;
  ASSERT_GE(blast, 0);
  ASSERT_GE(orders, 0);
  // "3 (+2)" and "10" end in the same column; only their heads differ.
  EXPECT_EQ(FindInRow(screen, blast, "3 (+2) "),
            FindInRow(screen, orders, "10 ") - 4);
}

// The column the brackets need is only opened for a character whose book can
// lend levels; everyone else gets the room for their skill names instead.
TEST_F(CharacterPanelTest, OnlyALenderPaysForTheLentColumn) {
  CharacterInstance lender = MakeLender(rng_, /*slash_blast=*/10);
  CharacterPanel wide(lender, account_, panel_focus_, LendingCatalog());
  ftxui::Component wide_comp = wide.MakeComponent();
  wide_comp->OnEvent(ftxui::Event::ArrowRight);

  CharacterInstance plain = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel narrow(plain, account_, panel_focus_, SkillCatalog());
  ftxui::Component narrow_comp = narrow.MakeComponent();
  narrow_comp->OnEvent(ftxui::Event::ArrowRight);

  ftxui::Screen wide_screen = RenderToScreen(wide_comp);
  ftxui::Screen narrow_screen = RenderToScreen(narrow_comp);
  int lent = FindCell(wide_screen, "12 (+2)").first;
  int plain_level = FindInRow(
      narrow_screen, FindCell(narrow_screen, "Slash Blast").second, " 0 ");
  ASSERT_GE(lent, 0);
  ASSERT_GE(plain_level, 0);
  EXPECT_LT(lent, plain_level) << "the lender's names give up the room";
  ftxui::Element card = wide.Render();
  EXPECT_LE(ftxui::Dimension::Fit(card).dimx, kLeftColumnMin)
      << "and the panel itself does not widen for them";
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
  CharacterPanel panel(c, account_, panel_focus_, AllKindsCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills

  EXPECT_TRUE(OnScreen(comp, "A:  Slash Blast"));
  EXPECT_TRUE(OnScreen(comp, "A:  War Leap"));
  EXPECT_TRUE(OnScreen(comp, "AA: Evil Eye Shock"));
  EXPECT_TRUE(OnScreen(comp, "P:  Iron Body"));
  EXPECT_TRUE(OnScreen(comp, "    Nameless"));

  // Orange, not red: red is the colour that says a thing is refused, and an
  // attack skill is never a problem for carrying its own kind.
  EXPECT_EQ(ColorOf(comp, "A:  Slash Blast"), kGold);
  EXPECT_NE(ColorOf(comp, "A:  Slash Blast"), kRed);
  EXPECT_EQ(ColorOf(comp, "AA: Evil Eye Shock"), kPurple);
  EXPECT_EQ(ColorOf(comp, "P:  Iron Body"), kGreen);
}

// The tag is a fact about the skill, not a second thing to press: Enter opens
// the skill, so the cursor covers the name and stops there at both ends.
TEST_F(CharacterPanelTest, TheHighlightLeavesTheTagAlone) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, AllKindsCatalog());
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowRight);  // page I -> page II
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Spear Sweep"), std::string::npos);
  EXPECT_EQ(rendered.find("Slash Blast"), std::string::npos);
}

TEST_F(CharacterPanelTest, DownIntoSkillRowsThenEnterFiresLearn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  std::string learned;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill& s) { learned = s.name(); };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(learned, "Slash Blast");
}

TEST_F(CharacterPanelTest, DownIntoSkillRowsLandsOnTheNameNotThePlus) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  bool learned = false;
  std::string inspected;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill&) { learned = true; };
  actions.menu = [&](const Skill& s) { inspected = s.name(); };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  bool learned = false;
  bool inspected = false;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill&) { learned = true; };
  actions.menu = [&](const Skill&) { inspected = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar -> skill rows

  // Eleven for the name, then the padding holding the column open and the
  // level beyond it -- neither of which Enter reaches.
  EXPECT_EQ(InversionMask(comp, "Slash Blast           0"),
            "11111111111"
            "000000000000");
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  std::string inspected;
  CharacterPanelActions actions;
  actions.menu = [&](const Skill& s) { inspected = s.name(); };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(inspected, "Slash Blast");
}

TEST_F(CharacterPanelTest, NoSpEntersTheSkillRowsButEnterDoesNothing) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  bool fired = false;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill&) { fired = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  bool fired = false;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill&) { fired = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows (has SP, so entered)
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  EXPECT_NE(RenderComponent(comp).find("Slash Blast          20"),
            std::string::npos);
  comp->OnEvent(ftxui::Event::Return);  // maxed: nothing to learn
  EXPECT_FALSE(fired);
}

TEST_F(CharacterPanelTest, UpFromSkillRowsReturnsToTheAdvancementBar) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // skill rows
  comp->OnEvent(ftxui::Event::ArrowUp);     // back to the advancement bar
  comp->OnEvent(ftxui::Event::ArrowUp);     // advancement bar -> outer tabs
  comp->OnEvent(ftxui::Event::ArrowLeft);   // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// The Skills tab's ring is four deep: the username row, the outer tab bar, the
// advancement bar under it, then the skills. Up off the top arrives at the
// bottom.
TEST_F(CharacterPanelTest, UpFromTheTabBarLandsOnTheLastSkill) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  bool fired = false;
  CharacterPanelActions actions;
  actions.learn = [&fired](const Skill&) { fired = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowUp);     // tab bar -> the username row
  comp->OnEvent(ftxui::Event::ArrowUp);     // username -> the last skill row
  comp->OnEvent(ftxui::Event::ArrowRight);  // name -> [+]
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(fired) << "Enter learns only from a skill row";
}

TEST_F(CharacterPanelTest, DownFromTheLastSkillReturnsToTheBar) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // the one skill row
  comp->OnEvent(ftxui::Event::ArrowDown);   // off the bottom -> the username
  comp->OnEvent(ftxui::Event::ArrowDown);   // username -> outer tab bar
  // Left switches outer tabs only from the bar, so Stats coming back is where
  // the cursor went.
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// A stage with no skills is a ring of three: the username row, the outer bar
// and the advancement bar. Down from the advancement bar carries on round
// rather than descending into a list that is not there.
TEST_F(CharacterPanelTest, DownFromTheAdvBarSkipsAnEmptySkillList) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);
  CharacterPanel panel(c, account_,
                       panel_focus_);  // no catalog: no skills at all
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // -> the username, not a row
  comp->OnEvent(ftxui::Event::ArrowDown);   // -> outer tab bar
  comp->OnEvent(ftxui::Event::ArrowLeft);   // outer tabs: Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

// Walking from one skill row to the next keeps whichever column the cursor was
// in. Only arriving from outside the rows puts it back on the name, so a player
// spending SP down a list does not have to re-cross to the [+] on every row.
TEST_F(CharacterPanelTest, WalkingBetweenSkillRowsKeepsTheColumn) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, TwoSkillCatalog());
  bool learned = false;
  CharacterPanelActions actions;
  actions.learn = [&learned](const Skill&) { learned = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  bool fired = false;
  CharacterPanelActions actions;
  actions.learn = [&](const Skill&) { fired = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
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
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  CharacterPanel panel(c, account_, panel_focus_);
  EXPECT_EQ(StatValue(panel.Render(), "Attack"), "10");
}

TEST_F(CharacterPanelTest, ShowsStrWithBreakdownWhenGearContributes) {
  sword_.mutable_base_stats()->set_str(5);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  CharacterPanel panel(c_, account_, panel_focus_);
  // base AP STR is 0 for the test character; gear adds 5; total = 5.
  EXPECT_NE(RenderElement(panel.Render()).find("STR: 5 (0+5)"),
            std::string::npos);
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

// The rows the panel asks for, borders included -- the height MainLayout
// stacks it at, and so what the combat panel below it has to fit around.
// Read from the requirement rather than off the screen: a window fills the box
// it is given, so rendering the panel alone measures the screen instead.
int PanelHeight(ftxui::Element element) {
  element->ComputeRequirement();
  return element->requirement().min_y;
}

TEST_F(CharacterPanelTest, ARowBudgetDropsTheLeastImportantStatsFirst) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  // 15 rows of chrome and stats above, then four rows for the extras: three
  // stats and the row that leads to the rest of them.
  panel.SetMaxRows(19);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"Attack", "Magic Attack", "Damage",
                                      "View All Stats"}));
}

// 24 rows is the terminal to fit: one goes to the exp bar and eight to a
// combat panel showing a mob bar and the respawn beat, which leaves this
// panel exactly its floor.
TEST_F(CharacterPanelTest, TheFloorLeavesTheCombatPanelItsRows) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  panel.SetMaxRows(24 - 1 - 8);
  EXPECT_EQ(PanelHeight(panel.Render()), 15);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"View All Stats"}));
}

TEST_F(CharacterPanelTest, TheViewAllStatsRowIsTheLastToGo) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  // Room for nothing but the way out, and then for less than that.
  panel.SetMaxRows(16);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"View All Stats"}));
  panel.SetMaxRows(4);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"View All Stats"}));
}

// What the budget is for: the combat panel sits under this one, and a panel
// that draws past its budget pushes the mob bars off a short terminal. So the
// measure is the height drawn, not which stats were dropped to reach it -- a
// heading row added without kStatsTabFixedRows following it passes every test
// above and still overruns by one, which is how the name row shipped.
TEST_F(CharacterPanelTest, ThePanelFitsInsideItsRowBudget) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  int natural = PanelHeight(panel.Render());
  EXPECT_EQ(natural, 24) << "chrome, the AP stats, 8 extras and the way out";
  // From the tightest budget the chrome fits in, up past the height the panel
  // wants: it takes every row it is given and not one more. 15 is the floor --
  // the chrome and the way out, with the rule above the extras given up.
  for (int budget = 15; budget <= natural + 2; ++budget) {
    panel.SetMaxRows(budget);
    EXPECT_EQ(PanelHeight(panel.Render()), std::min(budget, natural))
        << "at a budget of " << budget;
  }
}

// A Beginner's tab ends at the AP rows, so there is nothing for a budget to
// drop -- but the chrome above those rows is the same chrome, and a heading
// row counted wrong shows up here as a panel that has grown.
TEST_F(CharacterPanelTest, ABeginnerPanelIsTheChromeAndTheApRows) {
  CharacterPanel panel(c_, account_, panel_focus_);
  EXPECT_EQ(PanelHeight(panel.Render()), 14);
  panel.SetMaxRows(14);
  EXPECT_EQ(PanelHeight(panel.Render()), 14);
}

TEST_F(CharacterPanelTest, NoBudgetShowsEveryStat) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  EXPECT_EQ(ExtrasShown(panel.Render()).size(), 9u);  // 8 stats and the row
}

// --- The Skills tab's row budget ---

// A book of `count` stage-1 skills, named so their order reads off the panel.
std::map<std::string, Skill> BookOf(int count) {
  std::map<std::string, Skill> catalog;
  for (int i = 1; i <= count; ++i) {
    Skill skill;
    char name[16];
    std::snprintf(name, sizeof(name), "Skill %02d", i);
    skill.set_name(name);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(20);
    skill.set_skill_order(i);
    catalog[name] = skill;
  }
  return catalog;
}

// The panel on its Skills tab, with the cursor down on the skill rows.
ftxui::Component OnSkillRows(CharacterPanel& panel) {
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowDown);   // outer tabs -> advancement bar
  comp->OnEvent(ftxui::Event::ArrowDown);   // -> the skill rows
  return comp;
}

// Whether the scroll bar is drawn: any of the three glyphs it is made of.
bool HasScrollBar(const std::string& rendered) {
  return rendered.find("\u2503") != std::string::npos ||
         rendered.find("\u2579") != std::string::npos ||
         rendered.find("\u257b") != std::string::npos;
}

// The Bishop case: an eleven-skill book on a short terminal used to draw
// straight through the combat panel below it.
TEST_F(CharacterPanelTest, TheSkillsTabFitsInsideItsRowBudget) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, BookOf(11));
  ftxui::Component comp = OnSkillRows(panel);
  int natural = PanelHeight(panel.Render());
  EXPECT_EQ(natural, 20) << "nine rows of chrome and eleven skills";
  for (int budget = 10; budget <= natural + 2; ++budget) {
    panel.SetMaxRows(budget);
    EXPECT_EQ(PanelHeight(panel.Render()), std::min(budget, natural))
        << "at a budget of " << budget;
  }
}

// The window follows the cursor rather than sitting at the head of the book:
// every skill has to be reachable, budget or no budget.
TEST_F(CharacterPanelTest, TheSkillListScrollsToTheSelectedSkill) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, BookOf(11));
  panel.SetMaxRows(13);  // room for four skills
  ftxui::Component comp = OnSkillRows(panel);
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Skill 01"), std::string::npos);
  EXPECT_EQ(rendered.find("Skill 05"), std::string::npos) << "past the window";
  EXPECT_TRUE(HasScrollBar(rendered)) << "a book longer than its window";

  for (int i = 0; i < 10; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Skill 11"), std::string::npos)
      << "the cursor walked off the bottom of the panel";
  EXPECT_EQ(rendered.find("Skill 01"), std::string::npos)
      << "the head of the book should have scrolled away";
}

// And it centres the cursor in that window, rather than dragging it along the
// bottom edge -- the rule every scrolling list in the game follows.
TEST_F(CharacterPanelTest, TheSkillWindowCentresTheCursor) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, BookOf(11));
  panel.SetMaxRows(14);  // room for five skills
  ftxui::Component comp = OnSkillRows(panel);
  for (int i = 0; i < 5; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Skill 04"), std::string::npos) << "two above it";
  EXPECT_EQ(rendered.find("Skill 03"), std::string::npos);
  EXPECT_NE(rendered.find("Skill 08"), std::string::npos) << "two below it";

  // And walking back up moves the window a row at a time rather than snapping
  // it to the head of the book.
  comp->OnEvent(ftxui::Event::ArrowUp);
  rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Skill 03"), std::string::npos);
  EXPECT_EQ(rendered.find("Skill 08"), std::string::npos);
}

// And there is no bar over a book that fits, nor a column taken off the names
// to hold one.
TEST_F(CharacterPanelTest, NoScrollBarOverABookThatFits) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, account_, panel_focus_, BookOf(4));
  ftxui::Component comp = OnSkillRows(panel);
  panel.SetMaxRows(14);
  EXPECT_FALSE(HasScrollBar(RenderComponent(comp)));
}

// --- The username row ---

TEST_F(CharacterPanelTest, TheNameRowStartsOnTheInvitation) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  EXPECT_TRUE(OnScreen(comp, kDefaultUsername));
  EXPECT_TRUE(IsDim(comp, kDefaultUsername))
      << "dim while it is an invitation rather than a name";
}

TEST_F(CharacterPanelTest, UpFromTheTabBarLandsOnTheName) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  EXPECT_FALSE(IsInverted(comp, kDefaultUsername));
  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_TRUE(IsInverted(comp, kDefaultUsername));
}

// The other way onto the row: off the bottom of the panel, the ring coming
// round to its first stop.
TEST_F(CharacterPanelTest, DownOffTheBottomLandsOnTheName) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/0, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  // The tab bar, four AP stats and View All Stats, then round to the name.
  for (int i = 0; i < 6; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  EXPECT_TRUE(IsInverted(comp, kDefaultUsername));
}

TEST_F(CharacterPanelTest, TypingANameAndPressingEnterKeepsIt) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowUp);  // onto the name
  comp->OnEvent(ftxui::Event::Return);   // open the field
  EXPECT_FALSE(OnScreen(comp, kDefaultUsername)) << "the field opens empty";
  for (char ch : std::string("Sean99")) {
    comp->OnEvent(ftxui::Event::Character(ch));
  }
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(c.username(), "Sean99");
  EXPECT_TRUE(OnScreen(comp, "Sean99"));
}

TEST_F(CharacterPanelTest, EscapeLeavesTheOldName) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  c.SetUsername("Logikable");
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowUp);
  comp->OnEvent(ftxui::Event::Return);
  comp->OnEvent(ftxui::Event::Character('x'));
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(c.username(), "Logikable");
  EXPECT_TRUE(IsInverted(comp, "Logikable")) << "and the cursor stays put";
}

TEST_F(CharacterPanelTest, EnterOnAnEmptyFieldLeavesTheOldName) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  c.SetUsername("Logikable");
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowUp);
  comp->OnEvent(ftxui::Event::Return);  // open
  comp->OnEvent(ftxui::Event::Return);  // nothing typed
  EXPECT_EQ(c.username(), "Logikable");
}

// An arrow out of the field abandons the edit and moves in the same keystroke.
TEST_F(CharacterPanelTest, AnArrowOutOfTheFieldLeavesTheNameAndMoves) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  c.SetUsername("Logikable");
  CharacterPanel panel(c, account_, panel_focus_);
  StatField field = STAT_FIELD_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField f) { field = f; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowUp);
  comp->OnEvent(ftxui::Event::Return);
  comp->OnEvent(ftxui::Event::Character('x'));
  comp->OnEvent(ftxui::Event::ArrowDown);  // abandon, and on to the tab bar
  EXPECT_EQ(c.username(), "Logikable");
  EXPECT_FALSE(IsInverted(comp, "Logikable"));
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> STR
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(field, STAT_FIELD_STR) << "the cursor moved off the name";
}

// The row is a stop like any other, so the keys that walk the panel must not
// be eaten by a field the player never opened.
TEST_F(CharacterPanelTest, TheNameTakesKeysOnlyWhileTheFieldIsOpen) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowUp);  // onto the name
  comp->OnEvent(ftxui::Event::Character('x'));
  EXPECT_EQ(c.username(), kDefaultUsername);
  EXPECT_TRUE(OnScreen(comp, kDefaultUsername));
}

// The block is what a job fills in, so a Beginner's tab ends at the AP rows --
// and the way through to the All Stats screen ends with it. Two steps up off
// the bar land on LUK instead, and Enter there spends the point.
TEST_F(CharacterPanelTest, ABeginnerHasNoCombatStatsAndNoWayToTheScreen) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, account_, panel_focus_);
  EXPECT_TRUE(ExtrasShown(panel.Render()).empty());

  StatField field = STAT_FIELD_UNSPECIFIED;
  bool opened = false;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField f) { field = f; };
  actions.all_stats = [&] { opened = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the username row
  comp->OnEvent(ftxui::Event::ArrowUp);  // username -> LUK
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(opened);
  EXPECT_EQ(field, STAT_FIELD_LUK);
}

// Each advancement opens the block further: the first brings what a new job
// can move, the second the percent rows its passives write, the third the
// three that pay out on nothing the player has met before then.
TEST_F(CharacterPanelTest, EachAdvancementOpensTheStatBlockFurther) {
  CharacterInstance first = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel panel(first, account_, panel_focus_);
  EXPECT_EQ(ExtrasShown(panel.Render()),
            (std::vector<std::string>{"Attack", "Magic Attack", "Attack Speed",
                                      "View All Stats"}));

  CharacterInstance second = MakeSpearman(rng_);
  CharacterPanel later(second, account_, panel_focus_);
  std::vector<std::string> shown = ExtrasShown(later.Render());
  EXPECT_NE(std::find(shown.begin(), shown.end(), "Critical Rate"),
            shown.end());
  EXPECT_NE(std::find(shown.begin(), shown.end(), "Damage"), shown.end());
  EXPECT_EQ(std::find(shown.begin(), shown.end(), "Boss Damage"), shown.end());

  Character third_proto;
  third_proto.set_level(70);
  third_proto.set_job(JOB_BERSERKER);
  third_proto.set_job_stage(3);
  CharacterInstance third(rng_, std::move(third_proto));
  CharacterPanel last(third, account_, panel_focus_);
  std::vector<std::string> late = ExtrasShown(last.Render());
  for (const char* label :
       {"Boss Damage", "Ignore DEF", "Additional EXP", "Arcane Force"}) {
    EXPECT_NE(std::find(late.begin(), late.end(), label), late.end()) << label;
  }
  // And the row nobody asked for is gone from both.
  EXPECT_EQ(std::find(late.begin(), late.end(), "Dodge Chance"), late.end());
}

// The stat every Arcane River map measures the character against, at the foot
// of the block and under the rule that separates it from the swing.
TEST_F(CharacterPanelTest, ShowsTheArcaneForceTheWornSymbolsCome) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  CharacterInstance c(rng_, std::move(proto));
  CharacterPanel bare(c, account_, panel_focus_);
  EXPECT_EQ(StatValue(bare.Render(), "Arcane Force"), "0");

  EquipPrototype symbol;
  symbol.set_name("Arcane Symbol: Vanishing Journey");
  symbol.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  symbol.mutable_arcane_symbol()->set_meso_cost_base(8);
  Equip state;
  state.set_symbol_level(8);
  c.PickUp(std::make_unique<EquipInstance>(symbol, state));
  ASSERT_TRUE(c.Equip(0));

  CharacterPanel worn(c, account_, panel_focus_);
  EXPECT_EQ(StatValue(worn.Render(), "Arcane Force"), "100");
}

TEST_F(CharacterPanelTest, ShowsTheDamageLeversAsPercentages) {
  Skill levers = LeverPassive();
  std::map<std::string, Skill> catalog;
  catalog["levers"] = levers;
  CharacterInstance c = MakeSpearman(rng_);
  ASSERT_TRUE(c.LearnSkill(levers, 1));

  CharacterPanel panel(c, account_, panel_focus_, catalog);
  EXPECT_EQ(StatValue(panel.Render(), "Damage"), "7.50%");
  EXPECT_EQ(StatValue(panel.Render(), "Final Damage"), "5.00%");
  // The base 5% and 35% every character carries, with the skill's on top.
  EXPECT_EQ(StatValue(panel.Render(), "Critical Rate"), "25.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Critical Damage"), "37.50%");
}

// The levers a skill has to buy read zero; the two every character is born
// with read what they are born with.
TEST_F(CharacterPanelTest, TheLeversReadZeroButCritReadsItsBase) {
  CharacterInstance c = MakeSpearman(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  EXPECT_EQ(StatValue(panel.Render(), "Damage"), "0.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Final Damage"), "0.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Critical Rate"), "5.00%");
  EXPECT_EQ(StatValue(panel.Render(), "Critical Damage"), "35.00%");
}

TEST_F(CharacterPanelTest, AttackSpeedNamesTheStageTheWeaponIsSwungAt) {
  sword_.set_attack_speed(ATTACK_SPEED_AVERAGE);
  Skill levers = LeverPassive();  // +2 stages
  std::map<std::string, Skill> catalog;
  catalog["levers"] = levers;
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/1);
  ASSERT_TRUE(c.LearnSkill(levers, 1));

  CharacterPanel panel(c, account_, panel_focus_, catalog);
  EXPECT_EQ(StatValue(panel.Render(), "Attack Speed"), "-");

  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  EXPECT_EQ(StatValue(panel.Render(), "Attack Speed"), "Fast 2");
}

TEST_F(CharacterPanelTest, AttackSpeedStopsAtTheFastestStage) {
  sword_.set_attack_speed(ATTACK_SPEED_FASTEST_3);
  Skill levers = LeverPassive();  // +2 stages, with nowhere to go
  std::map<std::string, Skill> catalog;
  catalog["levers"] = levers;
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/1);
  ASSERT_TRUE(c.LearnSkill(levers, 1));
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);

  CharacterPanel panel(c, account_, panel_focus_, catalog);
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
  CharacterPanel panel(c, account_, panel_focus_);
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
  CharacterPanel panel(c_, account_, panel_focus_);
  ASSERT_EQ(BorderColor(panel.Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(panel.Render()), kYellow);
}

// The panel keeps no clock: whoever lit it is the one that puts it out, and
// the border has to actually go back.
TEST_F(CharacterPanelTest, ClearingTheHighlightRestoresTheTheme) {
  CharacterPanel panel(c_, account_, panel_focus_);
  panel.SetHighlighted(true);
  panel.SetHighlighted(false);
  EXPECT_EQ(BorderColor(panel.Render()), kTheme);
}

// This panel is the one with rules through the middle of it -- under the
// title, under the tab bar, and between the allocated stats and the derived
// ones. A gold box around steel-blue seams is not a lit panel.
TEST_F(CharacterPanelTest, LightsItsInnerRulesGoldToo) {
  CharacterPanel panel(c_, account_, panel_focus_);
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
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Advance"), kYellow);
}

TEST_F(CharacterPanelTest, OpeningTheAdvanceTabClearsItsGold) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent();
  panel_focus_ = kCharPanel;
  component->OnEvent(ftxui::Event::ArrowRight);

  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Advance"), kTheme);
  EXPECT_TRUE(account_.Seen(AdvanceTabKey(1)))
      << "recorded against the stage being advanced into, so the next "
         "advancement is news again";
}

// The other way a tab is read: focus arriving on the panel while the tab is
// already the open one. The controller calls this on every Tab, so the rule
// reads the same on both panels that carry gold -- see the bag, where the
// Equip tab is the one it actually catches.
TEST_F(CharacterPanelTest, MarkingTheActiveTabReadsIt) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent();
  panel_focus_ = kCharPanel;
  component->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_TRUE(account_.Seen(AdvanceTabKey(1)));

  // Stats has nothing to announce, so marking it records nothing rather than
  // recording the empty key -- which Seen would then answer yes to.
  component->OnEvent(ftxui::Event::ArrowLeft);
  panel.MarkActiveTabSeen();
  EXPECT_FALSE(account_.Seen(""));
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
  CharacterPanel panel(c, account_, panel_focus_, GatedCatalog());
  ftxui::Component comp = panel.MakeComponent();
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
  CharacterPanel panel(c, account_, panel_focus_, GatedCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  EXPECT_FALSE(IsDim(comp, "Hyper Body"));
  EXPECT_FALSE(IsDim(comp, "[+]"));
}

// An ordinary skill with SP behind it must not be dimmed by the check.
TEST_F(CharacterPanelTest, ASkillDemandingNothingIsNotDimmed) {
  CharacterInstance c = MakeWarrior(rng_, /*sp=*/20);
  CharacterPanel panel(c, account_, panel_focus_, SkillCatalog());
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Skills
  EXPECT_FALSE(IsDim(comp, "Slash Blast"));
}

// The rendered panel split into rows, for a test asking what follows what.
std::vector<std::string> Rows(ftxui::Element element) {
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kLeftColumnMin), ftxui::Dimension::Fixed(24));
  ftxui::Render(screen, element);
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.empty() ? " " : cell;
    }
    rows.push_back(row);
  }
  return rows;
}

// A 4th-job Hero at the level Hyper Stats open at, with a level in one stat
// of each allocation so the two read differently.
CharacterInstance MakeHyperHero(std::mt19937& rng, int ap = 0) {
  Character proto;
  proto.set_level(140);
  proto.set_ap(ap);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  (*proto.mutable_hyper_stats()
        ->mutable_farming()
        ->mutable_levels())[HYPER_STAT_FIELD_STR] = 1;
  (*proto.mutable_hyper_stats()
        ->mutable_bossing()
        ->mutable_levels())[HYPER_STAT_FIELD_STR] = 2;
  return CharacterInstance(rng, std::move(proto));
}

// The row arrives with the Hyper Stats it picks between, and not before.
TEST_F(CharacterPanelTest, TheStatsTabGetsAFarmBossRowAt140) {
  CharacterInstance early = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel before(early, account_, panel_focus_);
  EXPECT_EQ(RenderElement(before.Render()).find("Farm"), std::string::npos);

  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Farm"), std::string::npos);
  EXPECT_NE(rendered.find("Boss"), std::string::npos);
}

// The two rows of tabs sit together, with the rule under the pair.
TEST_F(CharacterPanelTest, NoRuleBetweenTheTwoRowsOfTabs) {
  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  std::vector<std::string> rows = Rows(panel.Render());
  size_t tabs = 0;
  while (tabs < rows.size() && rows[tabs].find("Stats") == std::string::npos) {
    ++tabs;
  }
  ASSERT_LT(tabs + 1, rows.size());
  EXPECT_NE(rows[tabs + 1].find("Farm"), std::string::npos)
      << "the Farm/Boss row should follow the tab bar directly";
}

// Which allocation the row is on is what the stats above and below it read.
TEST_F(CharacterPanelTest, TheFarmBossRowPicksWhatTheStatsRead) {
  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent();
  EXPECT_NE(RenderComponentText(comp).find("STR: 30 (0+30)"),
            std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> the Farm/Boss row
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Boss
  EXPECT_EQ(panel.hyper_preset(), StatPreset::kBossing);
  EXPECT_NE(RenderComponentText(comp).find("STR: 60 (0+60)"),
            std::string::npos);

  // The bars in this panel clamp rather than wrap, so the end is the end.
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.hyper_preset(), StatPreset::kBossing);
  comp->OnEvent(ftxui::Event::ArrowLeft);
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(panel.hyper_preset(), StatPreset::kFarming);
}

// And it is a stop in the same ring: Down off it lands on the first stat.
TEST_F(CharacterPanelTest, TheFarmBossRowIsAStopBetweenTheTabsAndTheStats) {
  CharacterInstance c = MakeHyperHero(rng_, /*ap=*/1);
  CharacterPanel panel(c, account_, panel_focus_);
  StatField allocated = STAT_FIELD_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.allocate = [&](StatField field) { allocated = field; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the Farm/Boss row
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> STR
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(allocated, STAT_FIELD_STR);
}

// --- the Hyper tab ---

// Walks the cursor from the outer tab bar onto the Hyper tab and down into
// its stat rows. Stats -> Skills -> Hyper is two steps right, and a 4th job
// with nothing pending has no Advance tab past it.
ftxui::Component OnHyperRows(CharacterPanel& panel) {
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Hyper
  comp->OnEvent(ftxui::Event::ArrowDown);   // -> the Farm/Boss row
  comp->OnEvent(ftxui::Event::ArrowDown);   // -> the first stat row
  return comp;
}

TEST_F(CharacterPanelTest, TheHyperTabArrivesWithTheStatsAndIsGoldUntilRead) {
  CharacterInstance early = MakeWarrior(rng_, /*sp=*/0);
  CharacterPanel before(early, account_, panel_focus_);
  EXPECT_EQ(RenderElement(before.Render()).find("Hyper"), std::string::npos);

  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Hyper"), kYellow);

  panel_focus_ = kCharPanel;
  ftxui::Component comp = panel.MakeComponent();
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowRight);
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Hyper"), kTheme);
  EXPECT_TRUE(account_.Seen(kHyperTabKey));
}

// Every stat, its level and the points left -- and nothing else.
TEST_F(CharacterPanelTest, TheHyperTabListsTheStatsAndTheSparePoints) {
  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  std::string rendered = TextOf(RenderToScreen(OnHyperRows(panel), 32));
  for (int i = 0; i < kNumHyperStats; ++i) {
    EXPECT_NE(rendered.find(HyperStatName(kHyperStatOrder[i])),
              std::string::npos)
        << HyperStatName(kHyperStatOrder[i]);
  }
  // Level 140 pays three points, and this character has spent one on STR.
  EXPECT_NE(rendered.find("2 Points"), std::string::npos);
  EXPECT_NE(rendered.find("[Reset]"), std::string::npos);
  // What a stat is worth is on the card Enter opens, not in a column here.
  EXPECT_EQ(rendered.find("+30"), std::string::npos);
}

// The [+] is the door, and it closes on a stat the level holds shut. The row
// itself dims with it, as a locked skill's does.
TEST_F(CharacterPanelTest, ArcaneForceIsHeldShutUntilItsOwnLevel) {
  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  ftxui::Component comp = OnHyperRows(panel);
  EXPECT_TRUE(IsDim(comp, "Arcane Force", /*rows=*/32));

  HyperStatField raised = HYPER_STAT_FIELD_UNSPECIFIED;
  CharacterPanelActions actions;
  actions.hyper_allocate = [&](HyperStatField field) { raised = field; };
  ftxui::Component with_callback = panel.MakeComponent(actions);
  // Down to the last row, which is Arcane Force, and over to its [+].
  for (int i = 1; i < kNumHyperStats; ++i) {
    with_callback->OnEvent(ftxui::Event::ArrowDown);
  }
  with_callback->OnEvent(ftxui::Event::ArrowRight);
  with_callback->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(raised, HYPER_STAT_FIELD_UNSPECIFIED)
      << "Arcane Force below level 200 has no [+] to press";
}

// A stat the points do reach asks the question rather than spending straight
// away -- the panel spends nothing itself.
TEST_F(CharacterPanelTest, TheHyperPlusAsksAboutTheStatUnderIt) {
  CharacterInstance c = MakeHyperHero(rng_);
  HyperStatField raised = HYPER_STAT_FIELD_UNSPECIFIED;
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  CharacterPanelActions actions;
  actions.hyper_allocate = [&](HyperStatField field) { raised = field; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowDown);
  comp->OnEvent(ftxui::Event::ArrowDown);
  comp->OnEvent(ftxui::Event::ArrowDown);   // STR -> DEX
  comp->OnEvent(ftxui::Event::ArrowRight);  // the name -> the [+]
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(raised, HYPER_STAT_FIELD_DEX);
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_DEX), 0)
      << "the panel asks; the controller spends";
}

// Enter on the name opens the card, as it does on a skill's name -- and it is
// never gated: a stat the level holds shut is the one worth reading about.
TEST_F(CharacterPanelTest, EnterOnAHyperStatNameOpensIt) {
  CharacterInstance c = MakeHyperHero(rng_);
  HyperStatField opened = HYPER_STAT_FIELD_UNSPECIFIED;
  HyperStatField raised = HYPER_STAT_FIELD_UNSPECIFIED;
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  CharacterPanelActions actions;
  actions.hyper_allocate = [&](HyperStatField field) { raised = field; };
  actions.hyper_inspect = [&](HyperStatField field) { opened = field; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowDown);
  comp->OnEvent(ftxui::Event::ArrowDown);  // the first stat row, on its name
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, HYPER_STAT_FIELD_STR);
  EXPECT_EQ(raised, HYPER_STAT_FIELD_UNSPECIFIED) << "the name spends nothing";

  // Right moves onto the [+], and Left back to the name.
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(raised, HYPER_STAT_FIELD_STR);
  opened = HYPER_STAT_FIELD_UNSPECIFIED;
  comp->OnEvent(ftxui::Event::ArrowLeft);
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, HYPER_STAT_FIELD_STR);

  // And the last row, which the character's level holds shut, opens too.
  for (int i = 1; i < kNumHyperStats; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(opened, HYPER_STAT_FIELD_ARCANE_FORCE);
}

// One column between the level and the [+], the same gap the skill rows have.
TEST_F(CharacterPanelTest, TheHyperRowsPutOneColumnBeforeThePlus) {
  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  ftxui::Screen screen = RenderToScreen(OnHyperRows(panel), 32);
  std::pair<int, int> str = FindCell(screen, "STR");
  ASSERT_GE(str.second, 0);
  // "  1 [+]": the digit, one blank, then the button.
  int plus = RowEnd(screen, str.second) - 2;
  EXPECT_EQ(screen.PixelAt(plus, str.second).character, "[");
  EXPECT_EQ(screen.PixelAt(plus - 1, str.second).character, " ");
  EXPECT_EQ(screen.PixelAt(plus - 2, str.second).character, "1");
}

// The last stop in the ring, under a rule of its own.
TEST_F(CharacterPanelTest, ResetIsTheStopBelowTheStats) {
  CharacterInstance c = MakeHyperHero(rng_);
  bool reset = false;
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  CharacterPanelActions actions;
  actions.hyper_allocate = [](HyperStatField) {};
  actions.hyper_reset = [&]() { reset = true; };
  ftxui::Component comp = panel.MakeComponent(actions);
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowRight);
  // Up off the name row -- the top of the ring -- comes out at the bottom of
  // it, which is [Reset].
  comp->OnEvent(ftxui::Event::ArrowUp);
  comp->OnEvent(ftxui::Event::ArrowUp);
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(reset);
}

// The rule and the button are never what a short terminal gives up.
TEST_F(CharacterPanelTest, TheHyperTabKeepsItsResetAtEveryBudget) {
  CharacterInstance c = MakeHyperHero(rng_);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  ftxui::Component comp = OnHyperRows(panel);
  int natural = PanelHeight(panel.Render());
  for (int budget = 12; budget <= natural + 2; ++budget) {
    panel.SetMaxRows(budget);
    EXPECT_EQ(PanelHeight(panel.Render()), std::min(budget, natural))
        << "at a budget of " << budget;
    EXPECT_NE(TextOf(RenderToScreen(comp, 32)).find("[Reset]"),
              std::string::npos)
        << "at a budget of " << budget;
  }
}

// --- the Ability tab ---

// A 4th-job Hero at the level Inner Ability opens at, carrying `honor` and a
// line of each of the three ranks that read differently: a Legendary one that
// holds, a Unique one that holds, and an Epic one that never can.
CharacterInstance MakeAbilityHero(std::mt19937& rng, int64_t honor) {
  Character proto;
  proto.set_level(kInnerAbilityUnlockLevel);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  proto.set_honor(honor);
  for (AbilityPreset* preset :
       {proto.mutable_inner_ability()->mutable_farming(),
        proto.mutable_inner_ability()->mutable_bossing()}) {
    preset->set_rank(ABILITY_RANK_LEGENDARY);
    AbilityLine* top = preset->add_lines();
    top->set_type(ABILITY_LINE_TYPE_BOSS_DAMAGE);
    top->set_rank(ABILITY_RANK_LEGENDARY);
    AbilityLine* middle = preset->add_lines();
    middle->set_type(ABILITY_LINE_TYPE_STR);
    middle->set_rank(ABILITY_RANK_UNIQUE);
    AbilityLine* bottom = preset->add_lines();
    bottom->set_type(ABILITY_LINE_TYPE_ATTACK);
    bottom->set_rank(ABILITY_RANK_EPIC);
  }
  return CharacterInstance(rng, std::move(proto));
}

// Walks the cursor onto the Ability tab and down into its line rows. Stats ->
// Skills -> Hyper -> Ability is three steps right.
ftxui::Component OnAbilityRows(CharacterPanel& panel) {
  ftxui::Component comp = panel.MakeComponent();
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> the Farm/Boss row
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> the first line
  return comp;
}

// The tab is gated on this character's own level, and the gold on it is the
// account's: the first one there is told, and the next one is not.
TEST_F(CharacterPanelTest, TheAbilityTabArrivesAt160AndIsGoldOnceAnAccount) {
  CharacterInstance early = MakeHyperHero(rng_);
  CharacterPanel before(early, account_, panel_focus_);
  EXPECT_EQ(RenderElement(before.Render()).find("Ability"), std::string::npos);

  CharacterInstance c = MakeAbilityHero(rng_, /*honor=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Ability"), kYellow);

  panel_focus_ = kCharPanel;
  OnAbilityRows(panel);
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(LabelColor(panel.Render(), "Ability"), kTheme);
  EXPECT_TRUE(account_.Seen(kAbilityTabKey));

  // A second character on the same account arrives to a quiet tab.
  CharacterInstance next = MakeAbilityHero(rng_, /*honor=*/0);
  CharacterPanel second(next, account_, panel_focus_);
  EXPECT_EQ(LabelColor(second.Render(), "Ability"), kTheme);
}

// The three lines, the pool over them and the price under them.
TEST_F(CharacterPanelTest, TheAbilityTabListsTheLinesTheHonorAndTheCost) {
  CharacterInstance c = MakeAbilityHero(rng_, /*honor=*/12345);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  std::string rendered = TextOf(RenderToScreen(OnAbilityRows(panel)));
  EXPECT_NE(rendered.find("Boss Damage"), std::string::npos);
  EXPECT_NE(rendered.find("+20%"), std::string::npos);
  EXPECT_NE(rendered.find("STR"), std::string::npos);
  EXPECT_NE(rendered.find("+30"), std::string::npos);
  // The pool reads with commas and the price without: one is a total to read
  // off, the other a number to weigh against it.
  EXPECT_NE(rendered.find("12,345 Honor"), std::string::npos);
  EXPECT_NE(rendered.find("Honor Cost"), std::string::npos);
  EXPECT_NE(rendered.find("8000"), std::string::npos);
  EXPECT_NE(rendered.find("[Reroll]"), std::string::npos);
}

// Every row is painted its own rank, and a held one is painted darker still.
TEST_F(CharacterPanelTest, EachLineIsPaintedItsRank) {
  CharacterInstance c = MakeAbilityHero(rng_, /*honor=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  ftxui::Component comp = OnAbilityRows(panel);
  ftxui::Screen screen = RenderToScreen(comp);
  const auto background = [&](const std::string& needle) {
    std::pair<int, int> at = FindCell(screen, needle);
    return screen.PixelAt(at.first, at.second).background_color;
  };
  EXPECT_EQ(background("Boss Damage"), kLegendary.ToColor());
  EXPECT_EQ(background("STR"), kUnique.ToColor());
  EXPECT_EQ(background("Attack"), kEpic.ToColor());

  ASSERT_TRUE(c.LockAbilityLine(0, true));
  ftxui::Screen held = RenderToScreen(comp);
  std::pair<int, int> at = FindCell(held, "Boss Damage");
  EXPECT_EQ(held.PixelAt(at.first, at.second).background_color,
            Faded(kLegendary).ToColor());
}

// Only a line that can be held carries a lock, so a row with no symbol is the
// refusal -- and Enter on it asks nothing.
TEST_F(CharacterPanelTest, OnlyALineThatHoldsCarriesALock) {
  CharacterInstance c = MakeAbilityHero(rng_, /*honor=*/0);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  int locked = -1;
  CharacterPanelActions actions;
  actions.ability_lock = [&](int index) { locked = index; };
  ftxui::Component comp = panel.MakeComponent(actions);
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> the Farm/Boss row
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> the Legendary line
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(locked, 0);

  locked = -1;
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> the Unique line
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(locked, 1);

  locked = -1;
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> the Epic line, which cannot
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(locked, -1) << "an Epic line has no lock to toggle";
}

// The price reddens and the button greys when the pool is short, and Enter on
// it does nothing -- there is nothing a dialog could add to what is on screen.
TEST_F(CharacterPanelTest, AShortPoolRedensTheCostAndShutsTheButton) {
  CharacterInstance poor = MakeAbilityHero(rng_, /*honor=*/10);
  CharacterPanel panel(poor, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  int rerolls = 0;
  CharacterPanelActions actions;
  actions.ability_reroll = [&] { ++rerolls; };
  ftxui::Component comp = panel.MakeComponent(actions);
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  for (int i = 0; i < 5; ++i) {
    comp->OnEvent(
        ftxui::Event::ArrowDown);  // bar -> preset -> 3 lines -> button
  }
  EXPECT_EQ(ColorOf(comp, "8000"), kRed);
  EXPECT_TRUE(IsDim(comp, "[Reroll]"));
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(rerolls, 0);

  CharacterInstance rich = MakeAbilityHero(rng_, /*honor=*/8000);
  CharacterPanel afford(rich, account_, panel_focus_);
  ftxui::Component paid = afford.MakeComponent(actions);
  for (int i = 0; i < 3; ++i) {
    paid->OnEvent(ftxui::Event::ArrowRight);
  }
  for (int i = 0; i < 5; ++i) {
    paid->OnEvent(ftxui::Event::ArrowDown);
  }
  EXPECT_NE(ColorOf(paid, "8000"), kRed);
  EXPECT_FALSE(IsDim(paid, "[Reroll]"));
  paid->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(rerolls, 1);
}

// The button is the last stop in the ring, and the ring wraps to the name row
// the way every other tab's does.
TEST_F(CharacterPanelTest, TheAbilityRingEndsOnTheRerollButton) {
  CharacterInstance c = MakeAbilityHero(rng_, /*honor=*/8000);
  CharacterPanel panel(c, account_, panel_focus_);
  panel_focus_ = kCharPanel;
  int rerolls = 0;
  CharacterPanelActions actions;
  actions.ability_reroll = [&] { ++rerolls; };
  ftxui::Component comp = panel.MakeComponent(actions);
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  // Down past the Farm/Boss row and the three lines lands on the button, and
  // one more wraps back round to the name at the top.
  for (int i = 0; i < 5; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  EXPECT_TRUE(IsInverted(comp, "[Reroll]"));
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_FALSE(IsInverted(comp, "[Reroll]"));
  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_TRUE(IsInverted(comp, "[Reroll]"));
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(rerolls, 1);
}

// Five chips do not fit the narrowest panel, so the bar scrolls under them --
// and whichever tab the cursor is on is one of the chips still drawn.
TEST_F(CharacterPanelTest, FiveTabsScrollOnTheNarrowestPanel) {
  // A Crusader who never took their 4th job: five tabs at once, which is the
  // most the bar is ever asked to hold.
  Character proto;
  proto.set_level(kInnerAbilityUnlockLevel);
  proto.set_job(JOB_CRUSADER);
  proto.set_job_stage(3);
  CharacterInstance c(rng_, std::move(proto));
  ASSERT_TRUE(c.CanAdvanceJob());
  CharacterPanel panel(c, account_, panel_focus_);
  panel.SetWidth(kLeftColumnMin);
  panel_focus_ = kCharPanel;
  EXPECT_TRUE(HasCell(PanelScreen(panel, kLeftColumnMin), "\u203a"))
      << "a bar that overruns says so on the edge it runs off";

  ftxui::Component comp = panel.MakeComponent();
  for (int i = 0; i < 4; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  ftxui::Screen at_end = PanelScreen(panel, kLeftColumnMin);
  EXPECT_GE(FindCell(at_end, "Advance").first, 0)
      << "the tab the cursor is on is always drawn";

  // And the whole bar fits at the widest, marks and all left off.
  panel.SetWidth(kLeftColumnMax);
  ftxui::Screen wide = PanelScreen(panel, kLeftColumnMax);
  EXPECT_GE(FindCell(wide, "Stats").first, 0);
  EXPECT_GE(FindCell(wide, "Advance").first, 0);
}

}  // namespace
}  // namespace ms
