#include "src/frontend/screens/familiar_cube_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "src/character/familiar.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/widgets/reroll_prompt.h"

namespace ms {
namespace {

class FamiliarCubePanelTest : public PanelTest {
 protected:
  FamiliarCubePanelTest() {
    familiar_.set_name("Snail");
    familiar_.set_level(kFamiliarMaxLevel);
    FamiliarLine* line = familiar_.add_lines();
    line->set_type(FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40);
    line->set_rank(POTENTIAL_RANK_LEGENDARY);
  }
  Familiar familiar_;
};

// The shelf holds the one card at its price, and the question shows the lines
// a Confirm would replace.
TEST_F(FamiliarCubePanelTest, TheCardRerollsTheLinesShown) {
  FamiliarCubePanel panel;
  panel.SetFamiliar(&familiar_, kFamiliarCubeMeso);
  const std::string shelf = RenderElement(panel.Render(true));
  EXPECT_NE(shelf.find("Red Familiar Card"), std::string::npos) << shelf;
  EXPECT_NE(shelf.find("50,000,000"), std::string::npos);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kNone);
  ASSERT_TRUE(panel.IsConfirming());
  const std::string question = RenderElement(panel.RenderConfirm());
  EXPECT_NE(question.find("Ignore DEF"), std::string::npos) << question;
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kReroll);
  EXPECT_TRUE(panel.IsConfirming()) << "Confirm leaves the window open";
}

// A purse short of the price greys Confirm, so Enter buys nothing.
TEST_F(FamiliarCubePanelTest, AShortPurseCannotReroll) {
  FamiliarCubePanel panel;
  panel.SetFamiliar(&familiar_, kFamiliarCubeMeso - 1);
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_NE(panel.OnEvent(ftxui::Event::Return), RerollAction::kReroll);
}

// Before stays the lines the question opened on and After follows the
// familiar, so the CP change is the running total; closing forgets it.
TEST_F(FamiliarCubePanelTest, BeforeIsTheLinesItOpenedOn) {
  FamiliarCubePanel panel;
  panel.SetFamiliar(&familiar_, 2 * kFamiliarCubeMeso);
  panel.SetCombatPowerDelta(-3);
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.opened_with(), nullptr);

  ASSERT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kReroll);
  Familiar rolled = familiar_;
  rolled.mutable_lines(0)->set_type(FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40);
  panel.SetFamiliar(&rolled, kFamiliarCubeMeso);
  ASSERT_NE(panel.opened_with(), nullptr);
  EXPECT_EQ(panel.opened_with()->lines(0).type(),
            FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40);
  const std::string question = RenderElement(panel.RenderConfirm());
  EXPECT_LT(question.find("Ignore DEF"), question.find("After"));
  EXPECT_GT(question.find("Boss"), question.find("After"));
  EXPECT_NE(question.find("-3"), std::string::npos);

  panel.OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(panel.opened_with(), nullptr);
}

}  // namespace
}  // namespace ms
