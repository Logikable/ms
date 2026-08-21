/* ContinuePrompt is the shared one-button control: a [Continue] the player
 * presses to dismiss something the game is only telling them. It is the
 * counterpart to ConfirmPrompt, for a dialog that asks nothing -- a result, or
 * a notice like a boss already cleared this reset.
 *
 * The prompt owns whether it is showing, and swallows every event while it is
 * open so a keystroke meant for it cannot reach the screen behind.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_CONTINUE_PROMPT_H_
#define MS_SRC_FRONTEND_WIDGETS_CONTINUE_PROMPT_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

class ContinuePrompt {
 public:
  // Height RenderWindow() occupies, so a layout can reserve the rows while the
  // prompt is closed and not shift when it opens.
  static constexpr int kWindowHeight = 3;

  void Open();
  void Close();
  bool open() const {
    return open_;
  }
  // True when the event dismissed the prompt, which closes it. Everything else
  // is swallowed while it is open.
  bool OnEvent(ftxui::Event event);
  // The bare button, for a caller placing it inside its own window.
  ftxui::Element Render() const;
  // The button centered in a titleless window, for standing on its own.
  ftxui::Element RenderWindow() const;

 private:
  bool open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_CONTINUE_PROMPT_H_
