/* The bar along the bottom of a character's screen: progress through the
 * current level, as a fraction and a number.
 *
 * It lives here rather than in the main view because the Inspect screen draws
 * one for a party member, and it must mean the same thing as the player's.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_EXP_BAR_H_
#define MS_SRC_FRONTEND_WIDGETS_EXP_BAR_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

// A one-row bar for `character`. At the level cap it reads MAX and is full:
// there is no next level, and an empty bar would look like EXP had been lost.
ftxui::Element ExpBar(const Character& character);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_EXP_BAR_H_
