/* ContinuePrompt is the shared one-button control: a [Continue] the player
 * presses to dismiss something the game is only telling them. It is the
 * counterpart to ConfirmPrompt, for a dialog that asks nothing, such as a
 * result or a notice that a boss has already been cleared this reset.
 *
 * The prompt tracks whether it is showing, and swallows every event while open
 * so a key meant for it can't reach the screen behind.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_CONTINUE_PROMPT_H_
#define MS_SRC_FRONTEND_WIDGETS_CONTINUE_PROMPT_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

class ContinuePrompt {
 public:
  // The height of RenderWindow(), so a layout can reserve the rows while the
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
  // Just the button, for a caller placing it inside its own window. `label`
  // renames it for a dialog the player is closing rather than continuing from.
  ftxui::Element Render(const std::string& label = "Continue") const;
  // The button centred in an untitled window, for use on its own.
  ftxui::Element RenderWindow() const;

 private:
  bool open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_CONTINUE_PROMPT_H_
