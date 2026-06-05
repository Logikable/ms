/* ScrollPanel lists all available scrolls when the player selects "Scroll"
 * from an item's context menu. It replaces the main layout while
 * kScrollSelect mode is active. The caller reads selected_scroll() on Enter
 * to apply the scroll to the active equipped item.
 */
#ifndef MS_SRC_FRONTEND_SCROLL_PANEL_H_
#define MS_SRC_FRONTEND_SCROLL_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

class ScrollPanel {
 public:
  explicit ScrollPanel(const std::map<std::string, Scroll>& scrolls);
  // Replaces the displayed scroll list and resets selection to 0. Call before
  // entering kScrollSelect to show only scrolls applicable to the target item.
  void SetFilter(std::vector<const Scroll*> filtered);
  // Filters to scrolls applicable to proto by tier and job category, then
  // calls SetFilter. Returns false (and does not update the filter) if no
  // scrolls match.
  bool SetFilterForPrototype(const EquipPrototype& proto);
  ftxui::Element Render();
  bool OnEvent(ftxui::Event event);
  // Returns the scroll at the current selection.
  const Scroll& selected_scroll() const;
  int selected() const {
    return selected_;
  }

 private:
  void ResetComponent();
  static void AppendStat(std::string& out, int val, const std::string& label);
  static std::string FormatEntry(const Scroll& scroll);

  const std::map<std::string, Scroll>& scrolls_;
  std::vector<const Scroll*> ordered_;
  int selected_ = 0;
  std::vector<std::string> entries_;
  ftxui::Component component_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCROLL_PANEL_H_
