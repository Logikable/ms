#include "src/frontend/panels/hotkeys_panel.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// Rows are written out at their natural length rather than padded to a fixed
// width. PadRight counts bytes, and the arrows are three bytes apiece, so it
// would pad this panel eight columns short and truncate the longest line
// through the middle of a glyph. Letting the window size itself to its content
// costs nothing here: unlike the panels above it, this one has no sibling to
// line its columns up with.
ftxui::Element Row(const std::string& text) {
  return ftxui::text(" " + text);
}

}  // namespace

ftxui::Element HotkeysPanel() {
  return ThemedWindow(
      " Hotkeys ",
      ftxui::vbox({
          // "open" rather than "select": the arrows are what select, and this
          // is the key that goes a level deeper -- into a menu, a screen, or a
          // map.
          Row("Enter: open/confirm"),
          Row("Escape: exit/cancel"),
          // Named as movement inside one panel, against Tab's movement
          // between them. Two lines that both said "panels" would leave a
          // player no way to tell which key did which.
          Row("↑/↓/←/→: move within a panel"),
          Row("Tab: switch panels"),
          ThemedSeparator(),
          Row("This panel will close at level " +
              std::to_string(HotkeysTipRetireLevel()) + "."),
      }));
}

}  // namespace ms
