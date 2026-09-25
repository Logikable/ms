/* The Keybinds screen: one row per action, with three key slots each.
 *
 * The first slot of every row is the default key. It is dimmed and the cursor
 * skips it, since a player who could clear it could lock themselves out of this
 * screen.
 *
 * Enter on a slot starts capture mode, where the next key pressed is the one it
 * takes. Escape on a slot clears it, and Escape on an empty slot leaves the
 * screen, as does the Close button at the bottom.
 *
 * The panel only displays. It moves its own cursor and remembers what it is
 * waiting for, but the bindings belong to the KeyMap.
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

  // Puts the cursor on the first slot the player can change, with nothing being
  // captured and no message. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` rows, wrapping at the ends. The Close button is
  // the row after the last action.
  void MoveRow(int delta);
  // Moves the cursor `delta` slots along its row, skipping the locked one and
  // stopping at the ends, since a row is short enough that wrapping would look
  // like a jump. Does nothing on Close.
  void MoveSlot(int delta);
  ftxui::Element Render() const;

  // The action under the cursor. Meaningless while the cursor is on Close.
  KeyAction selected_action() const;
  int selected_slot() const {
    return slot_;
  }
  bool on_close() const {
    return row_ == kKeyActionCount;
  }

  // Capture mode, in which the selected slot waits for the key it will take.
  void StartCapture();
  void StopCapture();
  bool capturing() const {
    return capturing_;
  }

  // Shows why the last key was refused, in place of the footer's instructions.
  // Cleared by the player's next action.
  void ShowRefusal(const std::string& message);

 private:
  // The width given to one key label, wide enough for the longest on screen.
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
