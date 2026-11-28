/* The HEXA Stat card, from Inspect on a HEXA tab node: the node's
 * numeral in the title, then its main stat and two additional ones, each with
 * its amount, the chance the next enhance raises it, and its level. Read-only.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_HEXA_HEXA_STAT_CARD_H_
#define MS_SRC_FRONTEND_SCREENS_HEXA_HEXA_STAT_CARD_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

// Node `index` (0 for the first), named for a job whose primary stat is
// `main_stat`.
ftxui::Element HexaStatCard(int index, const HexaStatNode& node,
                            StatField main_stat);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HEXA_HEXA_STAT_CARD_H_
