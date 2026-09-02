#include "src/frontend/widgets/item_columns.h"

#include <algorithm>
#include <string>

#include "src/frontend/widgets/format.h"

namespace ms {
namespace {

// What each column holds, at the width of the widest thing in it: "Equip
// Slot" itself, "Lv150", "Magician", an attack figure beside a stat figure,
// the "Scroll" heading over "7/7", "25*" and the widest potential total the
// game rolls.
constexpr int kSlotWidth = 10;
constexpr int kLevelWidth = 5;
constexpr int kJobWidth = 8;
constexpr int kStatsWidth = 20;
constexpr int kScrollWidth = 6;
constexpr int kStarsWidth = 5;
constexpr int kPotentialWidth = 12;

// Whether the mechanic behind `column` is open. The name and the slot answer
// to nothing: an item always has both.
bool Eligible(ItemColumn column, const ItemListOptions& options) {
  switch (column) {
    case ItemColumn::kLevel:
    case ItemColumn::kJob:
      return options.bag;
    case ItemColumn::kScroll:
      return options.scrolling;
    case ItemColumn::kStars:
      return options.star_force;
    case ItemColumn::kPotential:
      return options.potential;
    default:
      return true;
  }
}

}  // namespace

int ItemColumns::Width(ItemColumn column) const {
  if (!Shows(column)) {
    return 0;
  }
  switch (column) {
    case ItemColumn::kName:
      return name_width;
    case ItemColumn::kSlot:
      return kSlotWidth;
    case ItemColumn::kLevel:
      return kLevelWidth;
    case ItemColumn::kJob:
      return kJobWidth;
    case ItemColumn::kStats:
      return kStatsWidth;
    case ItemColumn::kScroll:
      return kScrollWidth;
    case ItemColumn::kStars:
      return kStarsWidth;
    case ItemColumn::kPotential:
      return kPotentialWidth;
  }
  return 0;
}

int ItemColumns::TotalWidth() const {
  int total = kItemListCursor + kItemListGutter;
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    if (!Shows(column)) {
      continue;
    }
    // The name follows the cursor with no gap of its own -- the cursor column
    // is the gap.
    total += Width(column) + (column == ItemColumn::kName ? 0 : kItemCellGap);
  }
  return total;
}

ItemColumns FitItemColumns(int width, const ItemListOptions& options) {
  ItemColumns columns;
  // The name is seated before anything is measured: a list of items with no
  // names is not a list. Everything else answers to the room left.
  columns.shown[static_cast<int>(ItemColumn::kName)] = true;
  int left = width - columns.TotalWidth();
  for (ItemColumn column : kItemColumnPriority) {
    if (column == ItemColumn::kName || !Eligible(column, options)) {
      continue;
    }
    // Seated first, then measured: Width answers zero for a column that is
    // not drawn.
    columns.shown[static_cast<int>(column)] = true;
    int cost = kItemCellGap + columns.Width(column);
    if (cost > left) {
      // The first column that does not fit ends the list. A narrower one
      // further down would fit, and taking it would put the lists' columns in
      // an order the player never asked for.
      columns.shown[static_cast<int>(column)] = false;
      break;
    }
    left -= cost;
  }
  // Whatever nothing claimed goes to the name, up to the longest name there
  // is to show.
  columns.name_width =
      std::clamp(kItemNameWidth + left, kItemNameWidth, kItemNameMax);
  return columns;
}

const char* ItemColumnHeader(ItemColumn column) {
  switch (column) {
    case ItemColumn::kName:
      return "Name";
    case ItemColumn::kSlot:
      return "Equip Slot";
    case ItemColumn::kLevel:
      return "Level";
    case ItemColumn::kJob:
      return "Job";
    case ItemColumn::kStats:
      return "Stats";
    case ItemColumn::kScroll:
      return "Scroll";
    case ItemColumn::kStars:
      return "Stars";
    case ItemColumn::kPotential:
      return "Potential";
  }
  return "";
}

std::string ItemListHeader(const ItemColumns& columns) {
  std::string header(kItemListCursor, ' ');
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    if (!columns.Shows(column)) {
      continue;
    }
    if (column != ItemColumn::kName) {
      header.append(kItemCellGap, ' ');
    }
    header += PadRight(ItemColumnHeader(column), columns.Width(column));
  }
  // The last column's padding is blank to the border and worth nothing.
  header.erase(header.find_last_not_of(' ') + 1);
  return header;
}

}  // namespace ms
