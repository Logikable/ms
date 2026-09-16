/* The rows of an Equipped list: what is worn, in slot order, already written
 * out as text.
 *
 * Two screens draw the same list -- the main screen's Equipped panel and the
 * Party Inspect screen, which draws a party member's -- so neither builds the
 * rows itself. What a worn item's row says is one question, asked here.
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

// The column header over the symbol list. The Equipped list's own header is
// ItemListHeader, over the columns its panel fitted.
extern const char kSymbolHeader[];

// One worn item as a row.
struct EquippedRow {
  // The row and its cells, cursor column excluded.
  ItemRowText text;
  EquipSlot slot;
  // Whether the item is worn but contributing nothing, which a list draws
  // dimmed rather than hidden.
  bool inactive = false;
  // Whether this preset is wearing the first preset's item rather than one of
  // its own. Drawn dimmed too: the row says what is on the character, and the
  // dim says the preset has nothing of its own to say about that slot.
  bool inherited = false;
};

// The rows for the gear `character` is wearing. Arcane Symbols are left out,
// wearing in slots of their own -- see SymbolRows.
//
// Only the row at `selected` slides a too-long name, `elapsed` being how long
// it has been selected; pass -1 and zero for a list whose names sit at their
// heads. `columns` is what the panel fitted into its width.
std::vector<EquippedRow> EquippedRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed, const ItemColumns& columns,
    StatPreset preset = StatPreset::kFirst);

// The rows for the Arcane Symbols `character` is wearing, in the order their
// areas open. Empty until the first one is put on, which is the whole of what
// the Symbols tab shows before then.
std::vector<EquippedRow> SymbolRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_EQUIPPED_LIST_H_
