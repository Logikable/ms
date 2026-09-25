/* BuffInfoPanel is the Buffs tab's version of SkillInspectPanel: one buff's
 * name, its effects one line at a time, and its two prices: the rent charged
 * each time it triggers, and the cost of buying it outright.
 *
 * Every card is the same width, measured from the widest line of the widest
 * buff, so moving through the tab doesn't resize the window. Its height is its
 * own: a buff with fewer effects has a shorter card.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BUFF_INFO_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BUFF_INFO_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

class BuffInfoPanel {
 public:
  // Sets the card's buff and whether this character has bought it. An owned
  // buff is never charged again, so its prices read differently.
  void SetBuff(ConsumableType type, bool owned);

  ftxui::Element Render() const;

  // The width the card takes, borders included. The same for every buff.
  static int Columns();

 private:
  ConsumableType type_ = CONSUMABLE_TYPE_UNSPECIFIED;
  bool owned_ = false;
};

// The cost of one trigger of `type`, as the card and the tab show it: "🪙 1,000
// per second while farming". Empty for a type no buff describes.
std::string ConsumableRentText(ConsumableType type);

// The cost of buying it outright: "🪙 100,000,000 to unlock permanently".
std::string ConsumablePermanentText(ConsumableType type);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BUFF_INFO_PANEL_H_
