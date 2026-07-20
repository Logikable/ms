#include "src/frontend/amount_selector.h"

#include <gtest/gtest.h>

#include "ftxui/component/event.hpp"

namespace ms {
namespace {

// Empties the textbox, leaving focus on it. Digits append rather than replace,
// so typing a fresh number means clearing first.
void Clear(AmountSelector* sel) {
  while (sel->value() > 0) {
    sel->OnEvent(ftxui::Event::Backspace);
  }
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

}  // namespace
}  // namespace ms
