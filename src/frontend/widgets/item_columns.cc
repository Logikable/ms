#include "src/frontend/widgets/item_columns.h"

#include <algorithm>
#include <string>

#include "src/frontend/widgets/format.h"

namespace ms {
namespace {

// Each column's width, set by the widest thing in it: "Equip Slot", "Lv150",
// "Magician", an attack figure next to a stat figure, and the "Scroll" heading
// over "7/7" and "25*". The potential column sets its own limits (see
// kItemPotentialWidth).
constexpr int kSlotWidth = 10;
constexpr int kLevelWidth = 5;
constexpr int kJobWidth = 8;
constexpr int kStatsWidth = 20;
constexpr int kScrollWidth = 6;
constexpr int kStarsWidth = 5;

// Whether the mechanic behind `column` is unlocked. The name and slot columns
// always show, since every item has both.
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
      return potential_width;
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
    // The name follows the cursor column, which serves as its gap.
    total += Width(column) + (column == ItemColumn::kName ? 0 : kItemCellGap);
  }
  return total;
}

ItemColumns FitItemColumns(int width, const ItemListOptions& options) {
  ItemColumns columns;
  // The name is placed before anything is measured, since a list needs names.
  // Every other column depends on the room left.
  columns.shown[static_cast<int>(ItemColumn::kName)] = true;
  int left = width - columns.TotalWidth();
  for (ItemColumn column : kItemColumnPriority) {
    if (column == ItemColumn::kName || !Eligible(column, options)) {
      continue;
    }
    // Mark it shown before measuring, because Width returns zero for a hidden
    // column.
    columns.shown[static_cast<int>(column)] = true;
    int cost = kItemCellGap + columns.Width(column);
    if (cost > left) {
      // The first column that doesn't fit ends the list. A narrower one further
      // down might fit, but taking it would put the columns in an order the
      // player didn't ask for.
      columns.shown[static_cast<int>(column)] = false;
      break;
    }
    left -= cost;
  }
  // Leftover room goes to the potential column first, then to the name up to
  // the longest name. A long name can still scroll under the cursor, but a
  // second effect has nowhere else to show.
  if (columns.Shows(ItemColumn::kPotential)) {
    int grow = std::clamp(left, 0, kItemPotentialMax - columns.potential_width);
    columns.potential_width += grow;
    left -= grow;
  }
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
  // The last column's padding is blank up to the border, so it is trimmed.
  header.erase(header.find_last_not_of(' ') + 1);
  return header;
}

}  // namespace ms
