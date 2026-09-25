/* HammerPanel is the confirm dialog for using a Golden Hammer on an item: one
 * more upgrade slot for a flat price.
 *
 * The panel holds no game state. Reset() sets up the purse the price is checked
 * against, and OnEvent() reports the answer. A player who can't pay gets a grey
 * [Confirm] instead of a dialog that refuses them after they press it.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_HAMMER_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_HAMMER_PANEL_H_

#include <cstdint>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class HammerPanel {
 public:
  // Sets up the dialog against a purse holding `meso`.
  void Reset(int64_t meso);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the purse covers a hammer. The controller checks this before
  // spending, so the check the grey button shows is the one that is enforced.
  bool affordable() const;

 private:
  int64_t meso_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HAMMER_PANEL_H_
