/* StarForcePanel draws the star force screen for one item: the item name, its
 * current stars, the stat gains, the odds and the cost of an attempt. At the
 * bottom is an [Enhance] / [Cancel] row: Enter on Enhance opens the inline
 * confirm bar, a second Enter confirms, and Enter on Cancel leaves. OnEvent
 * returns the ConfirmChoice every dialog returns.
 *
 * A player who can't afford the attempt isn't stopped at the confirm bar: the
 * price is red, [Enhance] is greyed and unreachable, and the cursor is already
 * on [Cancel].
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
  // The item together with the purse that pays for it, since the cost and the
  // money available are read together, and one without the other would show a
  // price nobody checked.
  void SetItem(const EquipInstance* item, int64_t meso);
  ftxui::Element Render() const;
  ftxui::Element RenderResult(const StarForceResult& r) const;
  // Enter presses the button or advances the confirm bar, Esc cancels the
  // confirm, and Left and Right switch buttons. kCancelled means [Cancel], on
  // which the caller closes the screen; the prompt's own Cancel returns
  // kPending.
  ConfirmChoice OnEvent(ftxui::Event event);
  void ResetConfirm();
  bool IsConfirming() const {
    return confirm_.open();
  }

 private:
  // The cost of one attempt on the current item, or 0 without an item.
  int64_t Cost() const;
  // Whether the purse covers it. If not, the cursor stays on [Cancel].
  bool Affordable() const;
  // Whether the cursor is on [Cancel], either because the player moved it there
  // or because [Enhance] can't be pressed. Computed rather than stored so it is
  // right from the first frame, since the panel gets its item during the
  // render, well after the screen opened.
  bool OnCancel() const;

  const EquipInstance* item_ = nullptr;
  int64_t meso_ = 0;
  ConfirmPrompt confirm_;
  bool cancel_selected_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_STAR_FORCE_PANEL_H_
