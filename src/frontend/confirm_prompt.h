/* ConfirmPrompt is the shared yes/no control: a [Confirm] / [Cancel] row that
 * owns which button the cursor is on and swallows every event while it is
 * open. Left/Right move between the buttons, Enter or Space picks the
 * highlighted one, and Escape cancels.
 *
 * The prompt owns whether it is showing, so a panel asks open() rather than
 * tracking that itself, and OnEvent closes it on either answer.
 */
#ifndef MS_SRC_FRONTEND_CONFIRM_PROMPT_H_
#define MS_SRC_FRONTEND_CONFIRM_PROMPT_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

// What an event did to an open prompt. kPending means the cursor moved (or the
// event was swallowed) and the prompt is still up.
enum class ConfirmChoice { kPending, kConfirmed, kCancelled };

class ConfirmPrompt {
 public:
  // Height RenderWindow() occupies, so a layout can reserve the same rows while
  // the prompt is closed and not shift when it opens.
  static constexpr int kWindowHeight = 3;

  // Shows the prompt. cancel_selected starts the cursor on [Cancel], for an
  // action that should not be one keystroke away.
  void Open(bool cancel_selected = false);
  void Close();
  bool open() const {
    return open_;
  }
  // Consumes every event while open, so a keystroke meant for the prompt can
  // never reach the panel behind it. Returns which way the answer went and
  // closes the prompt on kConfirmed or kCancelled.
  ConfirmChoice OnEvent(ftxui::Event event);
  // The bare button row, for a caller placing it inside its own window.
  ftxui::Element Render() const;
  // The button row centered in a titleless window, for sitting below a panel.
  ftxui::Element RenderWindow() const;

 private:
  bool open_ = false;
  bool cancel_selected_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CONFIRM_PROMPT_H_
