/* FamiliarCard is a familiar's inspect card, framed like an equip's: a level
 * bar where an equip has its stars, the name, what it is, and its two lines
 * under a rule, the rank named once above them.
 *
 * Drawn alone behind the Familiar tab's Inspect, and beside the list on the
 * Switch and Cube screens. It reads the book every frame, so a level-up or a
 * cube shows at once.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_FAMILIAR_CARD_H_
#define MS_SRC_FRONTEND_SCREENS_FAMILIAR_CARD_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/scroll_card.h"
#include "src/protos/familiar.pb.h"

namespace ms {

class FamiliarCard {
 public:
  // The familiar `species` names in `book`, which must outlive the card's
  // renders. An empty species draws a placeholder.
  void SetFamiliar(const FamiliarBook* book, const std::string& species);
  // See ScrollCard.
  void SetMaxRows(int rows) {
    card_.SetMaxRows(rows);
  }
  void ScrollBy(int delta) {
    card_.ScrollBy(delta);
  }
  void Reset() {
    card_.Reset();
  }
  ftxui::Element Render(bool focused = false,
                        const std::string& title = " Inspect ") const;

 private:
  const FamiliarBook* book_ = nullptr;
  std::string species_;
  ScrollCard card_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_FAMILIAR_CARD_H_
