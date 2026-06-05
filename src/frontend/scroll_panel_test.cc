#include "src/frontend/scroll_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

class ScrollPanelTest : public testing::Test {
 protected:
  static std::map<std::string, Scroll> MakeScrolls() {
    std::map<std::string, Scroll> scrolls;
    Scroll& a = scrolls["AAA Scroll"];
    a.set_name("AAA Scroll");
    a.set_success_rate(10);
    a.set_tier(SCROLL_TIER_1);
    a.mutable_stats()->set_attack(5);
    Scroll& z = scrolls["ZZZ Scroll"];
    z.set_name("ZZZ Scroll");
    z.set_success_rate(60);
    z.set_tier(SCROLL_TIER_2);
    return scrolls;
  }

  static std::string Render(ftxui::Component component) {
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, component->Render());
    return screen.ToString();
  }

  std::map<std::string, Scroll> scrolls_ = MakeScrolls();
  ScrollPanel panel_{scrolls_};
};

TEST_F(ScrollPanelTest, DefaultSelectionIsZero) {
  EXPECT_EQ(panel_.selected(), 0);
}

TEST_F(ScrollPanelTest, SelectedScrollIsFirstByKeyOrder) {
  // std::map orders by key; "AAA" < "ZZZ" so AAA is first.
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, RenderShowsScrollName) {
  ftxui::Component c = panel_.MakeComponent();
  EXPECT_NE(Render(c).find("AAA Scroll"), std::string::npos);
}

TEST_F(ScrollPanelTest, RenderShowsSuccessRate) {
  ftxui::Component c = panel_.MakeComponent();
  EXPECT_NE(Render(c).find("10%"), std::string::npos);
}

TEST_F(ScrollPanelTest, RenderShowsTier) {
  ftxui::Component c = panel_.MakeComponent();
  EXPECT_NE(Render(c).find("T1"), std::string::npos);
}

TEST_F(ScrollPanelTest, RenderShowsStat) {
  ftxui::Component c = panel_.MakeComponent();
  EXPECT_NE(Render(c).find("+5 ATT"), std::string::npos);
}

TEST_F(ScrollPanelTest, ArrowDownMovesSelection) {
  ftxui::Component c = panel_.MakeComponent();
  Render(c);  // populate entries_ so the menu knows its size
  c->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);
}

TEST_F(ScrollPanelTest, ArrowUpClampsAtFirst) {
  ftxui::Component c = panel_.MakeComponent();
  Render(c);
  c->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_.selected(), 0);
}

}  // namespace
}  // namespace ms
