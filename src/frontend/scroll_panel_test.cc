#include "src/frontend/scroll_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/protos/equip.pb.h"
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
    a.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    Scroll& z = scrolls["ZZZ Scroll"];
    z.set_name("ZZZ Scroll");
    z.set_success_rate(60);
    z.set_tier(SCROLL_TIER_2);
    z.set_scroll_type(SCROLL_TYPE_DEX);
    z.add_applicable_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
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

TEST_F(ScrollPanelTest, ArrowDownClampsAtLast) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);
}

TEST_F(ScrollPanelTest, SelectedScrollAfterArrowDownReturnsSecondScroll) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
}

TEST_F(ScrollPanelTest, SetFilterChangesSelectedScroll) {
  std::vector<const Scroll*> filter = {&scrolls_["ZZZ Scroll"]};
  panel_.SetFilter(filter);
  Render(panel_);
  EXPECT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
}

TEST_F(ScrollPanelTest, SetFilterResetsSelectionToZero) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);

  std::vector<const Scroll*> filter = {&scrolls_["AAA Scroll"]};
  panel_.SetFilter(filter);
  EXPECT_EQ(panel_.selected(), 0);
}

TEST_F(ScrollPanelTest, SetFilterForPrototypeReturnsTrueForMatch) {
  EquipPrototype proto;
  proto.set_required_level(1);  // tier 1
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_TRUE(panel_.SetFilterForPrototype(proto));
  Render(panel_);
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, SetFilterForPrototypeReturnsFalseForNoMatch) {
  EquipPrototype proto;
  proto.set_required_level(1);  // tier 1
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_PIRATE);  // no pirate scrolls
  EXPECT_FALSE(panel_.SetFilterForPrototype(proto));
  // Filter unchanged; original first entry still selected.
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, SetFilterForPrototypeTierMismatchReturnsFalse) {
  EquipPrototype proto;
  proto.set_required_level(75);  // tier 2; only ZZZ is tier 2 but it's bowman
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(panel_.SetFilterForPrototype(proto));
}

TEST_F(ScrollPanelTest, SetFilterForPrototypeResetsSelectionToZero) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);

  EquipPrototype proto;
  proto.set_required_level(1);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  panel_.SetFilterForPrototype(proto);
  EXPECT_EQ(panel_.selected(), 0);
}

}  // namespace
}  // namespace ms
