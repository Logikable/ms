/* Which columns an item list draws, and how wide each one is.
 *
 * The equipped panel and the bag's Equip tab list the same items in the same
 * columns, at whatever width the terminal has left them. A narrow panel
 * cannot hold all of them, so the columns are ranked and the list takes them
 * in that order until the room runs out -- and stops there. A lower-ranked
 * column never slips in past one that did not fit, however narrow it is.
 *
 * A column is also gated on its mechanic: potential, star force and scrolling
 * each appear only once the account has unlocked them, and the level and job
 * columns belong to the bag alone -- the equipped list is wearing the item, so
 * it has already answered both.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_ITEM_COLUMNS_H_
#define MS_SRC_FRONTEND_WIDGETS_ITEM_COLUMNS_H_

#include <string>

namespace ms {

// The columns of an item list, in the order they are drawn. Their ranking is
// a separate order -- see kItemColumnPriority.
enum class ItemColumn {
  kName,
  kSlot,
  kLevel,
  kJob,
  kStats,
  kScroll,
  kStars,
  kPotential,
};
inline constexpr int kNumItemColumns = 8;

// Which column gives way to which. Name and slot say what the item is; the
// three upgrades are what the player is working on; the level and job gates
// and the stats are read on the item's own card.
inline constexpr ItemColumn kItemColumnPriority[kNumItemColumns] = {
    ItemColumn::kName,  ItemColumn::kSlot,   ItemColumn::kPotential,
    ItemColumn::kStars, ItemColumn::kScroll, ItemColumn::kLevel,
    ItemColumn::kJob,   ItemColumn::kStats,
};

// The name column at its narrowest, on a panel that can spare nothing. Names
// run past it -- "Fafnir Windwing Shooter Trace" does -- so a name is cut to
// the column and slides under it while selected; see ScrollingWindow.
inline constexpr int kItemNameWidth = 26;

// And at its widest: the longest name the game ships, a trace's " Trace"
// included. Past this the columns after the name would be pushed away from it
// for no name's sake.
inline constexpr int kItemNameMax = 38;

// The column inside the left border, which the cursor caret writes into, and
// the blank column the rows keep inside the right border.
inline constexpr int kItemListCursor = 2;
inline constexpr int kItemListGutter = 1;

// The blank columns between one cell and the next.
inline constexpr int kItemCellGap = 2;

// What a list is allowed to show. The three mechanics are the account's --
// ask Unlocked(Feature::kPotential, ...) and its two neighbours.
struct ItemListOptions {
  // The bag, which lists items the character may not be able to wear. The
  // equipped list leaves both gate columns out.
  bool bag = false;
  bool scrolling = false;
  bool star_force = false;
  bool potential = false;
};

// The columns one list draws, and the room its name column got.
struct ItemColumns {
  int name_width = kItemNameWidth;
  bool shown[kNumItemColumns] = {};

  bool Shows(ItemColumn column) const {
    return shown[static_cast<int>(column)];
  }
  // The room `column` gets, name column included. Zero for one not drawn.
  int Width(ItemColumn column) const;
  // Everything the row spends: the cursor column, the cells with the gaps
  // between them, and the gutter.
  int TotalWidth() const;
};

// The columns that fit an item list `width` columns wide, cursor and gutter
// included. Taken in priority order and stopped at the first one that does
// not fit; whatever is left over afterwards goes to the name.
ItemColumns FitItemColumns(int width, const ItemListOptions& options);

// What `column` is called at the head of a list.
const char* ItemColumnHeader(ItemColumn column);

// The header row over a list drawing `columns`, cursor column and all.
std::string ItemListHeader(const ItemColumns& columns);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_COLUMNS_H_
