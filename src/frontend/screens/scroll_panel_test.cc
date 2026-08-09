#include "src/frontend/screens/scroll_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

class ScrollPanelTest : public PanelTest {
 protected:
  static std::map<std::string, Scroll> MakeScrolls() {
    std::map<std::string, Scroll> scrolls;
    Scroll& a = scrolls["AAA Scroll"];
    a.set_name("AAA Scroll");
    a.set_success_rate(10);
    a.set_tier(SCROLL_TIER_1);
    a.set_scroll_type(SCROLL_TYPE_ATT);
    a.mutable_stats()->set_attack(5);
    a.set_trace_cost(20);
    a.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    Scroll& z = scrolls["ZZZ Scroll"];
    z.set_name("ZZZ Scroll");
    z.set_success_rate(60);
    z.set_tier(SCROLL_TIER_2);
    z.set_scroll_type(SCROLL_TYPE_DEX);
    z.set_trace_cost(300);
    z.add_applicable_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
    return scrolls;
  }

  static std::string Render(ScrollPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }

  // Traces the character is carrying, so a test can put the balance where it
  // needs it before the panel reads it.
  void GiveTraces(int count) {
    ItemPrototype trace;
    trace.set_name(kSpellTraceName);
    trace.set_category(ITEM_CATEGORY_ETC);
    trace.set_max_stack(30000);
    c_.AddStackable(trace, count);
  }

  std::map<std::string, Scroll> scrolls_ = MakeScrolls();
  ScrollPanel panel_{c_, scrolls_};
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

TEST_F(ScrollPanelTest, RenderShowsStat) {
  EXPECT_NE(Render(panel_).find("+5 ATT"), std::string::npos);
}

TEST_F(ScrollPanelTest, ArrowDownMovesSelection) {
  Render(panel_);  // populate entries_ so the menu knows its size
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);
}

// The list is the whole screen, with no tab bar over it, so the ends meet.
TEST_F(ScrollPanelTest, ArrowUpFromTheFirstScrollWrapsToTheLast) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_.selected(), 1);
}

TEST_F(ScrollPanelTest, ArrowDownFromTheLastScrollWrapsToTheFirst) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 0);
}

TEST_F(ScrollPanelTest, ArrowDownSelectsTheSecondScroll) {
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

TEST_F(ScrollPanelTest, SetFilterRejectsAnItemWithNoScrolls) {
  EquipPrototype proto;
  proto.set_required_level(1);  // tier 1
  proto.add_equip_job_categories(
      EQUIP_JOB_CATEGORY_PIRATE);  // no pirate scrolls
  EXPECT_FALSE(panel_.SetFilterForPrototype(proto));
  // Filter unchanged; original first entry still selected.
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, SetFilterRejectsATierMismatch) {
  EquipPrototype proto;
  proto.set_required_level(75);  // tier 2; only ZZZ is tier 2 but it's bowman
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(panel_.SetFilterForPrototype(proto));
}

TEST_F(ScrollPanelTest, SetFilterResetsTheSelection) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);

  EquipPrototype proto;
  proto.set_required_level(1);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  panel_.SetFilterForPrototype(proto);
  EXPECT_EQ(panel_.selected(), 0);
}

// --- the Cost column and the balance ---

// Display columns, not bytes: the scroll glyph is four bytes wide and two
// columns wide, which is exactly the confusion this file has to keep out of
// the Cost column.
int DisplayColumns(const std::string& s) {
  int width = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    int bytes = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
    width += bytes == 4 ? 2 : 1;  // the emoji is the only wide glyph here
    i += bytes;
  }
  return width;
}

TEST_F(ScrollPanelTest, EachRowCarriesItsTraceCost) {
  std::string rendered = Render(panel_);
  EXPECT_NE(rendered.find("20 📜"), std::string::npos);
  EXPECT_NE(rendered.find("Cost"), std::string::npos);
}

// The one that catches a byte-padded cost cell: the heading and the number
// under it have to end in the same column.
TEST_F(ScrollPanelTest, TheCostColumnLinesUpWithItsHeading) {
  std::string rendered = Render(panel_);
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= rendered.size()) {
    size_t end = rendered.find('\n', start);
    if (end == std::string::npos) {
      end = rendered.size();
    }
    lines.push_back(rendered.substr(start, end - start));
    start = end + 1;
  }
  std::string header;
  std::string row;
  for (const std::string& line : lines) {
    if (line.find("Cost") != std::string::npos) {
      header = line;
    }
    if (line.find("20 📜") != std::string::npos) {
      row = line;
    }
  }
  ASSERT_FALSE(header.empty());
  ASSERT_FALSE(row.empty());
  EXPECT_EQ(DisplayColumns(header.substr(0, header.find("Cost") + 4)),
            DisplayColumns(row.substr(0, row.find("📜") + 4)));
}

TEST_F(ScrollPanelTest, TheTitleShowsWhatThePlayerIsHolding) {
  EXPECT_NE(Render(panel_).find("0 📜"), std::string::npos);
  GiveTraces(1240);
  EXPECT_NE(Render(panel_).find("1,240 📜"), std::string::npos);
}

TEST_F(ScrollPanelTest, AffordabilityFollowsTheBalance) {
  EXPECT_EQ(panel_.selected_scroll().trace_cost(), 20);
  EXPECT_FALSE(panel_.CanAffordSelected());
  GiveTraces(19);
  EXPECT_FALSE(panel_.CanAffordSelected());
  GiveTraces(1);  // exactly the price
  EXPECT_TRUE(panel_.CanAffordSelected());
}

// The name column gave up its width to Cost, so a name past it is cut rather
// than allowed to shove the other columns along. That it also SLIDES while
// selected is ScrollingWindow's promise and is tested with it, in marquee_test
// -- catching it here would mean sleeping out the marquee's pause.
TEST_F(ScrollPanelTest, ALongNameIsCutToItsColumn) {
  std::map<std::string, Scroll> scrolls;
  Scroll& s = scrolls["long"];
  s.set_name("100% Clean Slate");
  s.set_success_rate(100);
  s.set_tier(SCROLL_TIER_1);
  s.set_trace_cost(20);
  s.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  ScrollPanel panel(c_, scrolls);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("100% Clean"), std::string::npos);
  EXPECT_EQ(rendered.find("100% Clean Slate"), std::string::npos);
}

// The panel is the first of two refusals -- the controller will not spend what
// is not there either -- so this has to be asserted here, where the controller
// cannot cover for it.
TEST_F(ScrollPanelTest, TheConfirmWindowWillNotAnswerYesUnpaid) {
  panel_.OnEvent(ftxui::Event::Return);  // open the confirm window
  ASSERT_TRUE(panel_.IsConfirming());
  panel_.OnEvent(ftxui::Event::Return);  // answer yes with nothing to pay with
  EXPECT_FALSE(panel_.TakeConfirmed());

  GiveTraces(20);
  panel_.OnEvent(ftxui::Event::Return);
  panel_.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(panel_.TakeConfirmed());
}

TEST_F(ScrollPanelTest, TheConfirmWindowShowsTheCostAndWhatIsLeft) {
  GiveTraces(100);
  panel_.OnEvent(ftxui::Event::Return);
  std::string rendered = Render(panel_);
  EXPECT_NE(rendered.find("Confirm"), std::string::npos);
  EXPECT_NE(rendered.find("AAA Scroll"), std::string::npos);
  EXPECT_NE(rendered.find("20 📜"), std::string::npos) << "the cost";
  EXPECT_NE(rendered.find("80 📜"), std::string::npos) << "what is left after";
}

TEST_F(ScrollPanelTest, TheConfirmWindowSaysWhenTheTracesFallShort) {
  GiveTraces(5);
  panel_.OnEvent(ftxui::Event::Return);
  EXPECT_NE(Render(panel_).find("not enough"), std::string::npos);
}

// The window names the item as well as the scroll, which is the whole reason
// it replaced a bare button row.
TEST_F(ScrollPanelTest, TheConfirmWindowNamesTheItemBeingScrolled) {
  EquipPrototype sword;
  sword.set_name("Long Sword");
  sword.set_required_level(10);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  ASSERT_TRUE(panel_.SetFilterForPrototype(sword));
  panel_.OnEvent(ftxui::Event::Return);
  EXPECT_NE(Render(panel_).find("Long Sword"), std::string::npos);
}

}  // namespace
}  // namespace ms
