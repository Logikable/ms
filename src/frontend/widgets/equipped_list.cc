#include "src/frontend/widgets/equipped_list.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"

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

// The order the equipment window lists what is worn, top to bottom: down the
// body, then the accessories, then what is carried rather than worn.
//
// A table rather than the enum's own order, because a slot number is what a
// save names a worn item by and so can never move -- the rings and the second
// pendant are numbered at the bottom of the enum, and would trail the list
// instead of standing with their families.
//
// Only the head of each family is named. The symbols are here so that every
// slot has a place; they are listed on a tab of their own, which sorts itself.
constexpr EquipSlot kSlotOrder[] = {
    EQUIP_SLOT_PRIMARY_WEAPON,
    EQUIP_SLOT_HAT,
    EQUIP_SLOT_TOP,
    EQUIP_SLOT_BOTTOM,
    EQUIP_SLOT_SHOES,
    EQUIP_SLOT_GLOVES,
    EQUIP_SLOT_CAPE,
    EQUIP_SLOT_SHOULDER,
    EQUIP_SLOT_BELT,
    EQUIP_SLOT_FACE_ACCESSORY,
    EQUIP_SLOT_EYE_ACCESSORY,
    EQUIP_SLOT_EARRINGS,
    EQUIP_SLOT_PENDANT,
    EQUIP_SLOT_RING,
    EQUIP_SLOT_EMBLEM,
    EQUIP_SLOT_BADGE,
    EQUIP_SLOT_MEDAL,
    EQUIP_SLOT_POCKET,
    EQUIP_SLOT_PROJECTILE,
    EQUIP_SLOT_SECONDARY,
    EQUIP_SLOT_HEART,
    EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY,
    EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
    EQUIP_SLOT_SYMBOL_LACHELEIN,
    EQUIP_SLOT_SYMBOL_ARCANA,
    EQUIP_SLOT_SYMBOL_MORASS,
    EQUIP_SLOT_SYMBOL_ESFERA,
};

// Every slot but UNSPECIFIED and the four a family gained. A slot added
// without a place in the list would sort to the bottom unnoticed.
static_assert(EquipSlot_ARRAYSIZE ==
                  static_cast<int>(sizeof(kSlotOrder) / sizeof(kSlotOrder[0])) +
                      5,
              "a new slot needs a place in kSlotOrder");

// Where a slot sits in the list: its family's place, then its own place
// within the family, so the four rings stand together in the order they fill.
int SlotOrder(EquipSlot slot) {
  EquipSlot base = BaseSlot(slot);
  int size = static_cast<int>(sizeof(kSlotOrder) / sizeof(kSlotOrder[0]));
  for (int i = 0; i < size; ++i) {
    if (kSlotOrder[i] == base) {
      return i * 10 + SlotIndex(slot);
    }
  }
  return size * 10;
}

}  // namespace

std::string EquippedHeader(int name_width) {
  return "  " + PadRight("Name", name_width) +  // cursor + name
         "  Equip Slot"                         // 2 sep + 10 slot
         "  Stats               "               // 2 sep + 20 info
         "  Scroll"                             // 2 sep + 6 scroll
         "  Star Force";                        // 2 sep + label
}

const char kSymbolHeader[] =
    "  Name                            "  // 2 cursor + 32 name
    "  Lv "                               // 2 sep + 3 level
    "  EXP    "                           // 2 sep + 7 exp
    "  AF";                               // 2 sep + label

std::vector<EquippedRow> EquippedRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed, int name_width) {
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
    EquippedRow row;
    row.slot = slot;
    row.inactive = !character.AttackCounts(item.prototype());
    row.name_bytes = static_cast<int>(
        ItemNameCell(item.prototype().name(), slide, name_width).size());
    row.text =
        FormatItemEntry(item.prototype().name(), FormatWornSlot(slot),
                        RowInfo(character, item.stats()), item.prototype(),
                        item.equip_state(), slide, name_width);
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
