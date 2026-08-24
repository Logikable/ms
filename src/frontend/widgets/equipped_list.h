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
#include "src/protos/equip.pb.h"

namespace ms {

// The column header over the list. The two leading spaces are the cursor
// column the rows carry.
extern const char kEquippedHeader[];

// One worn item as a row.
struct EquippedRow {
  // The whole row, cursor column excluded.
  std::string text;
  EquipSlot slot;
  // The byte length of the name cell, for a caller colouring the name apart
  // from the columns after it. A name may hold multibyte characters, so its
  // column width is no guide to its length.
  int name_bytes = 0;
  // Whether the item is worn but contributing nothing, which a list draws
  // dimmed rather than hidden.
  bool inactive = false;
};

// The rows for what `character` is wearing. Only the row at `selected` slides
// a name too long for its column, and `elapsed` is how long it has been the
// selected one; pass -1 and zero for a list whose names all sit at their
// heads.
std::vector<EquippedRow> EquippedRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_EQUIPPED_LIST_H_
