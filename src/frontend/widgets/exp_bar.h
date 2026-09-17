/* The bar across the foot of a character's screen: how far into their level
 * they are, as a fraction and a figure.
 *
 * Here rather than on the main view because the Inspect screen draws one for
 * a party member, and a member's bar has to mean what the player's does.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_EXP_BAR_H_
#define MS_SRC_FRONTEND_WIDGETS_EXP_BAR_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

// A one-row bar for `character`. At the cap it reads MAX and sits full: there
// is no next level to fill towards, and an empty bar would read as the EXP
// having been taken away.
ftxui::Element ExpBar(const Character& character);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_EXP_BAR_H_
