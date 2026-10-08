#include "src/frontend/screens/familiar_switch_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "src/character/familiar.h"
#include "src/frontend/testing/panel_test_base.h"

namespace ms {
namespace {

class FamiliarSwitchPanelTest : public PanelTest {
 protected:
  std::string Render(const FamiliarSwitchPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, panel.Render(true));
    return screen.ToString();
  }
  FamiliarBook book_;
};

// The whole roster is listed, the preset's familiars ticked, and [Close] last.
TEST_F(FamiliarSwitchPanelTest, ListsTheRosterAndTicksWhatIsInUse) {
  FamiliarSwitchPanel panel;
  panel.Reset("Snail");
  panel.SetFamiliars(&book_, {"Snail", "Blue Snail", "Red Snail"});
  const std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("In Use"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Mutant Orange Mushroom"), std::string::npos);
  EXPECT_NE(rendered.find("[Close]"), std::string::npos);
  int ticks = 0;
  for (size_t at = rendered.find("✓"); at != std::string::npos;
       at = rendered.find("✓", at + 1)) {
    ++ticks;
  }
  EXPECT_EQ(ticks, 3);
}

// Enter asks first, and only a confirmed question equips. The row's own
// familiar asks nothing, and Up from the top lands on [Close].
TEST_F(FamiliarSwitchPanelTest, EnterAsksBeforeEquipping) {
  FamiliarSwitchPanel panel;
  panel.Reset("Snail");
  panel.SetFamiliars(&book_, {"Snail", "Blue Snail", "Red Snail"});
  EXPECT_EQ(panel.selected(), "Snail");
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return),
            FamiliarSwitchPanel::Action::kNone);
  EXPECT_FALSE(panel.IsConfirming()) << "switching it for itself";

  panel.OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(panel.selected(), "Blue Snail");
  panel.OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(panel.IsConfirming());
  EXPECT_NE(RenderElement(panel.RenderConfirm()).find("Equip Blue Snail?"),
            std::string::npos);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return),
            FamiliarSwitchPanel::Action::kEquip);

  panel.Reset("Snail");
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.selected(), "");
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return),
            FamiliarSwitchPanel::Action::kClose);
}

}  // namespace
}  // namespace ms
