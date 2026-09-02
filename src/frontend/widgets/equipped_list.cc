#include "src/frontend/widgets/equipped_list.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/slot_order.h"

namespace ms {
namespace {

// A symbol list's columns. The name gets more room than an item list's 26
// because every symbol is called "Arcane Symbol: <area>", and 26 cuts the
// longest of them mid-word -- there are only three columns after it to pay
// for the room. The other two are sized to the widest they hold: level 20,
// and the 372/372 the last rung asks for.
constexpr int kSymbolNameWidth = 32;
constexpr int kSymbolLevelWidth = 3;
constexpr int kSymbolExpWidth = 7;

// How far along its next level a symbol is, or "MAX" for one that has no next
// level to be along.
std::string SymbolExpCell(const Equip& state) {
  int needed = SymbolExpToNextLevel(SymbolLevel(state));
  if (needed == 0) {
    return "MAX";
  }
  return std::to_string(state.symbol_exp()) + "/" + std::to_string(needed);
}

}  // namespace

const char kSymbolHeader[] =
    "  Name                            "  // 2 cursor + 32 name
    "  Lv "                               // 2 sep + 3 level
    "  EXP    "                           // 2 sep + 7 exp
    "  AF";                               // 2 sep + label

std::vector<EquippedRow> EquippedRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed, const ItemColumns& columns) {
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       character.equipped()) {
    if (!IsArcaneSymbol(kv.second.prototype())) {
      slots.push_back(kv.first);
    }
  }
  std::sort(slots.begin(), slots.end(), [](EquipSlot a, EquipSlot b) {
    return SlotOrder(a) < SlotOrder(b);
  });
  std::vector<EquippedRow> rows;
  for (EquipSlot slot : slots) {
    const EquipInstance& item = character.equipped().at(slot);
    // Only the selected row's name slides; the rest sit at their heads.
    std::chrono::steady_clock::duration slide =
        static_cast<int>(rows.size()) == selected
            ? elapsed
            : std::chrono::steady_clock::duration::zero();
    ItemCells cells = EquipUpgradeCells(item.prototype(), item.equip_state());
    cells.name = item.prototype().name();
    cells.slot = FormatWornSlot(slot);
    cells.stats = ItemStatsCell(character.proto().job(), item.stats());
    EquippedRow row;
    row.slot = slot;
    row.inactive = !character.AttackCounts(item.prototype());
    row.text = FormatItemRow(columns, cells, slide);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<EquippedRow> SymbolRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed) {
  std::vector<EquippedRow> rows;
  // The worn map is keyed by slot, and the symbol slots are numbered in the
  // order their areas open -- so walking it is already the order to list them.
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       character.equipped()) {
    const EquipInstance& item = kv.second;
    if (!IsArcaneSymbol(item.prototype())) {
      continue;
    }
    std::chrono::steady_clock::duration slide =
        static_cast<int>(rows.size()) == selected
            ? elapsed
            : std::chrono::steady_clock::duration::zero();
    const Equip& state = item.equip_state();
    int level = SymbolLevel(state);
    std::string name =
        ScrollingWindow(item.prototype().name(), kSymbolNameWidth, slide);
    EquippedRow row;
    row.slot = kv.first;
    // Columns of its own, so the row is written out rather than fitted: a
    // symbol has no slot, no upgrades and no potential to show.
    row.text.text = name + "  " +
                    PadRight(std::to_string(level), kSymbolLevelWidth) + "  " +
                    PadRight(SymbolExpCell(state), kSymbolExpWidth) + "  +" +
                    std::to_string(SymbolArcaneForce(level));
    row.text.span[static_cast<int>(ItemColumn::kName)] = {
        0, static_cast<int>(name.size())};
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace ms
