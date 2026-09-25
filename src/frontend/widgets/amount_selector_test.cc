#include "src/frontend/widgets/amount_selector.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// Empties the textbox, leaving focus on it. Digits append rather than replace,
// so typing a new number means clearing first.
void Clear(AmountSelector* sel) {
  while (sel->value() > 0) {
    sel->OnEvent(ftxui::Event::Backspace);
  }
}

// The selector's bottom row is the game's standard confirm row, not a copy.
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

// A trade can offer nothing as easily as everything, so the caller chooses the
// low button's value.
TEST(AmountSelectorTest, TheLowButtonIsTheCallersToSet) {
  AmountSelector sel;
  sel.Reset(10);
  sel.set_low(0);
  ftxui::Element element = sel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);
  EXPECT_NE(screen.ToString().find("[0]"), std::string::npos);

  // Left onto it, then Enter: the value becomes zero, not one.
  sel.OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  EXPECT_EQ(sel.value(), 0);

  // Reset restores it, so no caller inherits another's button.
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::ArrowLeft);
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(sel.value(), 1);
}

// A large meso amount exceeds an int, and the field grows to show it.
TEST(AmountSelectorTest, CarriesAnAmountPastAnInt) {
  constexpr int64_t kPurse = 100000000000;
  AmountSelector sel;
  sel.Reset(kPurse, /*initial=*/0);
  for (int i = 0; i < 11; ++i) {
    sel.OnEvent(ftxui::Event::Character('9'));
  }
  EXPECT_EQ(sel.value(), 99999999999);

  // One more digit is held at the maximum rather than wrapping past it.
  sel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(sel.value(), kPurse);

  ftxui::Element element = sel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);
  EXPECT_NE(screen.ToString().find("100000000000"), std::string::npos);
}

// It starts at the full amount, so a player who wants everything just presses
// Enter.
TEST(AmountSelectorTest, DefaultsToMax) {
  AmountSelector sel;
  sel.Reset(10);
  EXPECT_EQ(sel.value(), 10);
}

// Digits append, either delete key removes the last one, and a value over the
// max is held at it. Zero is reachable, which is why Confirm can be disabled.
TEST(AmountSelectorTest, TheTextboxEditsByDigitAndDelete) {
  AmountSelector sel;
  sel.Reset(100);
  Clear(&sel);
  sel.OnEvent(ftxui::Event::Character('2'));
  sel.OnEvent(ftxui::Event::Character('5'));
  EXPECT_EQ(sel.value(), 25);
  sel.OnEvent(ftxui::Event::Delete);
  EXPECT_EQ(sel.value(), 2);
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 0);
  sel.OnEvent(ftxui::Event::Character('9'));
  sel.OnEvent(ftxui::Event::Character('9'));
  sel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(sel.value(), 100) << "999 held at the max";
}

TEST(AmountSelectorTest, TheShortcutButtonsSetOneAndMax) {
  AmountSelector sel;
  sel.Reset(100);
  sel.OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(sel.value(), 1);
  sel.OnEvent(ftxui::Event::ArrowRight);  // [1] -> textbox
  sel.OnEvent(ftxui::Event::ArrowRight);  // textbox -> [MAX]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(sel.value(), 100);
}

// Only the textbox takes digits, so moving onto a button and typing doesn't
// silently change the amount.
TEST(AmountSelectorTest, DigitsEditOnlyWhileTheTextboxHoldsFocus) {
  AmountSelector sel;
  sel.Reset(100);
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 10) << "the textbox holds focus to begin with";
  sel.OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  sel.OnEvent(ftxui::Event::Character('5'));
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 10);
  sel.OnEvent(ftxui::Event::ArrowRight);  // [1] -> textbox
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 1);
}

TEST(AmountSelectorTest, DownFromTextboxActivatesConfirm) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST(AmountSelectorTest, RightFromConfirmActivatesCancel) {
  AmountSelector sel;
  sel.Reset(10);
  sel.OnEvent(ftxui::Event::ArrowDown);   // textbox -> [Confirm]
  sel.OnEvent(ftxui::Event::ArrowRight);  // [Confirm] -> [Cancel]
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

// The confirm row is one Up from the textbox, so a player who moved down by
// mistake can get back to the amount without leaving the dialog.
TEST(AmountSelectorTest, UpFromTheConfirmRowReturnsToTheTextbox) {
  AmountSelector sel;
  sel.Reset(100);
  sel.OnEvent(ftxui::Event::ArrowDown);
  sel.OnEvent(ftxui::Event::ArrowUp);
  sel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(sel.value(), 10);
}

TEST(AmountSelectorTest, EscapeCancels) {
  AmountSelector sel;
  sel.Reset(10);
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

// --- the opening amount ---

std::string RenderSelector(const AmountSelector& sel) {
  ftxui::Element element = sel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);
  return screen.ToString();
}

TEST(AmountSelectorTest, OpensOnTheAmountItIsGiven) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1);
  EXPECT_EQ(sel.value(), 1);
}

// A shop can offer one of something the player can't afford, so the starting
// amount must handle a max of zero instead of exceeding it.
TEST(AmountSelectorTest, ClampsTheOpeningAmountToMax) {
  AmountSelector sel;
  sel.Reset(0, /*initial=*/1);
  EXPECT_EQ(sel.value(), 0);
  // Including through the shortcut: a caller that won't accept one must not get
  // one from the button either.
  sel.OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(sel.value(), 0);
}

// The line containing `needle`, including escape codes.
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
  sel.Reset(9, /*initial=*/1);
  sel.set_confirm_enabled(false);
  std::string row = LineWith(RenderSelector(sel), "[Confirm]");
  EXPECT_NE(row.find("\033[2m"), std::string::npos);

  sel.Reset(9, /*initial=*/1);
  EXPECT_EQ(LineWith(RenderSelector(sel), "[Confirm]").find("\033[2m"),
            std::string::npos);
}

// The disabled state must do nothing, not just look grey: a caller that can't
// accept the amount must never be told the player confirmed it.
TEST(AmountSelectorTest, ADisabledConfirmDoesNotConfirm) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1);
  sel.set_confirm_enabled(false);
  sel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  // Leaving still works.
  sel.OnEvent(ftxui::Event::ArrowRight);  // [Confirm] -> [Cancel]
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

TEST(AmountSelectorTest, ResetClearsTheDisabledConfirm) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1);
  sel.set_confirm_enabled(false);
  sel.Reset(9, /*initial=*/1);
  sel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(sel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

}  // namespace
}  // namespace ms
