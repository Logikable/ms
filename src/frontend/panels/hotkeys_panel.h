/* The tip pinned to the bottom-right corner for a new character: the four keys
 * the game uses, and a note saying when it will go away.
 *
 * It is a panel in name only. It holds no state and never takes focus, because
 * there is nothing on it to select, and a panel in the Tab ring with nothing
 * selectable stops receiving keys. HotkeysTipVisible decides whether it is
 * drawn, and the caller checks it, as with the equipped and bag panels.
 */
#ifndef MS_SRC_FRONTEND_PANELS_HOTKEYS_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_HOTKEYS_PANEL_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// The tip as a bordered window, sized to its longest line.
ftxui::Element HotkeysPanel();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_HOTKEYS_PANEL_H_
