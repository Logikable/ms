#include "src/frontend/scroll_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

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
    a.set_scroll_type(SCROLL_TYPE_ATT);
    a.mutable_stats()->set_attack(5);
    Scroll& z = scrolls["ZZZ Scroll"];
    z.set_name("ZZZ Scroll");
    z.set_success_rate(60);
    z.set_tier(SCROLL_TIER_2);
    z.set_scroll_type(SCROLL_TYPE_DEX);
    return scrolls;
  }

  static std::string Render(ScrollPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }

  std::map<std::string, Scroll> scrolls_ = MakeScrolls();
  ScrollPanel panel_{scrolls_};
};

TEST_F(ScrollPanelTest, DefaultSelectionIsZero) {
  EXPECT_EQ(panel_.selected(), 0);
}

TEST_F(ScrollPanelTest, SelectedScrollSortsByTypeThenRate) {
  // AAA is SCROLL_TYPE_ATT (1), ZZZ is SCROLL_TYPE_DEX (4).
  // Type sort puts AAA first despite its lower rate.
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, RenderShowsScrollName) {
  EXPECT_NE(Render(panel_).find("AAA Scroll"), std::string::npos);
}

TEST_F(ScrollPanelTest, RenderShowsSuccessRate) {
  EXPECT_NE(Render(panel_).find("10%"), std::string::npos);
}

TEST_F(ScrollPanelTest, RenderShowsTier) {
  EXPECT_NE(Render(panel_).find("T1"), std::string::npos);
}

TEST_F(ScrollPanelTest, RenderShowsStat) {
  EXPECT_NE(Render(panel_).find("+5 ATT"), std::string::npos);
}

TEST_F(ScrollPanelTest, ArrowDownMovesSelection) {
  Render(panel_);  // populate entries_ so the menu knows its size
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);
}

TEST_F(ScrollPanelTest, ArrowUpClampsAtFirst) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_.selected(), 0);
}

}  // namespace
}  // namespace ms
