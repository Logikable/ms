/* AmountSelector is the shared quantity entry for dialogs that pick an integer
 * in [0, max]: a textbox between a low button and [MAX], above a
 * [Confirm]/[Cancel] row. The low button is [1] unless the caller sets it; a
 * trade uses [0], since offering nothing is reasonable there.
 *
 * The textbox is selected on Reset and is the only place digits and Backspace
 * edit the value, showing a blinking caret while selected. Left/Right move
 * within a row, Down moves from the top row to the buttons, and Up returns to
 * the textbox. Enter activates the focused control and Escape cancels.
 *
 * Zero is allowed: Backspace the textbox empty. The selector owns no game
 * state: Reset(max) sets it up, value() reports the choice, and OnEvent returns
 * the same ConfirmChoice every dialog in the game uses.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_
#define MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_

#include <cstdint>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class AmountSelector {
 public:
  // Sets up the control to choose 0..max, starting at max with the textbox
  // selected. The field widens to fit the largest number allowed, so a large
  // meso amount doesn't outgrow its box.
  void Reset(int64_t max);
  // As above, but starting at `initial`, clamped to 0..max, so a caller can
  // pass 1 with a max of 0 and get an empty field.
  void Reset(int64_t max, int64_t initial);
  // What the button left of the field sets. It is 1 unless this is called, and
  // must be called after Reset, which resets it.
  void set_low(int64_t low);
  // Greys out [Confirm] and stops it working, for a choice the caller can't
  // accept, such as a total beyond the player's meso. Cancel still works.
  // Cleared by the next Reset.
  void set_confirm_enabled(bool enabled);
  // The low / textbox / [MAX] row above a [Confirm] [Cancel] row, with a
  // divider.
  ftxui::Element Render() const;
  // Handles arrow navigation, Enter/Space activation, digit entry and Backspace
  // on the textbox, and Escape (cancel). Returns the answer, or kPending while
  // the player is still choosing.
  ConfirmChoice OnEvent(ftxui::Event event);
  int64_t value() const {
    return value_;
  }

 private:
  // The result of Enter on the focused control. The low and [MAX] buttons set
  // the value and return kPending, since the player is still choosing.
  ConfirmChoice Activate();

  int64_t max_ = 0;
  int64_t value_ = 0;
  // What the low button sets, and its label.
  int64_t low_ = 1;
  int focus_ = 0;  // an internal Focus value (see amount_selector.cc)
  bool confirm_enabled_ = true;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_
