#include "src/frontend/keybinds.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "src/protos/keybinds.pb.h"

namespace ms {
namespace {

TEST(KeyMapTest, DefaultsBindTheKeysTheGameShippedWith) {
  Keybinds binds;
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 0), "↑");
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 0), "Enter");
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 1), "Space");
  EXPECT_EQ(keys.Label(KEY_ACTION_CANCEL, 1), "Backspace");
  EXPECT_EQ(keys.Label(KEY_ACTION_PREV_PANEL, 0), "Shift+Tab");
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "");
}

TEST(KeyMapTest, ABoundKeyArrivesAsItsActionsEvent) {
  Keybinds binds;
  KeyMap keys(&binds);
  EXPECT_EQ(keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w), BindOutcome::kBound);
  EXPECT_EQ(keys.Translate(ftxui::Event::w), ftxui::Event::ArrowUp);
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "W");
  // The default aliases go through the same door.
  EXPECT_EQ(keys.Translate(ftxui::Event::Character(' ')), ftxui::Event::Return);
  EXPECT_EQ(keys.Translate(ftxui::Event::Backspace), ftxui::Event::Escape);
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
  EXPECT_EQ(keys.Label(KEY_ACTION_UP, 1), "Shift+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_DOWN, 1), "Ctrl+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_LEFT, 1), "Alt+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_RIGHT, 1), "Ctrl+Alt+A");
  EXPECT_EQ(keys.Label(KEY_ACTION_CONFIRM, 2), "A");
  // Each of those is a key of its own.
  EXPECT_EQ(keys.Translate(ftxui::Event::A), ftxui::Event::ArrowUp);
  EXPECT_EQ(keys.Translate(ftxui::Event::a), ftxui::Event::Return);
}

TEST(KeyMapTest, ASaveKeepsWhatThePlayerBound) {
  Keybinds binds;
  {
    KeyMap keys(&binds);
    keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w);
    keys.Unbind(KEY_ACTION_CONFIRM, 1);
  }
  KeyMap reloaded(&binds);
  EXPECT_EQ(reloaded.Label(KEY_ACTION_UP, 1), "W");
  EXPECT_EQ(reloaded.Label(KEY_ACTION_CONFIRM, 1), "");
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

}  // namespace
}  // namespace ms
