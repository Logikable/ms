/* ScrollPanel lists available scrolls when the player selects "Scroll" from
 * an item's context menu. It overlays the main layout while kScrollSelect is
 * active. TuiController forwards all events here; OnEvent returns false only
 * for navigation so Tui can update scroll position. TakeConfirmed() returns
 * true once when the player confirms the selection via the confirm bar.
 */
#ifndef MS_SRC_FRONTEND_SCROLL_PANEL_H_
#define MS_SRC_FRONTEND_SCROLL_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "src/frontend/types.h"
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
  ftxui::Element RenderResult(const ScrollResult& r) const;
  // Handles navigation (Up/Down) and confirm-bar interaction. Returns false
  // only for navigation events so the caller can update the scroll position.
  bool OnEvent(ftxui::Event event);
  // Returns true once when the player has confirmed a scroll selection, then
  // resets the flag.
  bool TakeConfirmed();
  bool IsConfirming() const {
    return confirming_;
  }
  // Returns the scroll at the current selection.
  const Scroll& selected_scroll() const;
  int selected() const {
    return selected_;
  }

 private:
  void ResetComponent();
  static std::string FormatEntry(const Scroll& scroll);

  const std::map<std::string, Scroll>& scrolls_;
  std::vector<const Scroll*> ordered_;
  int selected_ = 0;
  std::vector<std::string> entries_;
  ftxui::Component component_;
  bool confirming_ = false;
  bool confirm_cancel_ = false;  // false = Confirm highlighted, true = Cancel
  bool confirmed_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCROLL_PANEL_H_
