#include "src/frontend/widgets/inventory_list.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The row's cells hboxed together, with whichever affixes the caller brought.
ftxui::Element Row(ftxui::Element lead, std::vector<ftxui::Element> cells,
                   ftxui::Element tail, int body_width) {
  ftxui::Element body = ftxui::hbox(std::move(cells));
  if (body_width > 0) {
    body =
        std::move(body) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, body_width);
  }
  std::vector<ftxui::Element> row;
  if (lead != nullptr) {
    row.push_back(std::move(lead));
  }
  row.push_back(std::move(body));
  if (tail != nullptr) {
    row.push_back(std::move(tail));
  }
  return ftxui::hbox(std::move(row));
}

}  // namespace

const char* const kInventoryTabLabels[kNumInventoryTabs] = {"Equip", "Use",
                                                            "Etc", "Shop"};

ItemCategory TabCategory(int tab) {
  if (tab == kUseTab) {
    return ITEM_CATEGORY_USE;
  }
  if (tab == kEtcTab) {
    return ITEM_CATEGORY_ETC;
  }
  return ITEM_CATEGORY_UNSPECIFIED;
}

std::vector<InventoryRowState> BuildEquipRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed, const ItemColumns& columns) {
  std::vector<InventoryRowState> rows;
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipTabItem& item = character.inventory()[i];
    const EquipPrototype& proto = item.prototype();
    int level = proto.required_level() > 0 ? proto.required_level() : 1;
    ItemCells cells = EquipUpgradeCells(proto, item.equip_state());
    cells.name = item.name();
    cells.slot = FormatSlot(proto.equip_slot());
    cells.level = "Lv" + std::to_string(level);
    cells.job = FormatJobCategories(proto);
    cells.stats = ItemStatsCell(character.proto().job(), item.stats());
    InventoryRowState row;
    // Only the selected row's name slides; the rest sit at their heads.
    std::chrono::steady_clock::duration slide =
        i == selected ? elapsed : std::chrono::steady_clock::duration::zero();
    row.label = FormatItemRow(columns, cells, slide);
    row.is_trace = character.inventory().equip_instance(i) == nullptr;
    row.level_ok = character.MeetsLevel(proto);
    row.job_ok = character.MeetsJob(proto);
    rows.push_back(std::move(row));
  }
  return rows;
}

ftxui::Element EquipHeader(const ItemColumns& columns, ftxui::Element lead,
                           ftxui::Element tail, int body_width) {
  return Row(std::move(lead), {ftxui::text(ItemListHeader(columns))},
             std::move(tail), body_width);
}

ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead, ftxui::Element tail,
                              int body_width) {
  std::string cursor = on_cursor ? "> " : "  ";
  const ItemRowText& label = row.label;
  // A row nothing can be done with: too low for it, the wrong class for it, or
  // a trace, which is a record of an item rather than one. Dimmed whole, the
  // way the skills tab dims a skill that cannot be learned -- one answer for
  // "this row's action is shut", in both lists (colors.h).
  bool blocked = !row.level_ok || !row.job_ok || row.is_trace;
  if (!blocked) {
    return Row(std::move(lead), {ftxui::text(cursor + label.text)},
               std::move(tail), body_width);
  }
  // The caret stays bright: it is the cursor, not part of the row.
  std::vector<ftxui::Element> cells = {ftxui::text(cursor)};
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    CellSpan span = label.Span(column);
    if (span.bytes == 0) {
      continue;
    }
    ftxui::Element cell =
        ftxui::text(label.text.substr(span.offset, span.bytes));
    // The cell that says WHY stays bright and red while the rest of the row
    // dims. Dimming it too would mute the one thing on the row worth reading.
    bool why = (column == ItemColumn::kLevel && !row.level_ok) ||
               (column == ItemColumn::kJob && !row.job_ok);
    cells.push_back(why ? std::move(cell) | ftxui::color(kRed)
                        : std::move(cell) | ftxui::dim);
  }
  return Row(std::move(lead), std::move(cells), std::move(tail), body_width);
}

ftxui::Element StackHeader(ftxui::Element lead, ftxui::Element tail,
                           int body_width) {
  return Row(std::move(lead),
             {ftxui::text("  " + PadRight("Name", kItemNameWidth) +
                          PadRight("Quantity", 10))},
             std::move(tail), body_width);
}

ftxui::Element RenderStackRow(const StackableItem& stack, bool on_cursor,
                              std::chrono::steady_clock::duration elapsed,
                              ftxui::Element lead, ftxui::Element tail,
                              int body_width) {
  std::string cursor = on_cursor ? "> " : "  ";
  std::string text = cursor +
                     ScrollingWindow(stack.name(), kItemNameWidth, elapsed) +
                     PadRight(std::to_string(stack.count()), 10);
  return Row(std::move(lead), {ftxui::text(text)}, std::move(tail), body_width);
}

}  // namespace ms
