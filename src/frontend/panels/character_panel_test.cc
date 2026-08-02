#include "src/frontend/panels/character_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/types.h"
#include "src/frontend/widgets/panel_test_base.h"
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

// A one-skill catalog holding the stage-1 Slash Blast.
std::map<std::string, Skill> SkillCatalog() {
  std::map<std::string, Skill> catalog;
  catalog["slash_blast"] = MakeSlashBlast();
  return catalog;
}

// The inverted flag of every cell under `needle`, as a string of '1' and '0' --
// inversion is how the cursor shows itself, and a mask says exactly how far it
// reaches. Walks pixels rather than the rendered string because the window
// border is multibyte, so a byte offset is not a column. `needle` must be
// ASCII: one character, one cell. Returns "" when `needle` is not on screen.
std::string InversionMask(ftxui::Component comp, const std::string& needle) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, comp->Render());
  int len = static_cast<int>(needle.size());
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x + len <= screen.dimx(); ++x) {
      std::string got;
      for (int i = 0; i < len; ++i) {
        got += screen.PixelAt(x + i, y).character;
      }
      if (got == needle) {
        std::string mask;
        for (int i = 0; i < len; ++i) {
          if (screen.PixelAt(x + i, y).inverted) {
            mask += '1';
          } else {
            mask += '0';
          }
        }
        return mask;
      }
    }
  }
  return "";
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
TEST_F(CharacterPanelTest, ABeginnerHasNoSkillsTab) {
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
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("HP: 103"), std::string::npos);
  EXPECT_NE(rendered.find("DEF: 30"), std::string::npos);
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
  EXPECT_NE(RenderElement(panel.Render()).find("CP 11,500"), std::string::npos);
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

TEST_F(CharacterPanelTest, SkillsTabListsActivesBeforePassives) {
  // The catalog is keyed by file stem, so "iron_body" would sort ahead of
  // "slash_blast" on its own; the list must put the castable skill first.
  Skill iron_body;
  iron_body.set_name("Iron Body");
  iron_body.set_kind(SKILL_KIND_PASSIVE);
  iron_body.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  iron_body.set_max_level(20);
  Skill war_leap;
  war_leap.set_name("War Leap");
  war_leap.set_kind(SKILL_KIND_ACTIVE);
  war_leap.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  war_leap.set_max_level(5);
  std::map<std::string, Skill> catalog = SkillCatalog();  // slash_blast
  catalog["slash_blast"].set_kind(SKILL_KIND_ATTACK);
  catalog["iron_body"] = iron_body;
  catalog["war_leap"] = war_leap;

  CharacterInstance c = MakeWarrior(rng_, /*sp=*/3);
  CharacterPanel panel(c, panel_focus_, catalog);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  size_t slash = rendered.find("Slash Blast");
  size_t leap = rendered.find("War Leap");
  size_t iron = rendered.find("Iron Body");
  ASSERT_NE(slash, std::string::npos);
  ASSERT_NE(leap, std::string::npos);
  ASSERT_NE(iron, std::string::npos);
  EXPECT_LT(slash, iron);
  EXPECT_LT(leap, iron);
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

  EXPECT_EQ(InversionMask(comp, "Slash Blast  0/20"), "11111111111000000");
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
  EXPECT_NE(RenderElement(panel.Render()).find("ATT: 10"), std::string::npos);
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

}  // namespace
}  // namespace ms
