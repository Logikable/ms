#include "src/frontend/widgets/inventory_list.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Two leading spaces match the "  " / "> " cursor the rows open with.
std::string ColumnHeader(int name_width) {
  return "  " + PadRight("Name", name_width) +  // cursor + name
         "  Equip Slot"                         // 2 sep + 10 slot
         "  Level  Job          "               // 2 sep + 20 info
         "  Scroll"                             // 2 sep + 6 scroll
         "  Star Force";                        // 2 sep + label
}

// Byte spans of an Equip row's cells, measured from the end of its name:
// the slot cell with its separators, then the level and job halves of the
// info column. The name's own length is on the row -- see name_bytes.
constexpr int kSlotSpan = 14;
constexpr int kLevelSpan = 7;
constexpr int kJobSpan = 13;

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
    std::chrono::steady_clock::duration elapsed, int name_width) {
  std::vector<InventoryRowState> rows;
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipTabItem& item = character.inventory()[i];
    const EquipPrototype& proto = item.prototype();
    int level = proto.required_level() > 0 ? proto.required_level() : 1;
    std::string info = "Lv" + PadRight(std::to_string(level), 3) + "  " +
                       FormatJobCategories(proto);
    InventoryRowState row;
    // Only the selected row's name slides; the rest sit at their heads.
    std::chrono::steady_clock::duration slide =
        i == selected ? elapsed : std::chrono::steady_clock::duration::zero();
    row.name_bytes =
        static_cast<int>(ItemNameCell(item.name(), slide, name_width).size());
    row.label =
        FormatItemEntry(item.name(), FormatSlot(proto.equip_slot()), info,
                        proto, item.equip_state(), slide, name_width);
    row.is_trace = character.inventory().equip_instance(i) == nullptr;
    row.level_ok = character.MeetsLevel(proto);
    row.job_ok = character.MeetsJob(proto);
    rows.push_back(std::move(row));
  }
  return rows;
}

ftxui::Element EquipHeader(ftxui::Element lead, ftxui::Element tail,
                           int body_width, int name_width) {
  return Row(std::move(lead), {ftxui::text(ColumnHeader(name_width))},
             std::move(tail), body_width);
}

ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead, ftxui::Element tail,
                              int body_width) {
  std::string cursor = on_cursor ? "> " : "  ";
  const std::string& label = row.label;
  // A row nothing can be done with: too low for it, the wrong class for it, or
  // a trace, which is a record of an item rather than one. Dimmed whole, the
  // way the skills tab dims a skill that cannot be learned -- one answer for
  // "this row's action is shut", in both lists (colors.h).
  bool blocked = !row.level_ok || !row.job_ok || row.is_trace;
  int slot_end = row.name_bytes + kSlotSpan;
  int level_end = slot_end + kLevelSpan;
  int job_end = level_end + kJobSpan;
  if (blocked && static_cast<int>(label.size()) >= job_end) {
    // The cell that says WHY stays bright and red while the rest of the row
    // dims. Dimming it too would mute the one thing on the row worth reading.
    ftxui::Element name =
        ftxui::text(label.substr(0, row.name_bytes)) | ftxui::dim;
    ftxui::Element slot =
        ftxui::text(label.substr(row.name_bytes, kSlotSpan)) | ftxui::dim;
    ftxui::Element lv = ftxui::text(label.substr(slot_end, kLevelSpan));
    lv = row.level_ok ? lv | ftxui::dim : lv | ftxui::color(kRed);
    ftxui::Element job = ftxui::text(label.substr(level_end, kJobSpan));
    job = row.job_ok ? job | ftxui::dim : job | ftxui::color(kRed);
    ftxui::Element rest = ftxui::text(label.substr(job_end)) | ftxui::dim;
    // The caret stays bright: it is the cursor, not part of the row.
    return Row(std::move(lead),
               {ftxui::text(cursor), std::move(name), std::move(slot),
                std::move(lv), std::move(job), std::move(rest)},
               std::move(tail), body_width);
  }
  return Row(std::move(lead), {ftxui::text(cursor + label)}, std::move(tail),
             body_width);
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
