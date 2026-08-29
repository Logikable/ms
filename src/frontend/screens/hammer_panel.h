/* HammerPanel is the confirm dialog for driving a golden hammer into a piece
 * of equipment: one more upgrade slot for a flat price.
 *
 * The panel owns no game state. Reset() seeds it with the purse the price is
 * being asked of; OnEvent() reports which way the answer went. A player who
 * cannot pay gets a greyed [Confirm] rather than a dialog that refuses them
 * after they press it.
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
  // Seeds the dialog against a purse holding `meso`.
  void Reset(int64_t meso);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the purse covers a hammer. The controller asks before spending,
  // so the check the greyed button shows is the check that is enforced.
  bool affordable() const;

 private:
  int64_t meso_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HAMMER_PANEL_H_
