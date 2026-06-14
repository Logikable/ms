/* StarForcePanel renders the star force attempt screen for a single item.
 * Shows the item name, current star count, stat gains, and probabilities.
 * Owns an inline confirm bar: Enter opens it, a second Enter confirms.
 * TakeConfirmed() returns true once when confirmed, then resets.
 */
#ifndef MS_SRC_FRONTEND_STAR_FORCE_PANEL_H_
#define MS_SRC_FRONTEND_STAR_FORCE_PANEL_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/types.h"

namespace ms {

class StarForcePanel {
 public:
  void SetItem(const EquipInstance* item);
  ftxui::Element Render() const;
  ftxui::Element RenderResult(const StarForceResult& r) const;
  // Handles Enter (open/advance confirm bar), Esc (cancel confirm), Left/Right
  // (switch buttons). Esc when not confirming is not consumed (caller handles).
  bool OnEvent(ftxui::Event event);
  bool TakeConfirmed();
  void ResetConfirm();
  bool IsConfirming() const {
    return confirming_;
  }

 private:
  const EquipInstance* item_ = nullptr;
  bool confirming_ = false;
  bool confirm_cancel_ = false;
  bool confirmed_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_STAR_FORCE_PANEL_H_
