#include "src/frontend/screens/star_force_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class StarForcePanelTest : public PanelTest {
 protected:
  EquipInstance MakeItem(int required_level, int stars) {
    EquipPrototype proto;
    proto.set_name("Sword");
    proto.set_required_level(required_level);
    Equip state;
    state.set_stars(stars);
    return EquipInstance(proto, state);
  }

  // The line drawn under the one holding `needle`, or "" if there is none.
  static std::string LineAfter(const std::string& rendered,
                               const std::string& needle) {
    size_t at = rendered.find(needle);
    size_t eol = at == std::string::npos ? at : rendered.find('\n', at);
    if (eol == std::string::npos) {
      return "";
    }
    size_t next = rendered.find('\n', eol + 1);
    return rendered.substr(eol + 1, next - eol - 1);
  }

  static std::string Render(StarForcePanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(StarForcePanelTest, NullItemShowsPlaceholder) {
  StarForcePanel panel;
  EXPECT_NE(Render(panel).find("(no item)"), std::string::npos);
}

// Every button the game draws is the one style, so the enhance button and the
// confirm row below it read alike.
TEST_F(StarForcePanelTest, DrawsButtonsInTheSharedStyle) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("[Enhance]"), std::string::npos);
  panel.OnEvent(ftxui::Event::Return);  // opens the confirm prompt
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("[Confirm]"), std::string::npos);
  EXPECT_NE(rendered.find("[Cancel]"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderShowsNameAndUpgradeArrow) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Sword"), std::string::npos);
  EXPECT_NE(rendered.find("0"), std::string::npos);
  EXPECT_NE(rendered.find("1"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderShowsSuccessAndFailRates) {
  // At 0★: success=95%, fail=5%.
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("95%"), std::string::npos);
  EXPECT_NE(rendered.find("5%"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderHidesDestroyRowWhenZero) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(Render(panel).find("Destroy"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderShowsDestroyRowWhenNonZero) {
  // At 15★: destroy=2.1%.
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/15);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Destroy"), std::string::npos);
  EXPECT_NE(rendered.find("2.1%"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderFormatsSubPercentRateCorrectly) {
  // At 21★: destroy=12.75%.
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/21);
  StarForcePanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("12.75%"), std::string::npos);
}

TEST_F(StarForcePanelTest, RateRowsAlignWhenMixedDecimals) {
  // At 15★: success=30% (no decimals), fail=67.9%, destroy=2.1% (both with
  // decimals). All three rate strings must be padded to the same rendered width
  // so hcenter places them at identical x offsets.
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/15);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  // All three rate lines appear; spot-check exact padded strings.
  EXPECT_NE(rendered.find("Success  30%  "), std::string::npos);
  EXPECT_NE(rendered.find("Fail     67.9%"), std::string::npos);
  EXPECT_NE(rendered.find("Destroy  2.1% "), std::string::npos);
}

TEST_F(StarForcePanelTest, AtMaxStarsShowsMaxMessageNotRates) {
  // Level 10 item: MaxStarsForLevel(10) == 5; place it at 5★.
  EquipInstance item = MakeItem(/*required_level=*/10, /*stars=*/5);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("(max)"), std::string::npos);
  EXPECT_NE(rendered.find("Maximum"), std::string::npos);
  EXPECT_EQ(rendered.find("Success"), std::string::npos);
  EXPECT_EQ(rendered.find("Enter"), std::string::npos);
}

// What the attempt takes, in its own section between the odds and the button
// -- the last thing read before pressing. A level 150 item's first star is
// 136,000.
TEST_F(StarForcePanelTest, RenderShowsWhatTheAttemptCosts) {
  EquipInstance item = MakeItem(/*required_level=*/150, /*stars=*/0);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  size_t price = rendered.find("136,000");
  ASSERT_NE(price, std::string::npos) << rendered;
  EXPECT_LT(rendered.find("Success"), price) << "the price is above the odds";
  EXPECT_LT(price, rendered.find("Enhance")) << "the price is below the button";
}

// The name and the star it is going for are one heading, so the rules start
// below the two of them rather than between.
TEST_F(StarForcePanelTest, NoRuleThroughTheHeading) {
  EquipInstance climbing = MakeItem(/*required_level=*/150, /*stars=*/3);
  StarForcePanel panel;
  panel.SetItem(&climbing);
  std::string under = LineAfter(Render(panel), "Sword");
  EXPECT_NE(under.find("3"), std::string::npos) << under;
  EXPECT_EQ(under.find("─"), std::string::npos) << "a rule split it";

  // And the same on the screen an item at its last star gets.
  EquipInstance topped = MakeItem(/*required_level=*/10, /*stars=*/5);
  panel.SetItem(&topped);
  under = LineAfter(Render(panel), "Sword");
  EXPECT_NE(under.find("(max)"), std::string::npos) << under;
}

// --- the result window's colour ---

// Which of the three happened is the whole point of the window, so it is said
// in the frame before it is said in words. The rules go with the border: a
// steel-blue seam across a gold window would read as two windows.
TEST_F(StarForcePanelTest, TheResultWindowTakesTheOutcomesColour) {
  StarForcePanel panel;
  StarForceResult r;
  r.equip_name = "Sword";
  r.stars_before = 3;
  r.stars_after = 4;

  r.outcome = kStarForceSuccess;
  EXPECT_EQ(BorderColor(panel.RenderResult(r)), kYellow);
  EXPECT_EQ(InnerRuleColor(panel.RenderResult(r)), kYellow);

  r.outcome = kStarForceDestroy;
  EXPECT_EQ(BorderColor(panel.RenderResult(r)), kRed);
  EXPECT_EQ(InnerRuleColor(panel.RenderResult(r)), kRed);

  // Nothing changed, so nothing is coloured for it. Same for an attempt that
  // was never made -- which is why it must not fall through to the destroy
  // branch and tell the player an item they still own is gone.
  r.outcome = kStarForceFail;
  EXPECT_EQ(BorderColor(panel.RenderResult(r)), kTheme);

  r.outcome = kStarForceNoMeso;
  EXPECT_EQ(BorderColor(panel.RenderResult(r)), kTheme);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, panel.RenderResult(r));
  std::string rendered = screen.ToString();
  EXPECT_NE(rendered.find("NOT ENOUGH MESO"), std::string::npos);
  EXPECT_EQ(rendered.find("DESTROYED"), std::string::npos);
}

}  // namespace
}  // namespace ms
