/* TraceRecoverPanel handles selection and confirmation for trace recovery.
 * RenderTabs() draws a row of star-count chips (one per matching item) with a
 * rule below. RenderBelow() draws the confirm bar or a 3-row spacer. Left and
 * Right move between chips, Enter opens the confirm bar, and a second Enter
 * confirms, which OnEvent reports as kConfirmed.
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

// The rows RenderTabs draws: the chip row and the rule below it. The screen
// subtracts these before telling the card beside them how tall it may be.
inline constexpr int kRecoverTabRows = 2;

class TraceRecoverPanel {
 public:
  explicit TraceRecoverPanel(const CharacterInstance& character);
  // Sets the trace to recover and rebuilds the list of matching inventory
  // items.
  void SetTrace(const EquipTabItem* trace);
  ftxui::Element RenderTabs() const;
  ftxui::Element RenderBelow() const;
  // Returns a made-up EquipInstance showing the state after recovery: the
  // trace's scroll stats with RecoveryStars() applied. Only valid when trace_
  // isn't null.
  EquipInstance PreviewResult() const;
  // Handles Left and Right and the confirm bar. Esc while not confirming isn't
  // consumed, so the caller can change screens.
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the confirm bar is open.
  bool IsConfirming() const {
    return confirm_.open();
  }
  // The inventory index of the selected base item, or -1 if no items match.
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
