/* ScrollPanel lists all available scrolls when the player selects "Scroll"
 * from an item's context menu. It replaces the main layout while
 * kScrollSelect mode is active. The caller reads selected_scroll() on Enter
 * to apply the scroll to the active equipped item.
 *
 * Call MakeComponent() exactly once; the returned Component captures
 * references to internal state, so the panel object must outlive it.
 */
#ifndef MS_SRC_FRONTEND_SCROLL_PANEL_H_
#define MS_SRC_FRONTEND_SCROLL_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "src/protos/scroll.pb.h"

namespace ms {

class ScrollPanel {
 public:
  explicit ScrollPanel(const std::map<std::string, Scroll>& scrolls);
  ftxui::Component MakeComponent();
  // Returns the scroll at the current selection.
  const Scroll& selected_scroll() const;
  int selected() const {
    return selected_;
  }

 private:
  static void AppendStat(std::string& out, int val, const std::string& label);
  static std::string FormatEntry(const Scroll& scroll);

  const std::map<std::string, Scroll>& scrolls_;
  // Stable ordering into scrolls_ (map key order).
  std::vector<const Scroll*> ordered_;
  int selected_ = 0;
  std::vector<std::string> entries_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCROLL_PANEL_H_
