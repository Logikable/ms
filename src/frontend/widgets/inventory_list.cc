#include "src/frontend/widgets/inventory_list.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Two leading spaces match the "  " / "> " cursor the rows open with.
constexpr char kColumnHeader[] =
    "  Name                      "  // 2 cursor + 26 name
    "  Equip Slot"                  // 2 sep + 10 slot
    "  Level  Job          "        // 2 sep + 20 info
    "  Scrolls";                    // 2 sep + label
// Aligns "Pass/Left/Restore" under the scroll column.
constexpr int kScrollColumn = 64;

// Byte offsets into an Equip row's label:
// name(26) | slot and padding(14) | level(7) | job(13) | rest
constexpr int kNameEnd = 26;
constexpr int kSlotEnd = 40;
constexpr int kLevelEnd = 47;
constexpr int kJobEnd = 60;

// The row's cells hboxed together, with whichever affixes the caller brought.
ftxui::Element Row(ftxui::Element lead, std::vector<ftxui::Element> cells,
                   ftxui::Element tail) {
  std::vector<ftxui::Element> row;
  if (lead != nullptr) {
    row.push_back(std::move(lead));
  }
  for (ftxui::Element& cell : cells) {
    row.push_back(std::move(cell));
  }
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
    std::chrono::steady_clock::duration elapsed) {
  std::vector<InventoryRowState> rows;
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipTabItem& item = character.inventory()[i];
    const EquipPrototype& proto = item.prototype();
    int level = proto.required_level() > 0 ? proto.required_level() : 1;
    std::string info = "Lv" + PadRight(std::to_string(level), 3) + "  " +
                       FormatJobCategories(proto);
    InventoryRowState row;
    // Only the selected row's name slides; the rest sit at their heads.
    row.label = FormatItemEntry(
        item.name(), proto.equip_slot(), info, proto, item.equip_state(),
        i == selected ? elapsed : std::chrono::steady_clock::duration::zero());
    row.is_trace = character.inventory().equip_instance(i) == nullptr;
    row.level_ok = character.MeetsLevel(proto);
    row.job_ok = character.MeetsJob(proto);
    rows.push_back(std::move(row));
  }
  return rows;
}

ftxui::Element EquipHeader(ftxui::Element lead, ftxui::Element tail) {
  return Row(std::move(lead), {ftxui::text(kColumnHeader)}, std::move(tail));
}

ftxui::Element EquipSubHeader(int lead_width) {
  return ftxui::text(std::string(lead_width + kScrollColumn, ' ') +
                     "Pass/Left/Restore");
}

ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead, ftxui::Element tail) {
  std::string cursor = on_cursor ? "> " : "  ";
  const std::string& label = row.label;
  // A row nothing can be done with: too low for it, the wrong class for it, or
  // a trace, which is a record of an item rather than one. Dimmed whole, the
  // way the skills tab dims a skill that cannot be learned -- one answer for
  // "this row's action is shut", in both lists (colors.h).
  bool blocked = !row.level_ok || !row.job_ok || row.is_trace;
  if (blocked && static_cast<int>(label.size()) >= kJobEnd) {
    // The cell that says WHY stays bright and red while the rest of the row
    // dims. Dimming it too would mute the one thing on the row worth reading.
    ftxui::Element name = ftxui::text(label.substr(0, kNameEnd)) | ftxui::dim;
    ftxui::Element slot =
        ftxui::text(label.substr(kNameEnd, kSlotEnd - kNameEnd)) | ftxui::dim;
    ftxui::Element lv =
        ftxui::text(label.substr(kSlotEnd, kLevelEnd - kSlotEnd));
    lv = row.level_ok ? lv | ftxui::dim : lv | ftxui::color(kRed);
    ftxui::Element job =
        ftxui::text(label.substr(kLevelEnd, kJobEnd - kLevelEnd));
    job = row.job_ok ? job | ftxui::dim : job | ftxui::color(kRed);
    ftxui::Element rest = ftxui::text(label.substr(kJobEnd)) | ftxui::dim;
    // The caret stays bright: it is the cursor, not part of the row.
    return Row(std::move(lead),
               {ftxui::text(cursor), std::move(name), std::move(slot),
                std::move(lv), std::move(job), std::move(rest)},
               std::move(tail));
  }
  return Row(std::move(lead), {ftxui::text(cursor + label)}, std::move(tail));
}

ftxui::Element StackHeader(ftxui::Element lead, ftxui::Element tail) {
  return Row(std::move(lead),
             {ftxui::text("  " + PadRight("Name", kItemNameWidth) +
                          PadRight("Quantity", 10))},
             std::move(tail));
}

ftxui::Element RenderStackRow(const StackableItem& stack, bool on_cursor,
                              std::chrono::steady_clock::duration elapsed,
                              ftxui::Element lead, ftxui::Element tail) {
  std::string cursor = on_cursor ? "> " : "  ";
  std::string text = cursor +
                     ScrollingWindow(stack.name(), kItemNameWidth, elapsed) +
                     PadRight(std::to_string(stack.count()), 10);
  return Row(std::move(lead), {ftxui::text(text)}, std::move(tail));
}

}  // namespace ms
