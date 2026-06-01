#ifndef MS_SRC_FRONTEND_CHARACTER_PANEL_H_
#define MS_SRC_FRONTEND_CHARACTER_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/character.h"

namespace ms {

ftxui::Element CharacterElement(const CharacterInstance& c);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
