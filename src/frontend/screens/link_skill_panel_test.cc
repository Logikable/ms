#include "src/frontend/screens/link_skill_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/link.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// A link skill with one lever, so its effect column has something to say.
Skill Link(const std::string& name, Job line, double crit) {
  Skill skill;
  skill.set_name(name);
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_link_line(line);
  skill.set_max_level(9);
  SkillPlacement* placement = skill.add_placement();
  placement->set_job_advancement(JOB_ADVANCEMENT_LINK);
  placement->set_skill_order(1);
  skill.mutable_base()->set_crit_rate(crit);
  return skill;
}

class LinkSkillPanelTest : public PanelTest {
 protected:
  void SetUp() override {
    PanelTest::SetUp();
    UnlockEverything();
    skills_["warrior"] = Link("Invincible Belief", JOB_SWORDMAN, 0.03);
    skills_["magician"] = Link("Empirical Knowledge", JOB_MAGICIAN, 0.04);
    skills_["archer"] = Link("Adventurer's Curiosity", JOB_ARCHER, 0.05);
    skills_["rogue"] = Link("Thief's Cunning", JOB_ROGUE, 0.06);
    skills_["archer"].mutable_placement(0)->set_skill_order(3);
    skills_["magician"].mutable_placement(0)->set_skill_order(2);
    skills_["rogue"].mutable_placement(0)->set_skill_order(4);

    UseCharacter(JOB_HERO, 210, 4);
  }

  // Puts a character of `job` in play and opens a fresh panel over them. A
  // roster that has climbed every other line comes with them, so all four
  // skills are on offer.
  void UseCharacter(Job job, int level, int stage) {
    Character proto;
    proto.set_level(level);
    proto.set_job(job);
    proto.set_job_stage(stage);
    hero_ = std::make_unique<CharacterInstance>(rng_, std::move(proto));
    hero_->set_account_max_level(210);
    LinkTally tally;
    tally.Record(JOB_BISHOP, 210);
    tally.Record(JOB_BOW_MASTER, 120);
    tally.Record(JOB_NIGHT_LORD, 70);
    hero_->set_link_tally(tally);
    panel_ = std::make_unique<LinkSkillPanel>(*hero_, skills_);
    panel_->Reset();
  }

  CharacterInstance& hero() {
    return *hero_;
  }

  ftxui::Screen Draw() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(40));
    ftxui::Render(screen, ftxui::center(panel_->Render()));
    return screen;
  }

  std::string Text() {
    return ScreenText(Draw());
  }

  // The rows the screen asks for, borders and all.
  int Rows() {
    ftxui::Element screen = panel_->Render();
    screen->ComputeRequirement();
    return screen->requirement().min_y;
  }

  // Onto the middle window, wherever the cursor was, and down onto its first
  // row.
  void ToEnabledRows() {
    while (panel_->zone() != LinkZone::kEnabled) {
      panel_->NextZone(1);
    }
    panel_->MoveRow(1);
  }

  std::mt19937 rng_{1};
  std::map<std::string, Skill> skills_;
  std::unique_ptr<CharacterInstance> hero_;
  std::unique_ptr<LinkSkillPanel> panel_;
};

// The three windows, their one header, and a character's own line's skill
// standing in the top one at the level the roster paid for it.
TEST_F(LinkSkillPanelTest, TheScreenNamesItsThreeWindows) {
  std::string text = Text();
  EXPECT_NE(text.find("My Skill"), std::string::npos);
  EXPECT_NE(text.find("Enabled Skills"), std::string::npos);
  EXPECT_NE(text.find("All Skills"), std::string::npos);
  EXPECT_NE(text.find("Name"), std::string::npos);
  EXPECT_NE(text.find("Effect"), std::string::npos);
  // Theirs for free: a Hero reads the warriors' skill off their own level.
  EXPECT_NE(text.find("Invincible Belief"), std::string::npos);
  EXPECT_NE(text.find("Critical Rate"), std::string::npos)
      << "the effect column";
}

// Tab walks the three windows and comes round; Up and Down move inside one.
TEST_F(LinkSkillPanelTest, TabWalksTheWindowsAndTheRingComesRound) {
  EXPECT_EQ(panel_->zone(), LinkZone::kMine);
  panel_->NextZone(1);
  EXPECT_EQ(panel_->zone(), LinkZone::kEnabled);
  panel_->NextZone(1);
  EXPECT_EQ(panel_->zone(), LinkZone::kAll);
  panel_->NextZone(1);
  EXPECT_EQ(panel_->zone(), LinkZone::kMine);
  panel_->NextZone(-1);
  EXPECT_EQ(panel_->zone(), LinkZone::kAll);
}

// An empty preset has one stop -- its bar -- and nothing to act on.
TEST_F(LinkSkillPanelTest, EveryPresetStartsEmptyAndTheBarIsTheOnlyStop) {
  panel_->NextZone(1);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kPreset);
  panel_->MoveRow(1);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kPreset)
      << "there is nowhere to go";
  EXPECT_NE(Text().find("(none equipped)"), std::string::npos);
}

// Add puts the skill under the cursor into the preset being read, and it
// leaves the bottom window as it arrives in the middle one.
TEST_F(LinkSkillPanelTest, AddingMovesASkillBetweenTheTwoLists) {
  panel_->NextZone(1);
  panel_->NextZone(1);
  const Skill* first = panel_->cursor().skill;
  ASSERT_NE(first, nullptr);
  EXPECT_TRUE(panel_->AddSelected());
  EXPECT_EQ(hero().link_skills(StatPreset::kFirst).size(), 1);
  EXPECT_EQ(hero().link_skills(StatPreset::kFirst).at(0), first->name());
  EXPECT_EQ(hero().link_skills(StatPreset::kSecond).size(), 0)
      << "the other presets are untouched";

  ToEnabledRows();
  ASSERT_EQ(panel_->cursor().kind, LinkCursor::Kind::kSkill);
  EXPECT_EQ(panel_->cursor().skill->name(), first->name());
  panel_->RemoveSelected();
  EXPECT_EQ(hero().link_skills(StatPreset::kFirst).size(), 0);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kPreset)
      << "the cursor climbs back to the bar";
}

// Twelve is the ceiling, and the panel says so by refusing rather than by
// silently dropping the thirteenth.
TEST_F(LinkSkillPanelTest, APresetRefusesAThirteenthSkill) {
  for (int i = 0; i < kMaxEquippedLinkSkills; ++i) {
    ASSERT_TRUE(hero().EquipLinkSkill("Filler " + std::to_string(i),
                                      StatPreset::kFirst));
  }
  panel_->NextZone(1);
  panel_->NextZone(1);
  ASSERT_EQ(panel_->cursor().kind, LinkCursor::Kind::kSkill);
  EXPECT_FALSE(panel_->AddSelected());
}

// Left and Right on the bar read another preset, and each holds its own
// twelve.
TEST_F(LinkSkillPanelTest, ThePresetBarPicksWhichTwelveAreRead) {
  ASSERT_TRUE(hero().EquipLinkSkill("Thief's Cunning", StatPreset::kSecond));
  panel_->NextZone(1);
  EXPECT_EQ(panel_->preset(), StatPreset::kFirst);
  EXPECT_NE(Text().find("(none equipped)"), std::string::npos);

  panel_->MovePreset(1);
  EXPECT_EQ(panel_->preset(), StatPreset::kSecond);
  EXPECT_NE(Text().find("Thief's Cunning"), std::string::npos);
  panel_->MovePreset(-1);
  panel_->MovePreset(-1);
  EXPECT_EQ(panel_->preset(), StatPreset::kFirst) << "the bar clamps";
}

// A skill the account has not earned yet draws no row anywhere: the autofill
// keeps it in the preset, and it appears once somebody reaches 70.
TEST_F(LinkSkillPanelTest, ALevelZeroSkillIsNotListed) {
  UseCharacter(JOB_HERO, 50, 2);
  LinkTally tally;
  tally.Record(JOB_BISHOP, 210);
  hero().set_link_tally(tally);
  ASSERT_TRUE(hero().EquipLinkSkill("Thief's Cunning", StatPreset::kFirst));
  ASSERT_TRUE(hero().EquipLinkSkill("Empirical Knowledge", StatPreset::kFirst));
  std::string text = Text();
  EXPECT_EQ(text.find("Thief's Cunning"), std::string::npos);
  EXPECT_EQ(text.find("Invincible Belief"), std::string::npos);
  EXPECT_NE(text.find("(not yet earned)"), std::string::npos);
  EXPECT_NE(text.find("Empirical Knowledge"), std::string::npos);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kNothing)
      << "the top window has no row to stand on";
  ToEnabledRows();
  EXPECT_EQ(panel_->cursor().skill->name(), "Empirical Knowledge");
  panel_->MoveRow(1);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kPreset)
      << "one row, then back to the bar";
}

// Their own line's skill is never on offer: it is held for free and takes
// none of the twelve.
TEST_F(LinkSkillPanelTest, TheirOwnLinesSkillIsNotInTheListToAdd) {
  panel_->NextZone(1);
  panel_->NextZone(1);
  for (int i = 0; i < 4; ++i) {
    if (panel_->cursor().skill != nullptr) {
      EXPECT_NE(panel_->cursor().skill->name(), "Invincible Belief");
    }
    panel_->MoveRow(1);
  }
  // A line nobody has climbed pays nothing, so it is not listed either.
  hero().set_link_tally(LinkTally());
  panel_->Reset();
  panel_->NextZone(1);
  panel_->NextZone(1);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kNothing);
}

// The menus differ by one entry, and the panel reads the choice for the
// controller so three enums do not have to travel with it.
TEST_F(LinkSkillPanelTest, EachWindowRaisesItsOwnMenu) {
  panel_->OpenMenu();
  EXPECT_TRUE(panel_->menu_open());
  EXPECT_EQ(panel_->menu_choice(), LinkMenuChoice::kInspect);
  panel_->MoveMenuCursor(1);
  EXPECT_EQ(panel_->menu_choice(), LinkMenuChoice::kClose)
      << "the top window offers no second action";
  panel_->CloseMenu();

  panel_->NextZone(1);
  panel_->NextZone(1);
  panel_->OpenMenu();
  panel_->MoveMenuCursor(1);
  EXPECT_EQ(panel_->menu_choice(), LinkMenuChoice::kAdd);
  EXPECT_NE(Text().find("Add"), std::string::npos);
}

// Enter on the bar raises the preset menu instead, and Use is shut while the
// autoswap is the thing picking.
TEST_F(LinkSkillPanelTest, TheBarRaisesThePresetMenu) {
  hero().set_autoswap_presets(true);
  panel_->NextZone(1);
  panel_->OpenMenu();
  EXPECT_TRUE(panel_->preset_menu_open());
  EXPECT_FALSE(panel_->menu_open());
  EXPECT_EQ(panel_->preset_menu_selected(), 1)
      << "Use is disabled, so the cursor opens past it";
  EXPECT_NE(Text().find("Use"), std::string::npos);
}

// Combat Orders does not reach the beginner's page, so a link skill stands at
// the rungs the account climbed and no more.
TEST_F(LinkSkillPanelTest, TheBookLendsALinkSkillNothing) {
  Skill orders;
  orders.set_name("Combat Orders");
  SkillPlacement* placement = orders.add_placement();
  placement->set_job_advancement(JOB_ADVANCEMENT_BEGINNER);
  orders.set_max_level(1);
  orders.mutable_base()->set_skill_level_bonus(1);
  skills_["orders"] = orders;

  Character proto;
  proto.set_level(210);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  (*proto.mutable_skill_levels())["Combat Orders"] = 1;
  hero_ = std::make_unique<CharacterInstance>(rng_, std::move(proto));
  hero_->set_account_max_level(210);
  LinkTally tally;
  tally.Record(JOB_BISHOP, 210);
  hero_->set_link_tally(tally);
  panel_ = std::make_unique<LinkSkillPanel>(*hero_, skills_);
  panel_->Reset();

  // Their own line pays three rungs off their own level, and that is all.
  EXPECT_EQ(panel_->SelectedLevel(), 3);
  EXPECT_EQ(Text().find("(+1)"), std::string::npos);
}

// A character with no job line has nothing in the top window and nothing to
// stand on there.
TEST_F(LinkSkillPanelTest, ABeginnerHasNoSkillOfTheirOwn) {
  UseCharacter(JOB_BEGINNER, 10, 0);
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kNothing);
  EXPECT_NE(Text().find("(no job line)"), std::string::npos);
}

// One height whatever the lists hold: twelve slots in the middle window and
// eight rows in the bottom one, drawn blank where nothing fills them, so a
// skill moving between the two does not move the screen under the cursor.
TEST_F(LinkSkillPanelTest, TheScreenIsOneHeightWhateverTheListsHold) {
  int height = Rows();
  panel_->NextZone(1);
  panel_->NextZone(1);
  while (panel_->cursor().skill != nullptr) {
    ASSERT_TRUE(panel_->AddSelected());
    EXPECT_EQ(Rows(), height);
  }
  EXPECT_EQ(panel_->cursor().kind, LinkCursor::Kind::kNothing)
      << "the bottom window has run out";
  EXPECT_NE(Text().find("(nothing left to add)"), std::string::npos);
  EXPECT_EQ(Rows(), height);
}

// The caret is the whole cursor: no band behind the row it stands on.
TEST_F(LinkSkillPanelTest, TheCaretIsTheOnlyCursorMark) {
  ftxui::Screen screen = Draw();
  ASSERT_NE(FindOnScreen(screen, "> Invincible Belief").x, -1);
  EXPECT_FALSE(PixelOf(screen, "Invincible Belief").inverted);
}

// Three windows down the screen, each as wide as its longest effect line.
TEST_F(LinkSkillPanelTest, NoWindowWeldsARowToItsRightBorder) {
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_->Render()).empty());
}
}  // namespace
}  // namespace ms
