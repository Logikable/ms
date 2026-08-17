/* StarForcePanel renders the star force attempt screen for a single item.
 * Shows the item name, current star count, stat gains, probabilities and what
 * the attempt costs. Its foot is an [Enhance] / [Cancel] row: Enter on Enhance
 * opens the inline confirm bar, a second Enter confirms, and Enter on Cancel
 * leaves. TakeConfirmed() and TakeCancelled() each return true once, then
 * reset.
 *
 * A player who cannot afford the attempt is not stopped at the confirm bar:
 * the price is red, [Enhance] is greyed and cannot be reached, and the cursor
 * is already on [Cancel].
 */
#ifndef MS_SRC_FRONTEND_SCREENS_STAR_FORCE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_STAR_FORCE_PANEL_H_

#include <cstdint>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/types.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/item/equip_instance.h"

namespace ms {

class StarForcePanel {
 public:
  // The item and the purse that has to pay for it, together: what an attempt
  // costs and what is there to spend on it are read in the same breath, and
  // one without the other would draw a price nobody checked.
  void SetItem(const EquipInstance* item, int64_t meso);
  ftxui::Element Render() const;
  ftxui::Element RenderResult(const StarForceResult& r) const;
  // Handles Enter (press the button under the cursor, or advance the confirm
  // bar), Esc (cancel confirm), Left/Right (switch buttons). Esc when not
  // confirming is not consumed (caller handles).
  bool OnEvent(ftxui::Event event);
  bool TakeConfirmed();
  // Whether the player pressed [Cancel]. The caller closes the screen; the
  // panel only reports it, the same way it reports a confirmed attempt.
  bool TakeCancelled();
  void ResetConfirm();
  bool IsConfirming() const {
    return confirm_.open();
  }

 private:
  // What one attempt on the current item takes, or 0 without an item.
  int64_t Cost() const;
  // Whether the purse covers it. False parks the cursor on [Cancel].
  bool Affordable() const;
  // Whether the cursor is on [Cancel] -- either because the player moved it
  // there, or because [Enhance] cannot be pressed. Derived rather than stored,
  // so it is right from the first frame: the panel is handed its item during
  // the render, long after the screen was opened.
  bool OnCancel() const;

  const EquipInstance* item_ = nullptr;
  int64_t meso_ = 0;
  ConfirmPrompt confirm_;
  bool confirmed_ = false;
  bool cancelled_ = false;
  bool cancel_selected_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_STAR_FORCE_PANEL_H_
