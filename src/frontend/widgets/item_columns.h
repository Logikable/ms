/* Which columns an item list draws, and how wide each is.
 *
 * The equipped panel and the bag's Equip tab list the same items in the same
 * columns, at whatever width the terminal leaves. A narrow panel can't fit them
 * all, so the columns are ranked and taken in order until the room runs out. A
 * lower-ranked column never takes the place of one that didn't fit, however
 * narrow.
 *
 * An upgrade column (scroll, stars and both potentials) appears once the
 * account unlocks its mechanic, or earlier if an item in the list already
 * carries it. The level and job columns appear only in the bag, since a worn
 * item already meets both.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_ITEM_COLUMNS_H_
#define MS_SRC_FRONTEND_WIDGETS_ITEM_COLUMNS_H_

#include <string>

namespace ms {

// The columns of an item list, in drawing order. Their ranking is separate (see
// kItemColumnPriority).
enum class ItemColumn {
  kName,
  kSlot,
  kLevel,
  kJob,
  kStats,
  kScroll,
  kStars,
  kPotential,
  kBonusPotential,
};
inline constexpr int kNumItemColumns = 9;

// The columns' ranking, highest first. Name and slot say what the item is. The
// upgrades are what the player works on. The level and job gates and the stats
// can be read on the item's card.
inline constexpr ItemColumn kItemColumnPriority[kNumItemColumns] = {
    ItemColumn::kName,           ItemColumn::kSlot,  ItemColumn::kPotential,
    ItemColumn::kBonusPotential, ItemColumn::kStars, ItemColumn::kScroll,
    ItemColumn::kLevel,          ItemColumn::kJob,   ItemColumn::kStats,
};

// The name column at its narrowest. Longer names, such as "Fafnir Windwing
// Shooter Trace", are cut to the column and scroll while selected (see
// ScrollingWindow).
inline constexpr int kItemNameWidth = 26;

// The name column at its widest: the longest name in the game, including a
// trace's " Trace". Any wider would push the following columns away for
// nothing.
inline constexpr int kItemNameMax = 38;

// The column inside the left border where the cursor caret goes, plus the blank
// column kept inside the right border.
inline constexpr int kItemListCursor = 2;
inline constexpr int kItemListGutter = 1;

// The blank columns between one cell and the next.
inline constexpr int kItemCellGap = 2;

// The potential column at its narrowest, holding one effect. "24% Crit DMG" is
// the widest one.
inline constexpr int kItemPotentialWidth = 12;

// The potential columns at their widest: all three of an item's lines, each as
// one effect, with gaps. Gloves and rings are read for several effects at once,
// so spare room goes here before the name.
inline constexpr int kItemPotentialMax =
    3 * kItemPotentialWidth + 2 * kItemCellGap;

// Beside a bonus potential column the main one is headed "Main Potential", and
// neither column narrows past its header.
inline constexpr int kItemMainPotentialWidth = 14;
inline constexpr int kItemBonusPotentialWidth = 15;

// Which upgrade columns a list may show (see ItemListOptionsFor).
struct ItemListOptions {
  // True for the bag, which lists items the character may not be able to wear.
  // The equipped list leaves out both gate columns.
  bool bag = false;
  bool scrolling = false;
  bool star_force = false;
  bool potential = false;
  bool bonus_potential = false;
};

// The columns a list draws, and the widths its stretchable columns got.
struct ItemColumns {
  int name_width = kItemNameWidth;
  int potential_width = kItemPotentialWidth;
  int bonus_potential_width = kItemBonusPotentialWidth;
  bool shown[kNumItemColumns] = {};

  bool Shows(ItemColumn column) const {
    return shown[static_cast<int>(column)];
  }
  // What `column` is called at the head of the list.
  const char* Header(ItemColumn column) const;
  // The width `column` gets, including the name column. Zero for a column not
  // drawn.
  int Width(ItemColumn column) const;
  // The row's full width: the cursor column, the cells and the gaps between
  // them, and the gutter.
  int TotalWidth() const;
};

// The columns that fit an item list `width` wide, including the cursor and
// gutter. They are taken in priority order, stopping at the first that doesn't
// fit. Leftover room widens the potential columns, then the name.
ItemColumns FitItemColumns(int width, const ItemListOptions& options);

// The header row over a list drawing `columns`, cursor column and all.
std::string ItemListHeader(const ItemColumns& columns);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_COLUMNS_H_
