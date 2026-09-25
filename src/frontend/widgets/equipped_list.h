/* The rows of an Equipped list: what is worn, in slot order, already formatted
 * as text.
 *
 * Two screens draw this list: the main screen's Equipped panel and the Party
 * Inspect screen, which shows a party member's. Neither builds the rows itself,
 * so what a worn item's row shows is decided in one place.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_EQUIPPED_LIST_H_
#define MS_SRC_FRONTEND_WIDGETS_EQUIPPED_LIST_H_

#include <chrono>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/item_row.h"
#include "src/protos/equip.pb.h"

namespace ms {

// The column header above the symbol list. The Equipped list's own header is
// ItemListHeader, over the columns its panel fitted.
extern const char kSymbolHeader[];

// One worn item as a row.
struct EquippedRow {
  // The row and its cells, not including the cursor column.
  ItemRowText text;
  EquipSlot slot;
  // Whether the item is worn but has no effect, which a list shows dimmed
  // rather than hidden.
  bool inactive = false;
  // Whether this preset is wearing the first preset's item rather than its own.
  // Also dimmed: the row shows what the character has on, and the dimming says
  // this preset has no item of its own in that slot.
  bool inherited = false;
};

// The rows for the gear `character` is wearing. Arcane Symbols are left out,
// since they have their own slots; see SymbolRows.
//
// Only the row at `selected` scrolls a name that is too long, with `elapsed` as
// how long it has been selected; pass -1 and zero for a list with no scrolling.
// `columns` is what the panel fitted into its width.
std::vector<EquippedRow> EquippedRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed, const ItemColumns& columns,
    StatPreset preset = StatPreset::kFirst);

// The rows for the Arcane Symbols `character` is wearing, in the order their
// areas unlock. Empty until the first is equipped, which is all the Symbols tab
// shows before then.
std::vector<EquippedRow> SymbolRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_EQUIPPED_LIST_H_
