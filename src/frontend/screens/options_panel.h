/* The Options screen: one row per setting the player can change.
 *
 * A row names a setting and shows its value on the right -- a checkbox for a
 * switch, a bar for a volume. Enter throws the switch under the cursor and
 * Left and Right move the volume under it. Enter on the Close button at the
 * foot leaves, and so does Escape. The cursor wraps, the Close button being
 * the stop past the last setting.
 *
 * The list is drawn to a fixed height with room for settings that do not
 * exist yet, so the panel does not grow a row at a time as they arrive.
 *
 * The panel is a view. It moves its own cursor; the settings themselves
 * belong to the account, which is what saves them.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/build_config.h"

namespace ms {

// The settings the screen holds, top to bottom. The two volumes are only
// there in a build that has music; see kAudioEnabled.
enum class Option {
  kPanelTitleBlink,
  kMapBgmVolume,
  kBossBgmVolume,
};
inline constexpr int kOptionCount = kAudioEnabled ? 3 : 1;

class OptionsPanel {
 public:
  explicit OptionsPanel(AccountInstance& account);

  // Puts the cursor on the first setting. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` rows, coming out the other end.
  void MoveRow(int delta);
  // Throws the switch the cursor is on. Does nothing on a volume or on Close.
  void Toggle();
  // Moves the volume the cursor is on by `delta`. Does nothing elsewhere.
  void Adjust(int delta);
  ftxui::Element Render() const;

  // The setting the cursor is on. Meaningless while it is on Close.
  Option selected_option() const;
  bool on_close() const {
    return row_ == kOptionCount;
  }

 private:
  // Rows the list is drawn to, so the panel keeps its size as settings are
  // added. The blank rows below the last one are the room they will take.
  static constexpr int kListRows = 5;
  // The name column, wide enough for the longest name and a gutter.
  static constexpr int kNameWidth = 22;
  // The bar a volume is drawn as, and the number standing after it.
  static constexpr int kBarWidth = 20;
  static constexpr int kValueWidth = 5;
  // What a row's value gets, whichever kind of value it is.
  static constexpr int kValueColumn = kBarWidth + kValueWidth;

  // Whether `option` is a volume rather than a switch.
  static bool IsVolume(Option option);
  // Whether `option` is switched on, asked of the account.
  bool IsOn(Option option) const;
  // The volume `option` stands at, asked of the account.
  int VolumeOf(Option option) const;
  ftxui::Element RenderRow(Option option, int row) const;
  ftxui::Element RenderBar(int volume) const;

  AccountInstance& account_;
  // The setting's row, or kOptionCount for the Close button.
  int row_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_
