/* One row of an item list, as text.
 *
 * The equipped panel and the bag draw the same row, so they fill in the same
 * cells and hand them here rather than each laying out its own columns. Which
 * cells are drawn is not this file's question -- see ItemColumns.
 *
 * A row comes back with the byte span of every cell in it, because a caller
 * that colours one cell apart from the rest -- a name being pointed at, a
 * level the character has not reached -- cannot count the bytes itself: a name
 * may hold multibyte characters, and the columns come and go with the width.
 * The spans butt up against each other and cover the whole row, so a caller
 * can write it out cell by cell and lose nothing.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_
#define MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_

#include <chrono>
#include <string>

#include "src/character/character.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// What one item has to say in each column. A caller fills in the cells its
// list draws and leaves the rest; an empty cell comes out as blanks.
struct ItemCells {
  // The whole name. Cut to the name column and slid under it by the
  // formatter, which is the only place that knows how wide the column came
  // out.
  std::string name;
  std::string slot;
  std::string level;
  std::string job;
  std::string stats;
  std::string scroll;
  std::string stars;
  std::string potential;

  const std::string& Get(ItemColumn column) const;
};

// Where one cell sits in a row: its offset in bytes and how many it takes,
// the two blank columns in front of it included. Zero bytes for a column the
// row does not draw.
struct CellSpan {
  int offset = 0;
  int bytes = 0;
};

// A row, and where to find each of its cells.
struct ItemRowText {
  std::string text;
  CellSpan span[kNumItemColumns];

  CellSpan Span(ItemColumn column) const {
    return span[static_cast<int>(column)];
  }
};

// The stat cell of an item row: the attack the job swings with, and the stat
// its damage is built on. Two figures is all a column holds, and a wand
// carries both attacks.
std::string ItemStatsCell(Job job, const EquipStats& stats);

// The scroll, star force and potential cells of an item, which read the same
// wherever it is listed. An upgrade the item refuses reads "-", and so does
// an item carrying no potential: a blank would look like a column that failed
// to draw rather than an item with nothing there.
ItemCells EquipUpgradeCells(const EquipPrototype& proto, const Equip& state);

// `cells` laid out in `columns`. `elapsed` is how long this row has been the
// selected one, which is what slides a name too long for its column; zero --
// the default, and what every unselected row passes -- shows the head of the
// name and holds it there.
ItemRowText FormatItemRow(const ItemColumns& columns, const ItemCells& cells,
                          std::chrono::steady_clock::duration elapsed =
                              std::chrono::steady_clock::duration::zero());

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_
