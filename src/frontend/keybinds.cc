#include "src/frontend/keybinds.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"

namespace ms {
namespace {

// Rewrites `child`'s keys through the map. See TranslateKeys.
class KeyTranslator : public ftxui::ComponentBase {
 public:
  KeyTranslator(ftxui::Component child, const KeyMap& keys,
                std::function<bool()> capturing)
      : keys_(keys), capturing_(std::move(capturing)) {
    Add(std::move(child));
  }

  bool OnEvent(ftxui::Event event) override {
    if (capturing_ != nullptr && capturing_()) {
      return ftxui::ComponentBase::OnEvent(event);
    }
    return ftxui::ComponentBase::OnEvent(keys_.Translate(event));
  }

 private:
  const KeyMap& keys_;
  std::function<bool()> capturing_;
};

}  // namespace

std::string KeyActionName(KeyAction action) {
  switch (action) {
    case KEY_ACTION_UP:
      return "Move Up";
    case KEY_ACTION_DOWN:
      return "Move Down";
    case KEY_ACTION_LEFT:
      return "Move Left";
    case KEY_ACTION_RIGHT:
      return "Move Right";
    case KEY_ACTION_CONFIRM:
      return "Confirm";
    case KEY_ACTION_CANCEL:
      return "Cancel";
    case KEY_ACTION_NEXT_PANEL:
      return "Switch Panel";
    case KEY_ACTION_PREV_PANEL:
      return "Previous Panel";
    default:
      return "";
  }
}

KeyCatalog::KeyCatalog() {
  AddSpecials();
  AddLetters();
  AddCharacters();
}

void KeyCatalog::Add(const std::string& input, const std::string& id,
                     const std::string& label) {
  if (by_input_.count(input) > 0) {
    return;
  }
  Entry entry{input, id, label};
  by_input_[input] = entry;
  by_id_[id] = entry;
}

void KeyCatalog::AddSpecials() {
  Add(ftxui::Event::ArrowUp.input(), "Up", "↑");
  Add(ftxui::Event::ArrowDown.input(), "Down", "↓");
  Add(ftxui::Event::ArrowLeft.input(), "Left", "←");
  Add(ftxui::Event::ArrowRight.input(), "Right", "→");
  Add(ftxui::Event::ArrowUpCtrl.input(), "CtrlUp", "Ctrl+↑");
  Add(ftxui::Event::ArrowDownCtrl.input(), "CtrlDown", "Ctrl+↓");
  Add(ftxui::Event::ArrowLeftCtrl.input(), "CtrlLeft", "Ctrl+←");
  Add(ftxui::Event::ArrowRightCtrl.input(), "CtrlRight", "Ctrl+→");
  Add(ftxui::Event::Return.input(), "Enter", "Enter");
  Add(ftxui::Event::Escape.input(), "Esc", "Esc");
  Add(ftxui::Event::Tab.input(), "Tab", "Tab");
  Add(ftxui::Event::TabReverse.input(), "ShiftTab", "Shift+Tab");
  Add(ftxui::Event::Backspace.input(), "Backspace", "Backspace");
  Add(ftxui::Event::Delete.input(), "Delete", "Delete");
  Add(ftxui::Event::Insert.input(), "Insert", "Insert");
  Add(ftxui::Event::Home.input(), "Home", "Home");
  Add(ftxui::Event::End.input(), "End", "End");
  Add(ftxui::Event::PageUp.input(), "PageUp", "Page Up");
  Add(ftxui::Event::PageDown.input(), "PageDown", "Page Down");
  Add(ftxui::Event::Character(' ').input(), "Space", "Space");

  const ftxui::Event function_keys[] = {
      ftxui::Event::F1,  ftxui::Event::F2,  ftxui::Event::F3,
      ftxui::Event::F4,  ftxui::Event::F5,  ftxui::Event::F6,
      ftxui::Event::F7,  ftxui::Event::F8,  ftxui::Event::F9,
      ftxui::Event::F10, ftxui::Event::F11, ftxui::Event::F12};
  for (int i = 0; i < 12; ++i) {
    std::string name = "F" + std::to_string(i + 1);
    Add(function_keys[i].input(), name, name);
  }
}

void KeyCatalog::AddLetters() {
  for (char lower = 'a'; lower <= 'z'; ++lower) {
    std::string upper(1, static_cast<char>(lower - 'a' + 'A'));
    // A letter is written the way a keyboard prints it, so shift is what
    // tells the two apart rather than the case of the label.
    Add(std::string(1, lower), upper, upper);
    Add(upper, "Shift" + upper, "Shift+" + upper);
    // Ctrl+C is left out on purpose: it closes the game, and a player who
    // bound it away could not.
    std::string ctrl(1, static_cast<char>(lower - 'a' + 1));
    if (lower != 'c') {
      Add(ctrl, "Ctrl" + upper, "Ctrl+" + upper);
      // Two modifiers are written as their initials, in Ctrl-Shift-Alt order:
      // spelling both out gives one key a label wider than the column it
      // shares with two others.
      Add("\x1b" + ctrl, "CtrlAlt" + upper, "CA+" + upper);
    }
    Add("\x1b" + std::string(1, lower), "Alt" + upper, "Alt+" + upper);
    Add("\x1b" + upper, "AltShift" + upper, "SA+" + upper);
  }
}

void KeyCatalog::AddCharacters() {
  // Everything else on the keyboard that prints: digits and punctuation, each
  // known by the character it types.
  for (char c = '!'; c <= '~'; ++c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      continue;
    }
    Add(std::string(1, c), std::string(1, c), std::string(1, c));
  }
}

std::string KeyCatalog::IdOf(const ftxui::Event& key) const {
  if (key.is_mouse()) {
    return "";
  }
  std::map<std::string, Entry>::const_iterator it = by_input_.find(key.input());
  if (it == by_input_.end()) {
    return "";
  }
  return it->second.id;
}

std::string KeyCatalog::LabelOf(const std::string& id) const {
  std::map<std::string, Entry>::const_iterator it = by_id_.find(id);
  if (it == by_id_.end()) {
    return "";
  }
  return it->second.label;
}

ftxui::Event KeyCatalog::EventOf(const std::string& id) const {
  std::map<std::string, Entry>::const_iterator it = by_id_.find(id);
  if (it == by_id_.end()) {
    return ftxui::Event::Custom;
  }
  return ftxui::Event::Special(it->second.input);
}

KeyMap::KeyMap(Keybinds* binds) : binds_(binds) {
  Normalize();
  Index();
}

ftxui::Event KeyMap::CanonicalEvent(KeyAction action) {
  switch (action) {
    case KEY_ACTION_UP:
      return ftxui::Event::ArrowUp;
    case KEY_ACTION_DOWN:
      return ftxui::Event::ArrowDown;
    case KEY_ACTION_LEFT:
      return ftxui::Event::ArrowLeft;
    case KEY_ACTION_RIGHT:
      return ftxui::Event::ArrowRight;
    case KEY_ACTION_CONFIRM:
      return ftxui::Event::Return;
    case KEY_ACTION_CANCEL:
      return ftxui::Event::Escape;
    case KEY_ACTION_NEXT_PANEL:
      return ftxui::Event::Tab;
    case KEY_ACTION_PREV_PANEL:
      return ftxui::Event::TabReverse;
    default:
      return ftxui::Event::Custom;
  }
}

ftxui::Event KeyMap::DefaultKey(KeyAction action, int slot) {
  // Only the locked slot ships with a key. The other two are the player's,
  // empty until they put something in them.
  if (slot == 0) {
    return CanonicalEvent(action);
  }
  return ftxui::Event::Custom;
}

Keybind* KeyMap::Row(KeyAction action) const {
  for (int i = 0; i < binds_->binds_size(); ++i) {
    if (binds_->binds(i).action() == action) {
      return binds_->mutable_binds(i);
    }
  }
  return nullptr;
}

void KeyMap::Normalize() {
  // A save carries only what the player set, and may carry nothing at all.
  // What comes out is one row per action, in order, three slots apiece.
  Keybinds ordered;
  for (int i = 0; i < kKeyActionCount; ++i) {
    KeyAction action = kKeyActions[i];
    Keybind* row = ordered.add_binds();
    row->set_action(action);
    Keybind* saved = Row(action);
    // The locked slot is the game's own key; the two after it are whatever
    // the save carries, which for a new character is nothing.
    row->add_keys(catalog_.IdOf(DefaultKey(action, 0)));
    for (int slot = 1; slot < kKeySlots; ++slot) {
      std::string id;
      if (saved != nullptr && slot < saved->keys_size()) {
        id = saved->keys(slot);
      }
      row->add_keys(id);
    }
  }
  *binds_ = ordered;

  // Anything this build cannot honour goes: a name it does not know, a key
  // some action holds locked, and a key a row above already claimed.
  std::map<std::string, bool> taken;
  for (int i = 0; i < binds_->binds_size(); ++i) {
    Keybind* row = binds_->mutable_binds(i);
    for (int slot = 0; slot < kKeySlots; ++slot) {
      // A copy: the row is written to below.
      std::string id = row->keys(slot);
      if (id.empty()) {
        continue;
      }
      ftxui::Event key = catalog_.EventOf(id);
      bool known = !catalog_.LabelOf(id).empty();
      bool reserved = ReservedFor(key) != KEY_ACTION_UNSPECIFIED;
      if (!known || taken[id] || (reserved && !Locked(slot))) {
        row->set_keys(slot, "");
        continue;
      }
      taken[id] = true;
    }
  }
}

void KeyMap::Index() {
  by_input_.clear();
  for (int i = 0; i < binds_->binds_size(); ++i) {
    const Keybind& row = binds_->binds(i);
    for (int slot = 0; slot < kKeySlots; ++slot) {
      if (row.keys(slot).empty()) {
        continue;
      }
      by_input_[catalog_.EventOf(row.keys(slot)).input()] = row.action();
    }
  }
}

std::string KeyMap::Label(KeyAction action, int slot) const {
  Keybind* row = Row(action);
  if (row == nullptr || slot < 0 || slot >= kKeySlots) {
    return "";
  }
  return catalog_.LabelOf(row->keys(slot));
}

std::string KeyMap::LabelOf(const ftxui::Event& key) const {
  return catalog_.LabelOf(catalog_.IdOf(key));
}

ftxui::Event KeyMap::Translate(const ftxui::Event& key) const {
  if (key.is_mouse()) {
    return key;
  }
  std::map<std::string, KeyAction>::const_iterator it =
      by_input_.find(key.input());
  if (it == by_input_.end()) {
    return key;
  }
  return CanonicalEvent(it->second);
}

KeyAction KeyMap::ReservedFor(const ftxui::Event& key) const {
  for (int i = 0; i < kKeyActionCount; ++i) {
    if (DefaultKey(kKeyActions[i], 0) == key) {
      return kKeyActions[i];
    }
  }
  return KEY_ACTION_UNSPECIFIED;
}

BindOutcome KeyMap::Bind(KeyAction action, int slot, const ftxui::Event& key) {
  if (Locked(slot) || slot < 0 || slot >= kKeySlots) {
    return BindOutcome::kReserved;
  }
  std::string id = catalog_.IdOf(key);
  if (id.empty()) {
    return BindOutcome::kUnsupported;
  }
  if (ReservedFor(key) != KEY_ACTION_UNSPECIFIED) {
    return BindOutcome::kReserved;
  }
  // One key, one job. Whatever else held it lets go -- including this action's
  // other slot, so binding a key twice moves it rather than doubling it.
  for (int i = 0; i < binds_->binds_size(); ++i) {
    Keybind* row = binds_->mutable_binds(i);
    for (int s = 1; s < kKeySlots; ++s) {
      if (row->keys(s) == id) {
        row->set_keys(s, "");
      }
    }
  }
  Keybind* row = Row(action);
  if (row == nullptr) {
    return BindOutcome::kUnsupported;
  }
  row->set_keys(slot, id);
  Index();
  return BindOutcome::kBound;
}

void KeyMap::Unbind(KeyAction action, int slot) {
  if (Locked(slot) || slot < 0 || slot >= kKeySlots) {
    return;
  }
  Keybind* row = Row(action);
  if (row == nullptr) {
    return;
  }
  row->set_keys(slot, "");
  Index();
}

ftxui::Component TranslateKeys(ftxui::Component child, const KeyMap& keys,
                               std::function<bool()> capturing) {
  return std::make_shared<KeyTranslator>(std::move(child), keys,
                                         std::move(capturing));
}

}  // namespace ms
