#include "src/frontend/screens/hyper_stat_level_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {
namespace {

// The [Confirm]/[Cancel] mechanics belong to confirm_prompt_test; these cover
// what this dialog says and what it does with an answer it cannot honour.
class HyperStatLevelPanelTest : public testing::Test {
 protected:
  static std::string Render(const HyperStatLevelPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(48),
                                                 ftxui::Dimension::Fixed(12));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(HyperStatLevelPanelTest, ShowsTheRungAndWhatItCosts) {
  HyperStatLevelPanel panel;
  panel.Reset("Critical Damage", 3, 8, 40);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Critical Damage"), std::string::npos);
  EXPECT_NE(rendered.find("Lv 3"), std::string::npos);
  EXPECT_NE(rendered.find("Lv 4"), std::string::npos);
  EXPECT_NE(rendered.find("Current Points: 40"), std::string::npos);
  EXPECT_NE(rendered.find("Required Points: 8"), std::string::npos);
  EXPECT_TRUE(panel.affordable());
}

// An allocation that cannot cover the rung gets the question asked and
// refused, rather than a dialog that closes as though something happened.
TEST_F(HyperStatLevelPanelTest, ARungWithoutThePointsCannotBeConfirmed) {
  HyperStatLevelPanel panel;
  panel.Reset("Arcane Force", 9, 35, 4);
  EXPECT_FALSE(panel.affordable());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  // And it is still up, saying the same thing.
  EXPECT_NE(Render(panel).find("Arcane Force"), std::string::npos);
  // Leaving still works.
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(HyperStatLevelPanelTest, ARungThePointsCoverConfirms) {
  HyperStatLevelPanel panel;
  panel.Reset("STR", 0, 1, 1);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

}  // namespace
}  // namespace ms
