/* One row of an item list, as text.
 *
 * The equipped panel and the bag draw the same row -- a name, the slot, an
 * info cell, and how far the item has been upgraded -- so they measure and
 * write it here rather than each laying out its own columns.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_
#define MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_

#include <chrono>
#include <string>

#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// How much room an item name gets in a list on the narrowest panel that draws
// one. Names run past it -- "Fafnir Mistilteinn Trace" does -- so a name is
// cut to this and slides under it while its row is selected; see
// ScrollingWindow.
constexpr int kItemNameWidth = 26;

// And what the column grows to on a wide one: the longest name the game
// ships, a trace's " Trace" included. Past this the columns after the name
// would be pushed away from it for no name's sake.
constexpr int kItemNameMax = 38;

// The columns an item row spends on everything but the name: its cursor, and
// the slot, info, scroll and star cells after it, separators included.
constexpr int kItemListFixedWidth = 56;

// The blank column a list keeps between its last cell and the panel's border.
// Only on the right: the column inside the left border is the cursor's, which
// is clearance of its own.
constexpr int kItemListGutter = 1;

// The name column an item list gets inside `width` columns -- whatever the
// cells after it and the gutter leave, held between the two numbers above.
int ItemNameWidthFor(int width);

// The name cell of a list row: `name` cut to `name_width` columns, sliding
// under them while the row is selected. FormatItemEntry opens with exactly
// this, so a caller that wants to colour the name on its own can split the
// rendered row on this cell's length -- the name may hold multibyte
// characters, and its column width says nothing about how many bytes it is.
std::string ItemNameCell(const std::string& name,
                         std::chrono::steady_clock::duration elapsed =
                             std::chrono::steady_clock::duration::zero(),
                         int name_width = kItemNameWidth);

// Formats a single item list entry: name (`name_width` cols), `slot_label`
// (10 cols), info (padded to 20 cols), scrolls as "+pass/slots", and star
// force level. The label is passed rather than the slot because a bag row and
// a worn row name the same slot differently -- FormatSlot and FormatWornSlot.
// Pass -1 for either upgrade to render "-" (use for an item that refuses
// it).
//
// `elapsed` is how long this row has been the selected one, which is what
// slides a name too long for the column. Zero -- the default, and what every
// unselected row passes -- shows the head of the name and holds it there.
std::string FormatItemEntry(const std::string& name,
                            const std::string& slot_label,
                            const std::string& info, int scroll_pass,
                            int scroll_slots, int stars,
                            std::chrono::steady_clock::duration elapsed =
                                std::chrono::steady_clock::duration::zero(),
                            int name_width = kItemNameWidth);

// The same row, with both upgrades read off the item itself. Every list that
// draws equipment uses this one, so no two of them can disagree about what an
// item that takes neither shows.
std::string FormatItemEntry(const std::string& name,
                            const std::string& slot_label,
                            const std::string& info,
                            const EquipPrototype& proto, const Equip& state,
                            std::chrono::steady_clock::duration elapsed =
                                std::chrono::steady_clock::duration::zero(),
                            int name_width = kItemNameWidth);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_ROW_H_
