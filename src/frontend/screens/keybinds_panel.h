/* The Keybinds screen: one row per action, three key slots apiece.
 *
 * The first slot of every row is the key the game shipped with. It is drawn
 * dim and the cursor steps over it: a player who could clear it could lock
 * themselves out of the screen they cleared it from.
 *
 * Enter on a slot puts it in capture mode, where the next key pressed is the
 * one it takes. Escape on a slot clears it, and Escape on a slot that is
 * already empty leaves the screen -- as does the Close button at the foot.
 *
 * The panel is a view. It moves its own cursor and remembers what it is
 * waiting for, but the bindings themselves belong to the KeyMap.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_KEYBINDS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_KEYBINDS_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/keybinds.h"
#include "src/protos/keybinds.pb.h"

namespace ms {

class KeybindsPanel {
 public:
  explicit KeybindsPanel(const KeyMap& keys);

  // Puts the cursor on the first slot a player can change, with nothing
  // captured and nothing to say. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` rows, coming out the other end. The Close button
  // is the row past the last action.
  void MoveRow(int delta);
  // Moves the cursor `delta` slots along its row, past the locked one and
  // stopping at the ends -- a row is short enough to see, so a cursor that
  // came out the other end would read as a jump. Does nothing on Close.
  void MoveSlot(int delta);
  ftxui::Element Render() const;

  // The action the cursor is on. Meaningless while it is on Close.
  KeyAction selected_action() const;
  int selected_slot() const {
    return slot_;
  }
  bool on_close() const {
    return row_ == kKeyActionCount;
  }

  // Whether the selected slot is waiting for the key it will take.
  void StartCapture();
  void StopCapture();
  bool capturing() const {
    return capturing_;
  }

  // Says why the last key was refused, in place of the footer's instructions.
  // Cleared by the next thing the player does.
  void ShowRefusal(const std::string& message);

 private:
  // Columns one key label is given, wide enough for the longest one on screen.
  int SlotWidth() const;
  ftxui::Element RenderRow(KeyAction action, int row, int width) const;
  ftxui::Element RenderCell(const std::string& label, bool locked,
                            bool selected, int width) const;
  ftxui::Element RenderFooter() const;

  const KeyMap& keys_;
  // The action row, or kKeyActionCount for the Close button.
  int row_ = 0;
  int slot_ = 1;
  bool capturing_ = false;
  std::string message_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_KEYBINDS_PANEL_H_
