/* AmountSelector is the shared quantity-entry control for modals that pick an
 * integer in [0, max]: a textbox flanked by [1] and [Max], over a
 * [Confirm]/[Cancel] row. The textbox is selected on Reset and is the only
 * place digits and Backspace edit the value, showing a blinking caret while
 * selected. Left/Right move within a row, Down drops from the top row to the
 * buttons, and Up returns to the textbox; Enter activates the focused control
 * and Escape cancels.
 *
 * A value of zero is allowed -- Backspace the textbox empty. The selector owns
 * no game state: Reset(max) seeds it, value() reports the choice, and OnEvent
 * answers with the ConfirmChoice every dialog in the game answers with.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_
#define MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class AmountSelector {
 public:
  // Seeds the control for choosing 0..max, defaulting the value to max with the
  // textbox selected.
  void Reset(int max);
  // As above, but opening at `initial`, which is clamped to 0..max -- so a
  // caller may pass 1 for a max of 0 and get the empty field that amount
  // deserves.
  void Reset(int max, int initial);
  // Greys out [Confirm] and stops it activating, for a choice the caller cannot
  // honour -- a total beyond the player's meso. Cancel still works. Cleared by
  // the next Reset.
  void set_confirm_enabled(bool enabled);
  // The [1] . textbox . [Max] row over a [Confirm] [Cancel] row, separated.
  ftxui::Element Render() const;
  // Handles arrow navigation, Enter/Space activation, digit entry and
  // Backspace on the textbox, and Escape (cancel). Returns which way the
  // answer went, kPending while the player is still choosing.
  ConfirmChoice OnEvent(ftxui::Event event);
  int value() const {
    return value_;
  }

 private:
  // What Enter on the focused control answered. The [1] and [Max] buttons set
  // the value and answer kPending: they are still the player choosing.
  ConfirmChoice Activate();

  int max_ = 0;
  int value_ = 0;
  int focus_ = 0;  // an internal Focus value (see amount_selector.cc)
  bool confirm_enabled_ = true;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_
