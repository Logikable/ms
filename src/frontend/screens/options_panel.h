/* The Options screen: one row per setting the player can change.
 *
 * Each row names a setting and shows its value on the right: a checkbox for a
 * switch, or a bar for a volume. Enter toggles the switch under the cursor, and
 * Left and Right change the volume under it. Enter on the Close button at the
 * bottom leaves, as does Escape. The cursor wraps, with the Close button as the
 * stop after the last setting.
 *
 * The list is drawn at a fixed height with room for future settings, so the
 * panel doesn't grow a row at a time as they are added.
 *
 * The panel only displays. It moves its own cursor, and the settings belong to
 * the account, which saves them.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/build_config.h"

namespace ms {

// The settings on the screen, top to bottom. The last two exist only in a build
// with music (see kAudioEnabled).
enum class Option {
  kPanelTitleBlink,
  kAutoswapPresets,
  kBuffIndicators,
  kMapBgmVolume,
  kBossBgmVolume,
};
inline constexpr int kOptionCount = kAudioEnabled ? 5 : 3;

class OptionsPanel {
 public:
  explicit OptionsPanel(AccountInstance& account);

  // Puts the cursor on the first setting. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` rows, wrapping at the ends.
  void MoveRow(int delta);
  // Toggles the switch under the cursor. Does nothing on a volume or on Close.
  void Toggle();
  // Changes the volume under the cursor by `delta`. Does nothing elsewhere.
  void Adjust(int delta);
  ftxui::Element Render() const;

  // The setting under the cursor. Meaningless while it is on Close.
  Option selected_option() const;
  bool on_close() const {
    return row_ == kOptionCount;
  }

 private:
  // The rows the list is drawn to, so the panel keeps its size as settings are
  // added. The blank rows below the last setting are room for them.
  static constexpr int kListRows = 6;
  // The name column, wide enough for the longest name plus a gutter.
  static constexpr int kNameWidth = 22;
  // The volume bar's width, and the number after it.
  static constexpr int kBarWidth = 20;
  static constexpr int kValueWidth = 5;
  // The width of a row's value, whichever kind it is.
  static constexpr int kValueColumn = kBarWidth + kValueWidth;

  // Whether `option` is a volume rather than a switch.
  static bool IsVolume(Option option);
  // Whether `option` is on, according to the account.
  bool IsOn(Option option) const;
  // The volume of `option`, according to the account.
  int VolumeOf(Option option) const;
  ftxui::Element RenderRow(Option option, int row) const;
  ftxui::Element RenderBar(int volume) const;

  AccountInstance& account_;
  // The setting's row, or kOptionCount for the Close button.
  int row_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_OPTIONS_PANEL_H_
