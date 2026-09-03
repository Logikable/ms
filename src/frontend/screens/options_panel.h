/* The Options screen: one row per switch the player can throw.
 *
 * A row names an option and shows its state as a checkbox. Enter on a row
 * toggles it, Enter on the Close button at the foot leaves, and so does
 * Escape. The cursor wraps, the Close button being the stop past the last
 * option.
 *
 * The list is drawn to a fixed height with room for options that do not exist
 * yet, so the panel does not grow a row at a time as they arrive.
 *
 * The panel is a view. It moves its own cursor; the settings themselves belong
 * to the account, which is what saves them.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/account.h"

namespace ms {

// The switches the screen holds, top to bottom.
enum class Option {
  kPanelTitleBlink,
};
inline constexpr int kOptionCount = 1;

class OptionsPanel {
 public:
  explicit OptionsPanel(AccountInstance& account);

  // Puts the cursor on the first option. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` rows, coming out the other end.
  void MoveRow(int delta);
  // Throws the switch the cursor is on. Does nothing on the Close button.
  void Toggle();
  ftxui::Element Render() const;

  // The option the cursor is on. Meaningless while it is on Close.
  Option selected_option() const;
  bool on_close() const {
    return row_ == kOptionCount;
  }

 private:
  // Rows the list is drawn to, so the panel keeps its size as options are
  // added. The blank rows below the last one are the room they will take.
  static constexpr int kListRows = 5;
  // The option column, wide enough for the longest name and a gutter.
  static constexpr int kNameWidth = 22;
  static constexpr int kStateWidth = 7;

  // Whether `option` is switched on, asked of the account.
  bool IsOn(Option option) const;
  ftxui::Element RenderRow(Option option, int row) const;

  AccountInstance& account_;
  // The option row, or kOptionCount for the Close button.
  int row_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_
