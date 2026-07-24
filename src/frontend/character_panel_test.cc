#include "src/frontend/character_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/equip_instance.h"
#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
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
  skill.set_stage(1);
  skill.set_max_level(20);
  return skill;
}

// A one-skill catalog holding the stage-1 Slash Blast.
std::map<std::string, Skill> SkillCatalog() {
  std::map<std::string, Skill> catalog;
  catalog["slash_blast"] = MakeSlashBlast();
  return catalog;
}

TEST_F(CharacterPanelTest, ShowsLevel) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Lv  1"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsJobName) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Beginner"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsBothTabs) {
  CharacterPanel panel(c_, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Stats"), std::string::npos);
  EXPECT_NE(rendered.find("Skills"), std::string::npos);
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
  iron_body.set_stage(1);
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

TEST_F(CharacterPanelTest, ArrowKeysSwitchTabs) {
  CharacterPanel panel(c_, panel_focus_);  // panel_focus_ == kCharPanel
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string skills = RenderComponent(comp);
  EXPECT_NE(skills.find("No advancements yet."),
            std::string::npos);  // c_ is a Beginner
  EXPECT_EQ(skills.find("HP:"), std::string::npos);

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
  // Back on the tab bar, Right switches to Skills and Enter no longer fires.
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_NE(RenderComponent(comp).find("No advancements yet."),
            std::string::npos);  // c is a Beginner
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

TEST_F(CharacterPanelTest, BeginnerSkillsTabShowsNoAdvancements) {
  CharacterPanel panel(c_, panel_focus_);  // c_ is a stage-0 Beginner
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("No advancements yet."), std::string::npos);
  EXPECT_EQ(rendered.find("SP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, WarriorSkillsTabShowsAdvancementTabAndSp) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);  // job_stage 1
  CharacterPanel panel(c, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([](StatField) {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find(" I "), std::string::npos);  // stage-1 tab
  EXPECT_NE(rendered.find("SP:"), std::string::npos);
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
  iron_body.set_stage(1);
  iron_body.set_max_level(20);
  Skill war_leap;
  war_leap.set_name("War Leap");
  war_leap.set_kind(SKILL_KIND_ACTIVE);
  war_leap.set_stage(1);
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
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(learned, "Slash Blast");
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
  comp->OnEvent(ftxui::Event::Return);      // nothing to spend
  EXPECT_FALSE(fired);
  // The cursor is on the rows, not the bar: Left/Right belong to the bar, so
  // an Up is what it takes to get back and switch tabs.
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
