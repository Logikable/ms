/* The player's keyboard bindings.
 *
 * Every action has three slots. The first holds the key the game shipped with
 * and cannot be changed, so a save can never leave the game unplayable; the
 * other two are the player's. No key does two jobs: binding one takes it off
 * whatever else held it.
 *
 * The rest of the frontend never learns any of this. A bound key is rewritten
 * to its action's own event -- Up to ArrowUp, Confirm to Return -- before any
 * panel sees it, so panels go on comparing against ftxui's events.
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
    KEY_ACTION_UP,         KEY_ACTION_DOWN,      KEY_ACTION_LEFT,
    KEY_ACTION_RIGHT,      KEY_ACTION_CONFIRM,   KEY_ACTION_CANCEL,
    KEY_ACTION_NEXT_PANEL, KEY_ACTION_PREV_PANEL};
inline constexpr int kKeyActionCount = 8;
// Keys one action can hold. The first is the locked one.
inline constexpr int kKeySlots = 3;

// What the player calls the action: "Move Up", "Confirm", "Switch Panel".
std::string KeyActionName(KeyAction action);

// How a bind attempt ended.
enum class BindOutcome {
  kBound,
  // The key is some action's locked first key, so nothing can take it.
  kReserved,
  // Not a key the game can bind: Ctrl+C, or something the terminal sends that
  // this build has no name for.
  kUnsupported,
};

// Every key the game can bind, under the three names one key has: the bytes
// the terminal sends, the id a save carries, and the label a screen shows.
class KeyCatalog {
 public:
  KeyCatalog();

  // The id for `key`, or empty when the game cannot bind it.
  std::string IdOf(const ftxui::Event& key) const;
  // The label for an id, or empty when this build has no such key.
  std::string LabelOf(const std::string& id) const;
  // The event an id names, or Event::Custom when this build has no such key.
  ftxui::Event EventOf(const std::string& id) const;

 private:
  // Records one key, unless its bytes are already spoken for. Tab and Ctrl+I
  // are the same byte to a terminal, and the name the player knows it by wins.
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
  // Reads `binds` and writes every change back to it, so what the save holds
  // is what the player set. Fills in the locked keys and drops any name this
  // build does not know.
  explicit KeyMap(Keybinds* binds);

  // The label of the key in `slot`, or empty when the slot is open.
  std::string Label(KeyAction action, int slot) const;
  // The label of a key, whether or not it is bound. Empty for a key the game
  // has no name for.
  std::string LabelOf(const ftxui::Event& key) const;
  // Rewrites a bound key to the event the game reads it as. Anything unbound
  // comes back as it arrived.
  ftxui::Event Translate(const ftxui::Event& key) const;
  // Puts `key` in `slot`, clearing wherever else it was bound -- another slot
  // of this action included.
  BindOutcome Bind(KeyAction action, int slot, const ftxui::Event& key);
  // Empties `slot`. A locked slot keeps its key.
  void Unbind(KeyAction action, int slot);
  // The action `key` is locked to, or KEY_ACTION_UNSPECIFIED for a key that is
  // free to be bound.
  KeyAction ReservedFor(const ftxui::Event& key) const;

  // The slot no action lets go of.
  static bool Locked(int slot) {
    return slot == 0;
  }
  // The event `action` is read as once a key has been rewritten.
  static ftxui::Event CanonicalEvent(KeyAction action);
  // The key `action` ships with, which is its locked first slot.
  static ftxui::Event DefaultKey(KeyAction action, int slot);

 private:
  // Puts `binds` into the shape the rest of this class assumes: one entry per
  // action, in order, each with kKeySlots keys.
  void Normalize();
  // Rebuilds the lookup Translate reads.
  void Index();
  Keybind* Row(KeyAction action) const;

  Keybinds* binds_;
  KeyCatalog catalog_;
  // Terminal bytes to the action they trigger.
  std::map<std::string, KeyAction> by_input_;
};

// Wraps `child` so every key reaching it is one the game names. `capturing` is
// asked first: while the Keybinds screen waits for a key, the raw one goes
// through untouched.
ftxui::Component TranslateKeys(ftxui::Component child, const KeyMap& keys,
                               std::function<bool()> capturing);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_KEYBINDS_H_
