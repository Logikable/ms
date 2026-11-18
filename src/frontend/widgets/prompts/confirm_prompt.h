/* ConfirmPrompt is the shared yes/no control: a [Confirm] / [Cancel] row that
 * tracks which button the cursor is on and swallows every event while open.
 * Left/Right move between the buttons, Enter or Space picks the highlighted
 * one, and Escape cancels.
 *
 * The prompt tracks whether it is showing, so a panel calls open() instead of
 * tracking that itself, and OnEvent closes it on either answer.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_CONFIRM_PROMPT_H_
#define MS_SRC_FRONTEND_WIDGETS_CONFIRM_PROMPT_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

// What an event did to an open prompt. kPending means the cursor moved (or the
// event was swallowed) and the prompt is still open.
enum class ConfirmChoice { kPending, kConfirmed, kCancelled };

// Which button in the confirm row is highlighted. kNone is for a caller whose
// cursor is elsewhere in the dialog, such as the amount selector's textbox.
enum class ConfirmFocus { kNone, kConfirm, kCancel };

// The [Confirm] / [Cancel] row, which is the same everywhere the game asks a
// yes/no question, whether a bare prompt or the bottom of a larger dialog.
ftxui::Element ConfirmButtons(ConfirmFocus focus);

// As above, with [Confirm] greyed out when that answer isn't allowed, the same
// dimming the item menu uses for an unavailable action. Cancel is never dimmed,
// since leaving is always possible.
ftxui::Element ConfirmButtons(ConfirmFocus focus, bool confirm_enabled);

// The width of ConfirmButtons, so a panel can size itself and not shift when
// the row appears below it.
constexpr int kConfirmButtonsWidth = 22;

class ConfirmPrompt {
 public:
  // The height of RenderWindow(), so a layout can reserve the rows while the
  // prompt is closed and not shift when it opens.
  static constexpr int kWindowHeight = 3;

  // Shows the prompt. cancel_selected starts the cursor on [Cancel], for an
  // action that shouldn't be one key press away.
  void Open(bool cancel_selected = false);
  void Close();
  bool open() const {
    return open_;
  }
  // Consumes every event while open, so a key meant for the prompt never
  // reaches the panel behind it, and closes on kConfirmed or kCancelled.
  //
  // With `confirm_enabled` false, Confirm isn't allowed: it returns kPending
  // and the prompt stays open, so the player sees the red row again rather than
  // the dialog disappearing as if something happened.
  ConfirmChoice OnEvent(ftxui::Event event, bool confirm_enabled = true);
  // Moves the cursor to [Cancel] on an open prompt, for when Confirm becomes
  // unavailable while the player is looking at it. The cubing window greys out
  // Confirm as soon as the player can't afford another roll, and the cursor
  // must not stay on a greyed button.
  void FocusCancel();
  // Which button the cursor is on, for a caller drawing the row itself. A
  // dialog that greys out Confirm needs both the focus and the enabled flag,
  // and only ConfirmButtons takes both.
  ConfirmFocus focus() const;
  // Just the button row, for a caller placing it inside its own window.
  ftxui::Element Render() const;
  // The button row centred in an untitled window, for placing below a panel.
  ftxui::Element RenderWindow() const;

 private:
  bool open_ = false;
  bool cancel_selected_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_CONFIRM_PROMPT_H_
