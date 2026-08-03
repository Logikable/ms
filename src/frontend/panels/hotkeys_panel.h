/* The tip pinned to the bottom-right corner for a new character: the four keys
 * the game is played with, and a note saying when it will go away.
 *
 * A panel in name only. It holds nothing, decides nothing, and never takes
 * focus -- there is nothing on it to select, and a panel in the Tab ring with
 * no selectable content goes deaf. Whether it is drawn at all is
 * HotkeysTipVisible's answer, asked by the caller, the same way the equipped
 * and bag panels are.
 */
#ifndef MS_SRC_FRONTEND_PANELS_HOTKEYS_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_HOTKEYS_PANEL_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// The tip as a bordered window, sized to its own longest line.
ftxui::Element HotkeysPanel();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_HOTKEYS_PANEL_H_
