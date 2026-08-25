/* SymbolLevelPanel is the confirm dialog for raising an Arcane Symbol a level.
 * The duplicates are already spent by the time it opens -- what it asks for is
 * the meso, which is the other half of the price.
 *
 * The panel owns no game state. Reset() seeds it with the symbol's name, the
 * level it is leaving, and what the rung costs; OnEvent() reports which way
 * the answer went. A player who cannot pay gets a greyed [Confirm] rather than
 * a dialog that refuses them after they press it.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SYMBOL_LEVEL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SYMBOL_LEVEL_PANEL_H_

#include <cstdint>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class SymbolLevelPanel {
 public:
  // Seeds the dialog for taking `symbol_name` out of `level` for `cost` meso,
  // against a purse holding `meso`.
  void Reset(const std::string& symbol_name, int level, int64_t cost,
             int64_t meso);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the purse covers the rung. The controller asks before spending,
  // so the check the greyed button shows is the check that is enforced.
  bool affordable() const {
    return meso_ >= cost_;
  }

 private:
  std::string symbol_name_;
  int level_ = 1;
  int64_t cost_ = 0;
  int64_t meso_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SYMBOL_LEVEL_PANEL_H_
