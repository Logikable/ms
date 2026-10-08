/* The rows of a familiar list: the Equipped panel's Familiar tab, which lists
 * a preset's three, and the Switch screen, which lists the whole roster with
 * an In Use column. Both draw the same cells, built here.
 *
 * A row's name is the player's for the familiar, and its Mob the roster's;
 * the two are the same until the player renames it.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_FAMILIAR_LIST_H_
#define MS_SRC_FRONTEND_WIDGETS_FAMILIAR_LIST_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/protos/equip.pb.h"
#include "src/protos/familiar.pb.h"

namespace ms {

// The widths of a familiar list's cells, not counting the cursor or the gaps.
// The Name and Mob columns are as wide as the longest name either can hold,
// and the Potential column takes what is left but a blank column before the
// border.
struct FamiliarColumns {
  int name = 0;
  int mob = 0;
  int level = 0;
  int potential = 0;
  // Whether the list has the In Use column, which only the Switch screen has.
  bool in_use = false;
};
// The columns for a list `width` wide, the cursor and the blank column at the
// end included.
FamiliarColumns FitFamiliarColumns(int width, bool in_use);
// The header over them, the cursor's columns included.
std::string FamiliarHeader(const FamiliarColumns& columns);

// One familiar's cells, each padded to its column.
struct FamiliarCells {
  std::string name;
  std::string mob;
  std::string level;
  std::string potential;
  std::string in_use;
  // Colours the potential. UNSPECIFIED, a familiar at level 0, leaves it plain.
  PotentialRank rank = POTENTIAL_RANK_UNSPECIFIED;
};
FamiliarCells FamiliarRowCells(const FamiliarBook& book,
                               const std::string& species,
                               const FamiliarColumns& columns, bool in_use);

// `cursor` and the cells as one line, the potential in its rank's colour.
// `name`, when given, is drawn in the name cell's place: the rename field.
ftxui::Element FamiliarRowElement(const std::string& cursor,
                                  const FamiliarCells& cells,
                                  ftxui::Element name = nullptr);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_FAMILIAR_LIST_H_
