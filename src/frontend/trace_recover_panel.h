/* TraceRecoverPanel shows a list of inventory items that can serve as the base
 * for recovering an EquipTrace. The player selects one and presses Enter to
 * trigger recovery. Display only; navigation is driven via OnEvent(); the
 * controller reads selected_index() and calls RecoverTrace on Enter.
 */
#ifndef MS_SRC_FRONTEND_TRACE_RECOVER_PANEL_H_
#define MS_SRC_FRONTEND_TRACE_RECOVER_PANEL_H_

#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"
#include "src/item.h"

namespace ms {

class TraceRecoverPanel {
 public:
  explicit TraceRecoverPanel(const CharacterInstance& character);
  // Sets the trace to recover and rebuilds the list of matching inventory
  // items.
  void SetTrace(const EquipTabItem* trace);
  ftxui::Element Render() const;
  // Handles Up/Down navigation. Returns false so the caller can consume
  // Escape/Enter without forwarding here.
  bool OnEvent(ftxui::Event event);
  // Returns the inventory index of the currently selected base item, or -1 if
  // there are no matching items.
  int selected_index() const;
  ftxui::Element RenderResult(const TraceRecoveryResult& r) const;

 private:
  const CharacterInstance& character_;
  const EquipTabItem* trace_ = nullptr;
  std::vector<int> matching_indices_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TRACE_RECOVER_PANEL_H_
