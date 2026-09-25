/* SymbolLevelPanel is the confirm dialog for raising an Arcane Symbol's level.
 * The duplicates are already spent by the time it opens; it asks for the meso,
 * the other half of the price.
 *
 * The panel holds no game state. Reset() sets the symbol's name, its current
 * level and the cost of the next, and OnEvent() reports the answer. A player
 * who can't pay gets a grey [Confirm] instead of a dialog that refuses them
 * after they press it.
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
  // Sets up the dialog for raising `symbol_name` from `level` for `cost` meso,
  // against a purse holding `meso`.
  void Reset(const std::string& symbol_name, int level, int64_t cost,
             int64_t meso);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the purse covers the level. The controller checks this before
  // spending, so the check the grey button shows is the one that is enforced.
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
