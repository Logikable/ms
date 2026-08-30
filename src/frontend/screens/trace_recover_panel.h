/* TraceRecoverPanel manages selection and confirmation for trace recovery.
 * RenderTabs() renders a row of star-count chips (one per matching item) with
 * a separator below. RenderBelow() renders the confirm bar or a 3-row spacer.
 * Left/Right navigate chips; Enter opens confirm; a second Enter confirms,
 * which is the kConfirmed OnEvent answers with.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_TRACE_RECOVER_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_TRACE_RECOVER_PANEL_H_

#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"

namespace ms {

// The rows RenderTabs draws: the chip row, and the rule under it. What the
// screen subtracts before telling the card beside them how tall it may be.
inline constexpr int kRecoverTabRows = 2;

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
  ConfirmChoice OnEvent(ftxui::Event event);
  // Returns true once when the player confirms recovery, then resets the flag.
  bool IsConfirming() const {
    return confirm_.open();
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
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_TRACE_RECOVER_PANEL_H_
