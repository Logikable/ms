/* AmountSelector is the shared quantity-entry control for modals that pick an
 * integer in [0, max]: a textbox flanked by [1] and [Max], over a
 * [Confirm]/[Cancel] row. The textbox is selected on Reset and is the only
 * place digits and Backspace edit the value, showing a blinking caret while
 * selected. Left/Right move within a row, Down drops from the top row to the
 * buttons, and Up returns to the textbox; Enter activates the focused control
 * and Escape cancels.
 *
 * A value of zero is allowed -- Backspace the textbox empty. The selector owns
 * no game state: Reset(max) seeds it, value() reports the choice, and
 * TakeConfirmed()/TakeCancelled() each return true once.
 */
#ifndef MS_SRC_FRONTEND_AMOUNT_SELECTOR_H_
#define MS_SRC_FRONTEND_AMOUNT_SELECTOR_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

class AmountSelector {
 public:
  // Seeds the control for choosing 0..max, defaulting the value to max with the
  // textbox selected.
  void Reset(int max);
  // The [1] . textbox . [Max] row over a [Confirm] [Cancel] row, separated.
  ftxui::Element Render() const;
  // Handles arrow navigation, Enter/Space activation, digit entry and Backspace
  // on the textbox, and Escape (cancel). Returns true if the event was
  // consumed.
  bool OnEvent(ftxui::Event event);
  int value() const {
    return value_;
  }
  // Each returns true exactly once, after Confirm / Cancel (or Escape), then
  // resets.
  bool TakeConfirmed();
  bool TakeCancelled();

 private:
  void Activate();

  int max_ = 0;
  int value_ = 0;
  int focus_ = 0;  // an internal Focus value (see amount_selector.cc)
  bool confirmed_ = false;
  bool cancelled_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_AMOUNT_SELECTOR_H_
