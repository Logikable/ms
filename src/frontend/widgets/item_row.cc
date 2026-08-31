#include "src/frontend/widgets/item_row.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// The columns after the name, in the order a row writes them.
constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;
constexpr int kScrollWidth = 6;

}  // namespace

int ItemNameWidthFor(int width) {
  return std::clamp(width - kItemListGutter - kItemListFixedWidth,
                    kItemNameWidth, kItemNameMax);
}

std::string ItemNameCell(const std::string& name,
                         std::chrono::steady_clock::duration elapsed,
                         int name_width) {
  return ScrollingWindow(name, name_width, elapsed);
}

std::string FormatItemEntry(const std::string& name,
                            const std::string& slot_label,
                            const std::string& info, int scroll_pass,
                            int scroll_slots, int stars,
                            std::chrono::steady_clock::duration elapsed,
                            int name_width) {
  // The slot count rides along so a row says how far the item can still go,
  // not only how far it has come.
  std::string scrolls = scroll_pass < 0
                            ? "-"
                            : "+" + std::to_string(scroll_pass) + "/" +
                                  std::to_string(scroll_slots);
  std::string star_force = stars < 0 ? "-" : std::to_string(stars) + "\u2605";
  return ItemNameCell(name, elapsed, name_width) + "  " +
         PadRight(slot_label, kSlotWidth) + "  " + PadRight(info, kInfoWidth) +
         "  " + PadRight(scrolls, kScrollWidth) + "  " + star_force;
}

std::string FormatItemEntry(const std::string& name,
                            const std::string& slot_label,
                            const std::string& info,
                            const EquipPrototype& proto, const Equip& state,
                            std::chrono::steady_clock::duration elapsed,
                            int name_width) {
  // An upgrade the item refuses outright reads "-": a zero there would look
  // like a ledger standing ready to be spent.
  int slots = TotalUpgradeSlots(proto, state);
  int pass = slots > 0 ? state.scroll_successes() : -1;
  int stars = Supports(proto, UPGRADE_STAR_FORCE) ? state.stars() : -1;
  return FormatItemEntry(name, slot_label, info, pass, slots, stars, elapsed,
                         name_width);
}

}  // namespace ms
