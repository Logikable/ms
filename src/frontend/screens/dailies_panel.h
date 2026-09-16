/* DailiesPanel is the question the Dailies menu entry asks: what today's claim
 * pays, and whether to take it.
 *
 * The rows are the reward, name on the left and count on the right, with a
 * column of clearance inside each border so nothing sits against the frame.
 *
 * The panel owns no game state: Reset() seeds it and OnEvent answers with the
 * ConfirmChoice every dialog answers with.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_DAILIES_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_DAILIES_PANEL_H_

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class DailiesPanel {
 public:
  // One row of the claim: what it is, and how many of it.
  struct Reward {
    std::string name;
    int count = 0;
  };

  // Seeds the panel with what the claim would pay, top to bottom.
  void Reset(std::vector<Reward> rewards);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);

 private:
  std::vector<Reward> rewards_;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_DAILIES_PANEL_H_
