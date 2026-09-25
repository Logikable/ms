/* One row of an item list, as text.
 *
 * The equipped panel and the bag draw the same row, so they fill in the same
 * cells and pass them here instead of each laying out its own columns. Which
 * cells are drawn is decided by ItemColumns.
 *
 * A row comes back with the byte span of each cell. A caller that colours one
 * cell differently (a selected name, a level the character hasn't reached)
 * can't count the bytes itself, because names may hold multibyte characters and
 * columns come and go with the width. The spans are contiguous and cover the
 * whole row, so a caller can write it out cell by cell and lose nothing.
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

// What one item shows in each column. A caller fills in the cells its list
// draws and leaves the rest. An empty cell comes out as blanks.
struct ItemCells {
  // The full name. The formatter cuts it to the name column and scrolls it,
  // since only the formatter knows how wide the column is.
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

// Where one cell sits in a row: its byte offset and length, including the two
// blank columns before it. Zero bytes for a column the row doesn't draw.
struct CellSpan {
  int offset = 0;
  int bytes = 0;
};

// A row, and where each of its cells is.
struct ItemRowText {
  std::string text;
  CellSpan span[kNumItemColumns];

  CellSpan Span(ItemColumn column) const {
    return span[static_cast<int>(column)];
  }
};

// The stat cell of an item row: the attack the job uses, and the stat its
// damage is based on. A column holds only two figures, and a wand carries both
// attacks.
std::string ItemStatsCell(Job job, const EquipStats& stats);

// The scroll, star force and potential cells of an item, which read the same
// wherever it is listed. An upgrade the item can't take reads "-", and so does
// a potential with nothing this job uses. A blank would look like a column that
// failed to draw. `potential_width` is the column's width, which decides how
// many effects the cell names.
ItemCells EquipUpgradeCells(const EquipPrototype& proto, const Equip& state,
                            Job job, int potential_width);

// `cells` laid out in `columns`. `elapsed` is how long this row has been
// selected, which scrolls a name too long for its column. Zero, the default and
// what every unselected row passes, shows the start of the name and keeps it
// there.
ItemRowText FormatItemRow(const ItemColumns& columns, const ItemCells& cells,
                          std::chrono::steady_clock::duration elapsed =
                              std::chrono::steady_clock::duration::zero());

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_
