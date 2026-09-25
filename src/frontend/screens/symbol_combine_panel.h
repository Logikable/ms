/* SymbolCombinePanel is the dialog for feeding spare Arcane Symbols into the
 * one being worn. It names the symbol, shows its EXP against the next level,
 * and puts the shared AmountSelector below. The amount starts at every spare
 * held, since the last level needs 372 duplicates and one keypress each would
 * be too much to ask.
 *
 * The amount counts spare items, which aren't worth one each: a claimed stack
 * carries twenty. The EXP row shows this, and it goes past the level's
 * requirement rather than stopping there, since the overflow goes toward the
 * next level.
 *
 * The panel holds no game state: Reset() sets it up, quantity() reports the
 * choice, and OnEvent returns the ConfirmChoice every dialog returns.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SYMBOL_COMBINE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SYMBOL_COMBINE_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/amount_selector.h"

namespace ms {

class SymbolCombinePanel {
 public:
  // Sets up the panel for feeding spares into a symbol at `level` that has
  // `exp` of the `needed` EXP for its next level. `spare_worths` is what each
  // spare is worth, in the order they would be used.
  void Reset(const std::string& symbol_name, int level, int exp, int needed,
             std::vector<int> spare_worths);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  int quantity() const {
    return selector_.value();
  }

 private:
  std::string symbol_name_;
  int level_ = 1;
  int exp_ = 0;
  int needed_ = 0;
  std::vector<int> spare_worths_;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SYMBOL_COMBINE_PANEL_H_
