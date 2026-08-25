#include "src/frontend/widgets/equipped_list.h"

#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"

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

// The stat column of one row: the attack this job swings with, then the stat
// its damage is built on.
std::string RowInfo(const CharacterInstance& character,
                    const EquipStats& stats) {
  // One column for the stat this job's damage is built on -- the same question
  // the character panel and the AP reset ask, so asked in the same place
  // rather than switched over jobs here.
  StatField primary = PrimaryStatField(character.proto().job());
  const DisplayStat* main = DisplayStatFor(primary);
  std::string main_str;
  if (main != nullptr && main->GetFrom(stats) > 0) {
    main_str = "+" + std::to_string(main->GetFrom(stats)) + " " + main->label;
  }
  // Room for one attack figure, so show the one this job swings with. A wand
  // carries both, and a magician's weapon attack never reaches the damage
  // chain.
  //
  // Asked of the stat this job builds on rather than listed job by job: every
  // INT job is a magician, so a new magician branch reads right the day it is
  // added instead of the day someone remembers this list.
  std::string atk_str;
  bool magic = primary == STAT_FIELD_INT;
  if (!magic && stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  } else if (stats.magic_attack() > 0) {
    atk_str = "+" + std::to_string(stats.magic_attack()) + " MATT";
  } else if (stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  }
  // Attack leads: it is the number that decides a weapon, and the main stat
  // qualifies it.
  return PadRight(atk_str, 10) + PadRight(main_str, 10);
}

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

const char kEquippedHeader[] =
    "  Name                      "  // 2 cursor + 26 name
    "  Equip Slot"                  // 2 sep + 10 slot
    "  Stats               "        // 2 sep + 20 info (10 atk + 10 main)
    "  Scroll"                      // 2 sep + 6 scroll
    "  Star Force";                 // 2 sep + label

const char kSymbolHeader[] =
    "  Name                            "  // 2 cursor + 32 name
    "  Lv "                               // 2 sep + 3 level
    "  EXP    "                           // 2 sep + 7 exp
    "  AF";                               // 2 sep + label

std::vector<EquippedRow> EquippedRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed) {
  std::vector<EquippedRow> rows;
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       character.equipped()) {
    const EquipInstance& item = kv.second;
    if (IsArcaneSymbol(item.prototype())) {
      continue;
    }
    // Only the selected row's name slides; the rest sit at their heads.
    std::chrono::steady_clock::duration slide =
        static_cast<int>(rows.size()) == selected
            ? elapsed
            : std::chrono::steady_clock::duration::zero();
    EquippedRow row;
    row.slot = kv.first;
    row.inactive = !character.AttackCounts(item.prototype());
    row.name_bytes =
        static_cast<int>(ItemNameCell(item.prototype().name(), slide).size());
    row.text = FormatItemEntry(item.prototype().name(), kv.first,
                               RowInfo(character, item.stats()),
                               item.prototype(), item.equip_state(), slide);
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
    row.name_bytes = static_cast<int>(name.size());
    row.text = name + "  " +
               PadRight(std::to_string(level), kSymbolLevelWidth) + "  " +
               PadRight(SymbolExpCell(state), kSymbolExpWidth) + "  +" +
               std::to_string(SymbolArcaneForce(level));
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace ms
