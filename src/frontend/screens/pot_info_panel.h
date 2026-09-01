/* PotInfoPanel is the Pots tab's counterpart to SkillInspectPanel: one pot's
 * name, what it is worth a line at a time, and the two prices it can be had
 * for -- the rent charged every time it procs, and what buying it outright
 * costs.
 *
 * Every card is the same width and the same height, measured from the widest
 * and longest pot in the table, so walking the tab does not resize the window
 * under the cursor. A pot with fewer effects than the longest pads with blank
 * rows rather than shrinking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_POT_INFO_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_POT_INFO_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

class PotInfoPanel {
 public:
  // Seeds the card: which pot, and whether this character has bought it. An
  // owned pot is never charged again, so its prices read differently.
  void SetPot(ConsumableType type, bool owned);

  ftxui::Element Render() const;

  // The columns the card takes, borders included. The same for every pot.
  static int Columns();

 private:
  ConsumableType type_ = CONSUMABLE_TYPE_UNSPECIFIED;
  bool owned_ = false;
};

// What one proc of `type` costs, as the card and the tab both state it --
// "🪙 1,000 per second while farming". Empty for a type no pot describes.
std::string ConsumableRentText(ConsumableType type);

// And what buying it outright costs: "🪙 100,000,000 to unlock permanently".
std::string ConsumablePermanentText(ConsumableType type);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_POT_INFO_PANEL_H_
