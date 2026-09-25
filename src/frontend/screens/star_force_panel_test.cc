#include "src/frontend/screens/star_force_panel.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// More than any attempt costs, so tests not about the price are never affected
// by it.
constexpr int64_t kDeepPurse = 1'000'000'000'000;

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

  // The line drawn below the one containing `needle`, or "" if there is none.
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

  // The cell where the first character of `label` lands. Used for the styling
  // ftxui records per pixel (a focused button's inversion, a greyed one's dim),
  // which the rendered string doesn't contain.
  static ftxui::Pixel PixelOf(StarForcePanel& panel, const std::string& label) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel.Render());
    return ms::PixelOf(screen, label);
  }

  // The column where `needle` starts, or -1 if it isn't there.
  static int ColumnOf(StarForcePanel& panel, const std::string& needle) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel.Render());
    return FindOnScreen(screen, needle).x;
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

// Every button in the game uses one style, so the Enhance button and the
// confirm row below it look alike.
TEST_F(StarForcePanelTest, DrawsButtonsInTheSharedStyle) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  EXPECT_NE(Render(panel).find("[Enhance]"), std::string::npos);
  panel.OnEvent(ftxui::Event::Return);  // opens the confirm prompt
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("[Confirm]"), std::string::npos);
  EXPECT_NE(rendered.find("[Cancel]"), std::string::npos);
}

// At 0 stars the attempt can't destroy the item, so there are two rates instead
// of three.
TEST_F(StarForcePanelTest, RenderShowsTheItemAndItsTwoRates) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Sword"), std::string::npos);
  EXPECT_NE(rendered.find("0"), std::string::npos);
  EXPECT_NE(rendered.find("1"), std::string::npos);
  EXPECT_NE(rendered.find("95%"), std::string::npos);
  EXPECT_NE(rendered.find("5%"), std::string::npos);
  EXPECT_EQ(rendered.find("Destroy"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderFormatsSubPercentRateCorrectly) {
  // At 21 stars the destroy rate is 12.75%.
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/21);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  EXPECT_NE(Render(panel).find("12.75%"), std::string::npos);
}

// At 15 stars destruction is possible, and the three rates are 30%, 67.9% and
// 2.1%: names aligned left in their column, numbers aligned right in theirs.
TEST_F(StarForcePanelTest, RateRowsAlignWhenMixedDecimals) {
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/15);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Success    30%"), std::string::npos);
  EXPECT_NE(rendered.find("Fail     67.9%"), std::string::npos);
  EXPECT_NE(rendered.find("Destroy   2.1%"), std::string::npos);
}

// A staff's next star adds +3 to each stat and +25 to HP and MP, giving two
// columns of different widths: the names line up on the left, the gains on the
// right, and neither row is longer than the other.
TEST_F(StarForcePanelTest, StatRowsStandInTwoColumns) {
  EquipPrototype proto;
  proto.set_name("Frozen Staff");
  proto.set_required_level(120);
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_equip_type(EQUIP_TYPE_STAFF);
  Equip state;
  state.set_stars(14);
  EquipInstance item(proto, state);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("STR   +3"), std::string::npos);
  EXPECT_NE(rendered.find("HP   +25"), std::string::npos);
  EXPECT_EQ(ColumnOf(panel, "STR"), ColumnOf(panel, "HP"))
      << "the names start in one column";
  // "+3" is two cells and "+25" three, so ending together puts the shorter one
  // a cell further right.
  EXPECT_EQ(ColumnOf(panel, "+3") + 2, ColumnOf(panel, "+25") + 3)
      << "and the gains end in one column";
}

TEST_F(StarForcePanelTest, AtMaxStarsShowsMaxMessageNotRates) {
  // A level 10 item: MaxStarsForLevel(10) is 5, so it is placed at 5 stars.
  EquipInstance item = MakeItem(/*required_level=*/10, /*stars=*/5);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("(max)"), std::string::npos);
  EXPECT_NE(rendered.find("Maximum"), std::string::npos);
  EXPECT_EQ(rendered.find("Success"), std::string::npos);
  EXPECT_EQ(rendered.find("Enter"), std::string::npos);
}

// The attempt's cost, in its own section between the odds and the button, the
// last thing read before pressing. A level 150 item's first star costs 136,000.
TEST_F(StarForcePanelTest, RenderShowsWhatTheAttemptCosts) {
  EquipInstance item = MakeItem(/*required_level=*/150, /*stars=*/0);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  std::string rendered = Render(panel);
  size_t price = rendered.find("136,000");
  ASSERT_NE(price, std::string::npos) << rendered;
  EXPECT_LT(rendered.find("Success"), price) << "the price is above the odds";
  EXPECT_LT(price, rendered.find("Enhance")) << "the price is below the button";
}

// The purse is shown above the price, labelled, since this screen is shown
// alone and nothing else on it says what the player has to spend.
TEST_F(StarForcePanelTest, ThePurseStandsOverThePrice) {
  EquipInstance item = MakeItem(/*required_level=*/150, /*stars=*/0);
  StarForcePanel panel;
  panel.SetItem(&item, 98'765'432'100);
  std::string rendered = Render(panel);
  size_t cost = rendered.find("Cost");
  size_t held = rendered.find("Held");
  ASSERT_NE(held, std::string::npos) << rendered;
  EXPECT_LT(held, cost) << "the purse is read first";
  EXPECT_NE(rendered.find("98,765,432,100"), std::string::npos);
  EXPECT_LT(cost, rendered.find("Enhance"));
  // The purse is just information, not a refusal, so only the price ever turns
  // red.
  EXPECT_NE(PixelOf(panel, "98,765,432,100").foreground_color, kRed);
}

// The name and the star being attempted form one heading, so the rules start
// below both rather than between them.
TEST_F(StarForcePanelTest, NoRuleThroughTheHeading) {
  EquipInstance climbing = MakeItem(/*required_level=*/150, /*stars=*/3);
  StarForcePanel panel;
  panel.SetItem(&climbing, kDeepPurse);
  std::string under = LineAfter(Render(panel), "Sword");
  EXPECT_NE(under.find("3"), std::string::npos) << under;
  EXPECT_EQ(under.find("─"), std::string::npos) << "a rule split it";

  // The same on the screen for an item at its maximum stars.
  EquipInstance topped = MakeItem(/*required_level=*/10, /*stars=*/5);
  panel.SetItem(&topped, kDeepPurse);
  under = LineAfter(Render(panel), "Sword");
  EXPECT_NE(under.find("(max)"), std::string::npos) << under;
}

// --- the foot: [Enhance] [Cancel] ---

// Leaving is a button as well as a key, and the cursor starts on the button the
// player came to press.
TEST_F(StarForcePanelTest, TheFootIsEnhanceThenCancelAndTheCursorMoves) {
  EquipInstance item = MakeItem(/*required_level=*/150, /*stars=*/0);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  std::string rendered = Render(panel);
  EXPECT_LT(rendered.find("[Enhance]"), rendered.find("[Cancel]"));
  EXPECT_TRUE(PixelOf(panel, "[Enhance]").inverted);
  EXPECT_FALSE(PixelOf(panel, "[Cancel]").inverted);
  panel.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_TRUE(PixelOf(panel, "[Cancel]").inverted);
  EXPECT_FALSE(PixelOf(panel, "[Enhance]").inverted);
  panel.OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_TRUE(PixelOf(panel, "[Enhance]").inverted);
}

// Reported like a confirmed attempt: the panel returns the choice and the
// caller closes the screen.
TEST_F(StarForcePanelTest, CancelIsReportedAndOpensNoPrompt) {
  EquipInstance item = MakeItem(/*required_level=*/150, /*stars=*/0);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  panel.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
  EXPECT_FALSE(panel.IsConfirming());
}

// A player who can't pay is told so at the price and left on the only button
// that does anything, instead of being led through a confirmation for an
// attempt that could never happen.
TEST_F(StarForcePanelTest, APurseTooThinGreysEnhanceAndParksOnCancel) {
  // A level 150 item's first star costs 136,000.
  EquipInstance item = MakeItem(/*required_level=*/150, /*stars=*/0);
  StarForcePanel panel;
  panel.SetItem(&item, 135999);
  EXPECT_EQ(PixelOf(panel, "136,000").foreground_color, kRed);
  EXPECT_NE(PixelOf(panel, "135,999").foreground_color, kRed)
      << "the purse is a fact, not a refusal";
  EXPECT_TRUE(PixelOf(panel, "[Enhance]").dim);
  EXPECT_FALSE(PixelOf(panel, "[Enhance]").inverted);
  EXPECT_TRUE(PixelOf(panel, "[Cancel]").inverted);

  // Left can't reach the greyed button, and Enter leaves instead of asking.
  panel.OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_TRUE(PixelOf(panel, "[Cancel]").inverted);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
  EXPECT_FALSE(panel.IsConfirming());

  // With a purse that covers it, the screen is back to normal. Not exactly
  // 136,000, since two rows showing the same number could let the check match
  // the purse.
  panel.SetItem(&item, 500'000);
  EXPECT_NE(PixelOf(panel, "136,000").foreground_color, kRed);
  EXPECT_FALSE(PixelOf(panel, "[Enhance]").dim);
  EXPECT_TRUE(PixelOf(panel, "[Enhance]").inverted);
}

// --- the result window's colour ---

// Which of the three outcomes happened is the point of the window, so the frame
// shows it before the text does. The rules match the border, since a steel-blue
// rule across a gold window would look like two windows.
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

  // Nothing changed, so nothing is coloured. The same applies to an attempt
  // that was never made, which is why it must not fall through to the destroy
  // branch and tell the player an item they still have is gone.
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

// The window and the result card measure themselves separately, so both have to
// request the margin.
TEST_F(StarForcePanelTest, NeitherWindowWeldsTextToItsBorder) {
  EquipInstance item = MakeItem(150, 17);
  StarForcePanel panel;
  panel.SetItem(&item, kDeepPurse);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
  StarForceResult result;
  result.outcome = kStarForceSuccess;
  result.equip_name = "Fafnir Battle Cleaver";
  result.stars_before = 17;
  result.stars_after = 18;
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.RenderResult(result)).empty());
}
}  // namespace
}  // namespace ms
