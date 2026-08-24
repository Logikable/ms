#include "src/frontend/widgets/text_field.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"

namespace ms {
namespace {

// Types `text` one character at a time, the way a player does.
void Type(TextField& field, const std::string& text) {
  for (char c : text) {
    field.OnEvent(ftxui::Event::Character(c));
  }
}

TEST(TextFieldTest, EditingStartsEmptyAndTakesWhatIsTyped) {
  TextField field(12);
  EXPECT_FALSE(field.editing());
  field.BeginEdit();
  EXPECT_TRUE(field.editing());
  EXPECT_EQ(field.text(), "");
  Type(field, "Sean");
  EXPECT_EQ(field.text(), "Sean");
}

TEST(TextFieldTest, EnterOnSomethingTypedCommitsIt) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "Sean99");
  EXPECT_EQ(field.OnEvent(ftxui::Event::Return), TextEntry::kCommitted);
  EXPECT_EQ(field.text(), "Sean99") << "the caller reads the entry after it";
  EXPECT_FALSE(field.editing());
}

// The four ways out that keep the old name. Enter is among them only while
// nothing has been typed.
TEST(TextFieldTest, EveryEmptyWayOutCancels) {
  for (const ftxui::Event& key :
       {ftxui::Event::Escape, ftxui::Event::ArrowUp, ftxui::Event::ArrowDown,
        ftxui::Event::Return}) {
    TextField field(12);
    field.BeginEdit();
    EXPECT_EQ(field.OnEvent(key), TextEntry::kCancelled);
    EXPECT_FALSE(field.editing());
  }
}

// Up and Down abandon whatever was typed rather than committing it -- only
// Enter keeps a name.
TEST(TextFieldTest, AnArrowThrowsAwayWhatWasTyped) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "Sean");
  EXPECT_EQ(field.OnEvent(ftxui::Event::ArrowDown), TextEntry::kCancelled);
  EXPECT_EQ(field.text(), "");
}

TEST(TextFieldTest, TakesOnlyLettersDigitsAndSpaces) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "a B-9_!.z");
  EXPECT_EQ(field.text(), "a B9z");
}

TEST(TextFieldTest, ASpaceNeedsACharacterBeforeIt) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "  IL Arch");
  EXPECT_EQ(field.text(), "IL Arch");
}

TEST(TextFieldTest, CommittingDropsTheSpacesOnTheEnd) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "IL Arch  ");
  EXPECT_EQ(field.OnEvent(ftxui::Event::Return), TextEntry::kCommitted);
  EXPECT_EQ(field.text(), "IL Arch");
}

// Backspace reaches the field only when the player has not bound it to Cancel,
// so Delete has to erase too.
TEST(TextFieldTest, EitherEraseKeyDropsTheLastCharacter) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "Sean");
  field.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(field.text(), "Sea");
  field.OnEvent(ftxui::Event::Delete);
  EXPECT_EQ(field.text(), "Se");
}

TEST(TextFieldTest, ErasingAnEmptyFieldIsHarmless) {
  TextField field(12);
  field.BeginEdit();
  field.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(field.text(), "");
  EXPECT_TRUE(field.editing()) << "erasing nothing is not a way out";
}

TEST(TextFieldTest, DropsCharactersPastTheLimit) {
  TextField field(4);
  field.BeginEdit();
  Type(field, "Logikable");
  EXPECT_EQ(field.text(), "Logi");
}

// The field is only live between BeginEdit and the way out, so a key arriving
// while the cursor merely rests on the row must not be eaten.
TEST(TextFieldTest, SwallowsNothingWhileClosed) {
  TextField field(12);
  EXPECT_EQ(field.OnEvent(ftxui::Event::Character('a')), TextEntry::kPending);
  EXPECT_EQ(field.text(), "");
  EXPECT_EQ(field.OnEvent(ftxui::Event::Return), TextEntry::kPending);
}

TEST(TextFieldTest, EditingAgainStartsFromEmpty) {
  TextField field(12);
  field.BeginEdit();
  Type(field, "Sean");
  field.OnEvent(ftxui::Event::Return);
  field.BeginEdit();
  EXPECT_EQ(field.text(), "");
}

}  // namespace
}  // namespace ms
