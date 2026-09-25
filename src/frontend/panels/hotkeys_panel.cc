#include "src/frontend/panels/hotkeys_panel.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"

namespace ms {
namespace {

// Rows are written at their natural length, not padded to a fixed width. This
// panel has no neighbour to line its columns up with, so the window can size
// itself to its content. The window is then as wide as the longest row, so each
// row has to include the blank column inside each border.
ftxui::Element Row(const std::string& text) {
  return ftxui::text(" " + text + " ");
}

}  // namespace

ftxui::Element HotkeysPanel() {
  return ThemedWindow(
      " Hotkeys ",
      ftxui::vbox({
          // "open" rather than "select": the arrows select, and Enter goes a
          // level deeper, into a menu, a screen or a map.
          Row("Enter: open/confirm"),
          Row("Escape: exit/cancel"),
          // Described as movement within a panel, as opposed to Tab's movement
          // between panels. If both lines said "panels", the player couldn't
          // tell which key does which.
          Row("↑/↓/←/→: move within a panel"),
          Row("Tab: switch panels"),
          ThemedSeparator(),
          Row("This panel will close at level " +
              std::to_string(HotkeysTipRetireLevel()) + "."),
      }));
}

}  // namespace ms
