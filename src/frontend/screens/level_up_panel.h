/* LevelUpPanel is the confirm dialog for raising something one level: a
 * symbol, whose duplicates are already spent and which asks for the meso, or a
 * familiar, which asks for the EXP pool.
 *
 * The panel holds no game state. Reset() sets the name, its current level and
 * the price of the next, and OnEvent() reports the answer. A player who can't
 * pay gets a grey [Confirm] instead of a dialog that refuses them after they
 * press it.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_LEVEL_UP_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_LEVEL_UP_PANEL_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class LevelUpPanel {
 public:
  // Sets up the dialog for raising `name` from `level`. `price` is the cost
  // as shown ("1,810,000 meso"), and `affordable` whether it can be paid.
  void Reset(const std::string& name, int level, const std::string& price,
             bool affordable);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the price can be paid. The controller checks this before
  // spending, so the check the grey button shows is the one that is enforced.
  bool affordable() const {
    return affordable_;
  }

 private:
  std::string name_;
  int level_ = 1;
  std::string price_;
  bool affordable_ = false;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_LEVEL_UP_PANEL_H_
