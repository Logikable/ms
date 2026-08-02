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
#ifndef MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_
#define MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

// Whether the [1] and [MAX] shortcuts sit beside the value field. A shop hides
// them: buying opens at one, and "as many as I can afford" is not an amount
// anyone reaches for.
enum class QuickPicks { kShown, kHidden };

class AmountSelector {
 public:
  // Seeds the control for choosing 0..max, defaulting the value to max with the
  // textbox selected.
  void Reset(int max);
  // As above, but opening at `initial` and optionally without the shortcuts.
  // `initial` is clamped to 0..max, so a caller may pass 1 for a max of 0 and
  // get the empty field that amount deserves.
  void Reset(int max, int initial, QuickPicks quick_picks);
  // Greys out [Confirm] and stops it activating, for a choice the caller cannot
  // honour -- a total beyond the player's meso. Cancel still works. Cleared by
  // the next Reset.
  void set_confirm_enabled(bool enabled);
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
  QuickPicks quick_picks_ = QuickPicks::kShown;
  bool confirm_enabled_ = true;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_AMOUNT_SELECTOR_H_
