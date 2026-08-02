#ifndef MS_SRC_FRONTEND_MAIN_LAYOUT_H_
#define MS_SRC_FRONTEND_MAIN_LAYOUT_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// Arranges the five pieces of the main view: a left column of the character
// panel over the combat panel, a right column of the equipped panel over the
// bag, and the exp bar across the foot.
//
// Split out of Tui::RenderMain so it can be measured in a test. Every rule
// below was a bug on screen first:
//
//   - Combat belongs in the left COLUMN, not in a row of its own beneath both.
//     As a row it capped the column beside it at its own top edge and left the
//     width of the bag blank below that. It is CharacterPanel::kTotalWidth
//     wide, same as the character panel, so the two stack cleanly.
//   - The bag shrinks but does not grow, so an empty tab is a few rows rather
//     than a screen of blank. Below its own height it hands over to the list
//     inside it, which scrolls.
//   - Marking the bag as the one thing that shrinks is also what protects the
//     panel above it: a column with nothing shrinkable in it takes ftxui's
//     other path and squashes every panel a share of the overflow instead, so
//     a full bag would cost the equipped panel a row.
//   - Each column is a vbox because the hbox holding them stretches a bare
//     child to the full height of the row.
ftxui::Element MainLayout(ftxui::Element character, ftxui::Element combat,
                          ftxui::Element equipped, ftxui::Element inventory,
                          ftxui::Element exp_bar);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAIN_LAYOUT_H_
