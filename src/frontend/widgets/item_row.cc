#include "src/frontend/widgets/item_row.h"

#include <chrono>
#include <string>

#include "src/character/character.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

const std::string& ItemCells::Get(ItemColumn column) const {
  static_assert(kNumItemColumns == 8, "a new column needs a cell");
  const std::string* const cells[kNumItemColumns] = {
      &name, &slot, &level, &job, &stats, &scroll, &stars, &potential};
  return *cells[static_cast<int>(column)];
}

// The stat column of one row: the attack this job uses, then the stat its
// damage is based on.
std::string ItemStatsCell(Job job, const EquipStats& stats) {
  // One column for the stat this job's damage is based on. The character panel
  // and the AP reset ask the same question, so it is answered in one place
  // instead of switching over jobs here.
  StatField primary = PrimaryStatField(job);
  const DisplayStat* main = DisplayStatFor(primary);
  std::string main_str;
  if (main != nullptr && main->GetFrom(stats) > 0) {
    main_str = "+" + std::to_string(main->GetFrom(stats)) + " " + main->label;
  }
  // There is room for one attack figure, so show the one this job uses. A wand
  // carries both, and a magician's weapon attack never reaches the damage
  // formula. The choice follows the job's stat rather than a list of jobs, so a
  // new magician branch works without changes.
  std::string atk_str;
  bool magic = primary == STAT_FIELD_INT;
  if (!magic && stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  } else if (stats.magic_attack() > 0) {
    atk_str = "+" + std::to_string(stats.magic_attack()) + " MATT";
  } else if (stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  }
  // Attack comes first because it decides a weapon. The main stat comes second.
  return PadRight(atk_str, 10) + PadRight(main_str, 10);
}

ItemCells EquipUpgradeCells(const EquipPrototype& proto, const Equip& state,
                            Job job, int potential_width) {
  ItemCells cells;
  // The slot count is included so a row shows how far the item can still go,
  // not only how far it has come.
  int slots = TotalUpgradeSlots(proto, state);
  cells.scroll = slots > 0 ? std::to_string(state.scroll_successes()) + "/" +
                                 std::to_string(slots)
                           : "-";
  cells.stars = Supports(proto, UPGRADE_STAR_FORCE)
                    ? std::to_string(state.stars()) + "★"
                    : "-";
  cells.potential = PotentialCell(state.main_potential(),
                                  proto.required_level(), PrimaryStatField(job),
                                  SecondaryStatField(job), potential_width);
  return cells;
}

ItemRowText FormatItemRow(const ItemColumns& columns, const ItemCells& cells,
                          std::chrono::steady_clock::duration elapsed) {
  ItemRowText row;
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    if (!columns.Shows(column)) {
      continue;
    }
    int start = static_cast<int>(row.text.size());
    // The name follows the cursor column, which serves as its gap.
    if (column != ItemColumn::kName) {
      row.text.append(kItemCellGap, ' ');
    }
    row.text += column == ItemColumn::kName
                    ? ScrollingWindow(cells.name, columns.name_width, elapsed)
                    : PadRight(cells.Get(column), columns.Width(column));
    row.span[i] = {start, static_cast<int>(row.text.size()) - start};
  }
  return row;
}

}  // namespace ms
