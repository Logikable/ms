#include "src/frontend/widgets/amount_selector.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// Empties the textbox, leaving focus on it. Digits append rather than replace,
// so typing a fresh number means clearing first.
void Clear(AmountSelector* sel) {
  while (sel->value() > 0) {
    sel->OnEvent(ftxui::Event::Backspace);
  }
}

// The selector's foot is the game's one confirm row, not a lookalike.
TEST(AmountSelectorTest, DrawsTheSharedConfirmRow) {
  AmountSelector sel;
  sel.Reset(10);
  ftxui::Element element = sel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);
  std::string rendered = screen.ToString();
  EXPECT_NE(rendered.find("[Confirm]"), std::string::npos);
  EXPECT_NE(rendered.find("[Cancel]"), std::string::npos);
  EXPECT_NE(rendered.find("[1]"), std::string::npos);
  EXPECT_NE(rendered.find("[MAX]"), std::string::npos);
}

TEST(AmountSelectorTest, DefaultsToMax) {
  AmountSelector sel;
  sel.Reset(10);
  EXPECT_EQ(sel.value(), 10);
}

TEST(AmountSelectorTest, TextboxSelectedByDefaultSoDigitsEdit) {
  AmountSelector sel;
  sel.Reset(100);                        // value starts at 100
  sel.OnEvent(ftxui::Event::Backspace);  // only edits if the textbox is on
  EXPECT_EQ(sel.value(), 10);            // 100 -> 10
}

TEST(AmountSelectorTest, OneButtonSetsValueToOne) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(sel.value(), 1);
}

TEST(AmountSelectorTest, MaxButtonRestoresMax) {
  AmountSelector sel;
  sel.Reset(100);
  Clear(&sel);
  sel.OnEvent(ftxui::Event::ArrowRight);  // textbox -> [MAX]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(sel.value(), 100);
}

TEST(AmountSelectorTest, ZeroIsReachableByHand) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::Backspace);
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 0);
}

TEST(AmountSelectorTest, DigitsEditValue) {
  AmountSelector sel;
  sel.Reset(100);
  Clear(&sel);
  sel.OnEvent(ftxui::Event::Character('2'));
  sel.OnEvent(ftxui::Event::Character('5'));
  EXPECT_EQ(sel.value(), 25);
}

TEST(AmountSelectorTest, DigitsClampToMax) {
  AmountSelector sel;
  sel.Reset(100);
  Clear(&sel);
  sel.OnEvent(ftxui::Event::Character('9'));
  sel.OnEvent(ftxui::Event::Character('9'));
  sel.OnEvent(ftxui::Event::Character('9'));  // 999 -> clamp 100
  EXPECT_EQ(sel.value(), 100);
}

TEST(AmountSelectorTest, BackspaceDeletesLastDigit) {
  AmountSelector sel;
  sel.Reset(100);
  Clear(&sel);
  sel.OnEvent(ftxui::Event::Character('2'));
  sel.OnEvent(ftxui::Event::Character('5'));  // 25
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 2);
}

TEST(AmountSelectorTest, DigitsIgnoredWhenAButtonIsSelected) {
  AmountSelector sel;
  sel.Reset(100);                             // value 100, textbox selected
  sel.OnEvent(ftxui::Event::ArrowLeft);       // textbox -> [1] button
  sel.OnEvent(ftxui::Event::Character('5'));  // ignored off the textbox
  EXPECT_EQ(sel.value(), 100);
}

TEST(AmountSelectorTest, DownFromTextboxActivatesConfirm) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(sel.TakeConfirmed());
  EXPECT_FALSE(sel.TakeConfirmed());  // resets after read
}

TEST(AmountSelectorTest, RightFromConfirmActivatesCancel) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::ArrowDown);   // textbox -> [Confirm]
  sel.OnEvent(ftxui::Event::ArrowRight);  // [Confirm] -> [Cancel]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(sel.TakeCancelled());
  EXPECT_FALSE(sel.TakeConfirmed());
}

TEST(AmountSelectorTest, UpFromButtonReturnsToTextbox) {
  AmountSelector sel;
  sel.Reset(100);
  sel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  sel.OnEvent(ftxui::Event::ArrowUp);    // [Confirm] -> textbox
  sel.OnEvent(ftxui::Event::Backspace);  // edits only if textbox is on
  EXPECT_EQ(sel.value(), 10);
}

TEST(AmountSelectorTest, EscapeCancels) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(sel.TakeCancelled());
}

// --- opening amount and hidden shortcuts ---

std::string RenderSelector(const AmountSelector& sel) {
  ftxui::Element element = sel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);
  return screen.ToString();
}

TEST(AmountSelectorTest, OpensOnTheAmountItIsGiven) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1, QuickPicks::kShown);
  EXPECT_EQ(sel.value(), 1);
}

// A shop can offer one of something the player cannot afford, so the opening
// amount has to survive a max of zero rather than sit above it.
TEST(AmountSelectorTest, ClampsTheOpeningAmountToMax) {
  AmountSelector sel;
  sel.Reset(0, /*initial=*/1, QuickPicks::kShown);
  EXPECT_EQ(sel.value(), 0);
}

TEST(AmountSelectorTest, HidesTheShortcutsWhenAsked) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  std::string rendered = RenderSelector(sel);
  EXPECT_EQ(rendered.find("[1]"), std::string::npos);
  EXPECT_EQ(rendered.find("[MAX]"), std::string::npos);
  EXPECT_NE(rendered.find("[Confirm]"), std::string::npos);
}

// Hidden is not merely invisible. Left and Right used to walk onto the
// shortcuts, which would strand the cursor on a control nobody can see.
TEST(AmountSelectorTest, HiddenShortcutsCannotBeReached) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  sel.OnEvent(ftxui::Event::ArrowLeft);
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 0) << "Left moved off the textbox";
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  sel.OnEvent(ftxui::Event::ArrowRight);
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 0) << "Right moved off the textbox";
}

// The single line holding `needle`, escape codes and all.
std::string LineWith(const std::string& rendered, const std::string& needle) {
  size_t at = rendered.find(needle);
  if (at == std::string::npos) {
    return "";
  }
  size_t begin = rendered.rfind('\n', at);
  begin = begin == std::string::npos ? 0 : begin + 1;
  return rendered.substr(begin, rendered.find('\n', at) - begin);
}

TEST(AmountSelectorTest, DimsAConfirmItCannotHonour) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  sel.set_confirm_enabled(false);
  std::string row = LineWith(RenderSelector(sel), "[Confirm]");
  EXPECT_NE(row.find("\033[2m"), std::string::npos);

  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  EXPECT_EQ(LineWith(RenderSelector(sel), "[Confirm]").find("\033[2m"),
            std::string::npos);
}

// The dimming has to be inert, not just grey: a caller that cannot honour the
// amount must never be told the player confirmed it.
TEST(AmountSelectorTest, ADisabledConfirmDoesNotConfirm) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  sel.set_confirm_enabled(false);
  sel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(sel.TakeConfirmed());
  // Leaving is still available.
  sel.OnEvent(ftxui::Event::ArrowRight);  // [Confirm] -> [Cancel]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(sel.TakeCancelled());
}

TEST(AmountSelectorTest, ResetClearsTheDisabledConfirm) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  sel.set_confirm_enabled(false);
  sel.Reset(9, /*initial=*/1, QuickPicks::kHidden);
  sel.OnEvent(ftxui::Event::ArrowDown);
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(sel.TakeConfirmed());
}

}  // namespace
}  // namespace ms
