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

// Opens on the whole amount, so a player who wants all of it presses Enter and
// nothing else.
TEST(AmountSelectorTest, DefaultsToMax) {
  AmountSelector sel;
  sel.Reset(10);
  EXPECT_EQ(sel.value(), 10);
}

// Digits append, either delete key drops the last one, and a value over the
// max is held at it. Zero is reachable, which is why the confirm can be
// refused.
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

// Digits belong to the textbox alone, so arrowing onto a button and typing does
// not quietly change the amount under it.
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

// The confirm row is one Up from the textbox, so a player who went down to it
// by mistake gets back to the amount without leaving the dialog.
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
  sel.OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(sel.TakeCancelled());
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

// A shop can offer one of something the player cannot afford, so the opening
// amount has to survive a max of zero rather than sit above it.
TEST(AmountSelectorTest, ClampsTheOpeningAmountToMax) {
  AmountSelector sel;
  sel.Reset(0, /*initial=*/1);
  EXPECT_EQ(sel.value(), 0);
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
  sel.Reset(9, /*initial=*/1);
  sel.set_confirm_enabled(false);
  std::string row = LineWith(RenderSelector(sel), "[Confirm]");
  EXPECT_NE(row.find("\033[2m"), std::string::npos);

  sel.Reset(9, /*initial=*/1);
  EXPECT_EQ(LineWith(RenderSelector(sel), "[Confirm]").find("\033[2m"),
            std::string::npos);
}

// The dimming has to be inert, not just grey: a caller that cannot honour the
// amount must never be told the player confirmed it.
TEST(AmountSelectorTest, ADisabledConfirmDoesNotConfirm) {
  AmountSelector sel;
  sel.Reset(9, /*initial=*/1);
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
  sel.Reset(9, /*initial=*/1);
  sel.set_confirm_enabled(false);
  sel.Reset(9, /*initial=*/1);
  sel.OnEvent(ftxui::Event::ArrowDown);
  sel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(sel.TakeConfirmed());
}

}  // namespace
}  // namespace ms
