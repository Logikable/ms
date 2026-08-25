/* SymbolCombinePanel is the modal for feeding spare Arcane Symbols into the
 * one being worn. It names the symbol, says where its EXP stands against the
 * next level, and puts the shared AmountSelector under that. The amount opens
 * at every spare held: the last rung asks for 372 duplicates, and one keypress
 * each is not a thing to ask of anybody.
 *
 * The panel owns no game state: Reset() seeds it, quantity() reports the
 * choice, and TakeConfirmed() / TakeCancelled() each return true once.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SYMBOL_COMBINE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SYMBOL_COMBINE_PANEL_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/amount_selector.h"

namespace ms {

class SymbolCombinePanel {
 public:
  // Seeds the panel for feeding `spares` copies into a symbol at `level` that
  // has taken `exp` of the `needed` its next level asks for.
  void Reset(const std::string& symbol_name, int level, int exp, int needed,
             int spares);
  ftxui::Element Render() const;
  bool OnEvent(ftxui::Event event);
  int quantity() const {
    return selector_.value();
  }
  bool TakeConfirmed();
  bool TakeCancelled();

 private:
  std::string symbol_name_;
  int level_ = 1;
  int exp_ = 0;
  int needed_ = 0;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SYMBOL_COMBINE_PANEL_H_
