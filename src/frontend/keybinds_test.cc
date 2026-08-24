#include "src/frontend/keybinds.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "src/frontend/widgets/text_field.h"
#include "src/protos/keybinds.pb.h"

namespace ms {
namespace {

TEST(KeyMapTest, DefaultsBindTheKeysTheGameShippedWith) {
  Keybinds binds;
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 0), "↑");
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 0), "Enter");
  EXPECT_EQ(keys.Label(KEY_ACTION_PREV_PANEL, 0), "Shift+Tab");
  // Only the locked slot ships with a key.
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "");
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 1), "");
  EXPECT_EQ(keys.Label(KEY_ACTION_CANCEL, 1), "");
}

TEST(KeyMapTest, ABoundKeyArrivesAsItsActionsEvent) {
  Keybinds binds;
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w), BindOutcome::kBound);
  EXPECT_EQ(keys.Translate(ftxui::Event::w), ftxui::Event::ArrowUp);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "W");
  // Space is a key like any other: it does nothing until the player puts it
  // somewhere.
  EXPECT_EQ(keys.Translate(ftxui::Event::Character(' ')),
            ftxui::Event::Character(' '));
  keys.Bind(KEY_ACTION_CONFIRM, 1, ftxui::Event::Character(' '));
  EXPECT_EQ(keys.Translate(ftxui::Event::Character(' ')), ftxui::Event::Return);
  // An unbound key is left alone.
  EXPECT_EQ(keys.Translate(ftxui::Event::q), ftxui::Event::q);
}

TEST(KeyMapTest, BindingAKeyTakesItOffWhateverHeldIt) {
  Keybinds binds;
  KeyMap keys(&binds);
  keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w);
  keys.Bind(KEY_ACTION_DOWN, 2, ftxui::Event::w);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "");
  EXPECT_EQ(keys.Label(KEY_ACTION_DOWN, 2), "W");
  EXPECT_EQ(keys.Translate(ftxui::Event::w), ftxui::Event::ArrowDown);

  // Bound twice to the same action, the key moves rather than doubling.
  keys.Bind(KEY_ACTION_DOWN, 1, ftxui::Event::w);
  EXPECT_EQ(keys.Label(KEY_ACTION_DOWN, 1), "W");
  EXPECT_EQ(keys.Label(KEY_ACTION_DOWN, 2), "");
}

TEST(KeyMapTest, ALockedKeyCannotBeTakenOrCleared) {
  Keybinds binds;
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::Escape),
            BindOutcome::kReserved);
  EXPECT_EQ(keys.Bind(KEY_ACTION_CANCEL, 1, ftxui::Event::Escape),
            BindOutcome::kReserved);
  EXPECT_EQ(keys.ReservedFor(ftxui::Event::Escape), KEY_ACTION_CANCEL);
  keys.Unbind(KEY_ACTION_CANCEL, 0);
  EXPECT_EQ(keys.Label(KEY_ACTION_CANCEL, 0), "Esc");
}

TEST(KeyMapTest, AKeyTheGameHasNoNameForIsRefused) {
  Keybinds binds;
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::Special("\x1b[1;2A")),
            BindOutcome::kUnsupported);
  // Ctrl+C closes the game, so it is not on offer.
  EXPECT_EQ(keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::CtrlC),
            BindOutcome::kUnsupported);
}

TEST(KeyMapTest, UnbindingOpensTheSlot) {
  Keybinds binds;
  KeyMap keys(&binds);
  keys.Bind(KEY_ACTION_CONFIRM, 1, ftxui::Event::Character(' '));
  keys.Unbind(KEY_ACTION_CONFIRM, 1);
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 1), "");
  EXPECT_EQ(keys.Translate(ftxui::Event::Character(' ')),
            ftxui::Event::Character(' '));
}

TEST(KeyMapTest, ModifiersAndLettersReadAsTheyArePrinted) {
  Keybinds binds;
  KeyMap keys(&binds);
  keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::A);
  keys.Bind(KEY_ACTION_DOWN, 1, ftxui::Event::CtrlA);
  keys.Bind(KEY_ACTION_LEFT, 1, ftxui::Event::AltA);
  keys.Bind(KEY_ACTION_RIGHT, 1, ftxui::Event::CtrlAltA);
  keys.Bind(KEY_ACTION_CONFIRM, 2, ftxui::Event::a);
  keys.Bind(KEY_ACTION_NEXT_PANEL, 1, ftxui::Event::Special("\x1bG"));
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "Shift+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_DOWN, 1), "Ctrl+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_LEFT, 1), "Alt+A");
  // Two modifiers are their initials, in Ctrl-Shift-Alt order.
  EXPECT_EQ(keys.Label(KEY_ACTION_RIGHT, 1), "CA+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 2), "A");
  EXPECT_EQ(keys.Label(KEY_ACTION_NEXT_PANEL, 1), "SA+G");
  // Each of those is a key of its own.
  EXPECT_EQ(keys.Translate(ftxui::Event::A), ftxui::Event::ArrowUp);
  EXPECT_EQ(keys.Translate(ftxui::Event::a), ftxui::Event::Return);
}

TEST(KeyMapTest, ASaveKeepsWhatThePlayerBound) {
  Keybinds binds;
  {
    KeyMap keys(&binds);
    keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w);
    keys.Bind(KEY_ACTION_CONFIRM, 2, ftxui::Event::Character(' '));
  }
  KeyMap reloaded(&binds);
  EXPECT_EQ(reloaded.Label(KEY_ACTION_UP, 1), "W");
  EXPECT_EQ(reloaded.Label(KEY_ACTION_CONFIRM, 2), "Space");
  EXPECT_EQ(reloaded.Translate(ftxui::Event::w), ftxui::Event::ArrowUp);
}

TEST(KeyMapTest, ASaveThatNamesNonsenseFallsBackToTheDefaults) {
  Keybinds binds;
  Keybind* row = binds.add_binds();
  row->set_action(KEY_ACTION_UP);
  row->add_keys("");
  row->add_keys("NoSuchKey");
  row->add_keys("Esc");
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 0), "↑");
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "");
  // Escape belongs to Cancel and cannot be held by anything else.
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 2), "");
  EXPECT_EQ(keys.Label(KEY_ACTION_CANCEL, 0), "Esc");
}

TEST(KeyMapTest, ASaveCannotBindOneKeyToTwoActions) {
  Keybinds binds;
  for (int i = 0; i < 2; ++i) {
    Keybind* row = binds.add_binds();
    row->set_action(kKeyActions[i]);
    row->add_keys("");
    row->add_keys("W");
    row->add_keys("");
  }
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "W");
  EXPECT_EQ(keys.Label(KEY_ACTION_DOWN, 1), "");
}

// Records the last key it was handed, standing in for the component tree.
class KeySpy : public ftxui::ComponentBase {
 public:
  bool OnEvent(ftxui::Event event) override {
    last = event;
    return true;
  }

  ftxui::Event last = ftxui::Event::Custom;
};

TEST(TranslateKeysTest, TheTreeHearsTheGamesKeys) {
  Keybinds binds;
  KeyMap keys(&binds);
  keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w);
  std::shared_ptr<KeySpy> spy = std::make_shared<KeySpy>();

  ftxui::Component wrapped = TranslateKeys(spy, keys, nullptr);
  wrapped->OnEvent(ftxui::Event::w);
  EXPECT_EQ(spy->last, ftxui::Event::ArrowUp);
  wrapped->OnEvent(ftxui::Event::q);
  EXPECT_EQ(spy->last, ftxui::Event::q);
}

// While a slot waits for a key, the raw one has to get through -- otherwise a
// bound key could never be rebound.
TEST(TranslateKeysTest, CapturingLetsTheRawKeyThrough) {
  Keybinds binds;
  KeyMap keys(&binds);
  keys.Bind(KEY_ACTION_CONFIRM, 1, ftxui::Event::Character(' '));
  bool capturing = true;
  std::shared_ptr<KeySpy> spy = std::make_shared<KeySpy>();

  ftxui::Component wrapped =
      TranslateKeys(spy, keys, [&capturing]() { return capturing; });
  wrapped->OnEvent(ftxui::Event::Character(' '));
  EXPECT_EQ(spy->last, ftxui::Event::Character(' '));
  capturing = false;
  wrapped->OnEvent(ftxui::Event::Character(' '));
  EXPECT_EQ(spy->last, ftxui::Event::Return);
}

// Types into a TextField, standing in for the name row of the character panel.
class FieldSpy : public ftxui::ComponentBase {
 public:
  bool OnEvent(ftxui::Event event) override {
    field.OnEvent(event);
    return true;
  }

  TextField field{12};
};

// A player who binds Space to Confirm must still be able to put a space in
// their name: while the field is open the key arrives as it was pressed.
TEST(TranslateKeysTest, SpaceBoundToConfirmStillTypesASpace) {
  Keybinds binds;
  KeyMap keys(&binds);
  keys.Bind(KEY_ACTION_CONFIRM, 1, ftxui::Event::Character(' '));
  std::shared_ptr<FieldSpy> spy = std::make_shared<FieldSpy>();
  spy->field.BeginEdit();

  ftxui::Component wrapped =
      TranslateKeys(spy, keys, [&spy]() { return spy->field.editing(); });
  for (char c : std::string("IL Arch Mage")) {
    wrapped->OnEvent(ftxui::Event::Character(c));
  }
  EXPECT_EQ(spy->field.text(), "IL Arch Mage");
  EXPECT_TRUE(spy->field.editing()) << "no space committed the name";

  // Once the field is shut, the same key is Confirm again.
  spy->field.EndEdit();
  wrapped->OnEvent(ftxui::Event::Character(' '));
  EXPECT_EQ(spy->field.text(), "");
}

}  // namespace
}  // namespace ms
