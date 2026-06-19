/* TraceRecoverPanel manages selection and confirmation for trace recovery.
 * RenderTabs() renders a row of star-count chips (one per matching item) with
 * a separator below. RenderBelow() renders the confirm bar or a 3-row spacer.
 * Left/Right navigate chips; Enter opens confirm; a second Enter confirms.
 * TakeConfirmed() returns true once when confirmed.
 */
#ifndef MS_SRC_FRONTEND_TRACE_RECOVER_PANEL_H_
#define MS_SRC_FRONTEND_TRACE_RECOVER_PANEL_H_

#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/frontend/types.h"
#include "src/item.h"

namespace ms {

class TraceRecoverPanel {
 public:
  explicit TraceRecoverPanel(const CharacterInstance& character);
  // Sets the trace to recover and rebuilds the list of matching inventory
  // items.
  void SetTrace(const EquipTabItem* trace);
  ftxui::Element RenderTabs() const;
  ftxui::Element RenderBelow() const;
  // Returns a synthetic EquipInstance representing the post-recovery state:
  // trace's scroll stats with RecoveryStars() applied. Only valid when
  // trace_ != nullptr.
  EquipInstance PreviewResult() const;
  // Handles Left/Right navigation and confirm-bar interaction. Esc when not
  // confirming is not consumed (caller handles screen transition).
  bool OnEvent(ftxui::Event event);
  // Returns true once when the player confirms recovery, then resets the flag.
  bool TakeConfirmed();
  bool IsConfirming() const {
    return confirming_;
  }
  // Returns the inventory index of the currently selected base item, or -1 if
  // there are no matching items.
  int selected_index() const;
  ftxui::Element RenderResult(const TraceRecoveryResult& r) const;

 private:
  const CharacterInstance& character_;
  const EquipTabItem* trace_ = nullptr;
  std::vector<int> matching_indices_;
  int selected_ = 0;
  bool confirming_ = false;
  bool confirm_cancel_ = false;
  bool confirmed_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TRACE_RECOVER_PANEL_H_
