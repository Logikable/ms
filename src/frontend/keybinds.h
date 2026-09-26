/* The player's key bindings.
 *
 * Every action has three slots. For an action the game gives a default key,
 * the first holds it and can't be changed, so a save can never make the game
 * unplayable; the other two belong to the player. An action with no default,
 * like Mute, gives the player all three. A key does only one thing: binding it
 * removes it from whatever had it.
 *
 * The rest of the frontend doesn't need to know about this. A bound key is
 * rewritten to its action's own event (Up to ArrowUp, Confirm to Return) before
 * any panel sees it, so panels keep comparing against ftxui's events. An action
 * with no such event is a command: its key is consumed and reported instead.
 */
#ifndef MS_SRC_FRONTEND_KEYBINDS_H_
#define MS_SRC_FRONTEND_KEYBINDS_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/protos/keybinds.pb.h"

namespace ms {

// The actions, in the order the Keybinds screen lists them.
inline constexpr KeyAction kKeyActions[] = {
    KEY_ACTION_UP,         KEY_ACTION_DOWN,       KEY_ACTION_LEFT,
    KEY_ACTION_RIGHT,      KEY_ACTION_CONFIRM,    KEY_ACTION_CANCEL,
    KEY_ACTION_NEXT_PANEL, KEY_ACTION_PREV_PANEL, KEY_ACTION_MUTE};
inline constexpr int kKeyActionCount = 9;
// Keys one action can have. The first is locked if the action has a default.
inline constexpr int kKeySlots = 3;

// The action's display name: "Move Up", "Confirm", "Switch Panel".
std::string KeyActionName(KeyAction action);

// The result of a bind attempt.
enum class BindOutcome {
  kBound,
  // The key is another action's locked key, so it can't be bound.
  kReserved,
  // Not a bindable key: Ctrl+C, or something the terminal sends that this build
  // has no name for.
  kUnsupported,
};

// Every bindable key under its three names: the bytes the terminal sends, the
// id a save stores, and the label a screen shows.
class KeyCatalog {
 public:
  KeyCatalog();

  // The id for `key`, or empty if the game can't bind it.
  std::string IdOf(const ftxui::Event& key) const;
  // The label for an id, or empty if this build has no such key.
  std::string LabelOf(const std::string& id) const;
  // The event an id names, or Event::Custom if this build has no such key.
  ftxui::Event EventOf(const std::string& id) const;

 private:
  // Records one key, unless its bytes are already taken. Tab and Ctrl+I are the
  // same byte to a terminal, and the name players know wins.
  void Add(const std::string& input, const std::string& id,
           const std::string& label);
  void AddSpecials();
  void AddLetters();
  void AddCharacters();

  struct Entry {
    std::string input;
    std::string id;
    std::string label;
  };
  std::map<std::string, Entry> by_input_;
  std::map<std::string, Entry> by_id_;
};

class KeyMap {
 public:
  // Reads `binds` and writes every change back to it, so the save holds what
  // the player set. Fills in the locked keys and drops names this build doesn't
  // know.
  explicit KeyMap(Keybinds* binds);

  // The label of the key in `slot`, or empty if the slot is empty.
  std::string Label(KeyAction action, int slot) const;
  // The label of a key, bound or not. Empty for a key the game has no name for.
  std::string LabelOf(const ftxui::Event& key) const;
  // Rewrites a bound key to the event the game reads it as. Unbound keys, and
  // keys bound to a command, are returned unchanged.
  ftxui::Event Translate(const ftxui::Event& key) const;
  // The action `key` is bound to, or KEY_ACTION_UNSPECIFIED.
  KeyAction ActionOf(const ftxui::Event& key) const;
  // Puts `key` in `slot` and removes it from anywhere else it was bound,
  // including this action's other slots.
  BindOutcome Bind(KeyAction action, int slot, const ftxui::Event& key);
  // Empties `slot`. A locked slot keeps its key.
  void Unbind(KeyAction action, int slot);
  // The action `key` is locked to, or KEY_ACTION_UNSPECIFIED if it is free to
  // bind.
  KeyAction ReservedFor(const ftxui::Event& key) const;

  // Whether `action`'s `slot` holds a default key the player can't change.
  static bool Locked(KeyAction action, int slot) {
    return slot == 0 && DefaultKey(action, 0) != ftxui::Event::Custom;
  }
  // The event `action` becomes once a key is rewritten, or Event::Custom for a
  // command.
  static ftxui::Event CanonicalEvent(KeyAction action);
  // Whether `action` is handled where its key is pressed rather than rewritten.
  static bool IsCommand(KeyAction action) {
    return CanonicalEvent(action) == ftxui::Event::Custom;
  }
  // The default key for `action`'s `slot`, or Event::Custom if it has none.
  static ftxui::Event DefaultKey(KeyAction action, int slot);

 private:
  // Puts `binds` into the shape the rest of this class expects: one entry per
  // action, in order, each with kKeySlots keys.
  void Normalize();
  // Rebuilds the lookup Translate uses.
  void Index();
  Keybind* Row(KeyAction action) const;

  Keybinds* binds_;
  KeyCatalog catalog_;
  // Terminal bytes to the action they trigger.
  std::map<std::string, KeyAction> by_input_;
};

// Wraps `child` so every key it receives is one the game names. A command's
// key goes to `command` instead and never reaches `child`. `capturing` is
// checked first: while the Keybinds screen waits for a key, or a text field is
// open, the raw key passes through unchanged.
ftxui::Component TranslateKeys(
    ftxui::Component child, const KeyMap& keys, std::function<bool()> capturing,
    std::function<void(KeyAction)> command = nullptr);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_KEYBINDS_H_
