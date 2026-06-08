#include "src/frontend/star_force_panel.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"

namespace ms {
namespace {

// Formats a parts-per-thousand value as "XX.X%" when fractional, "XX%" when
// whole.
std::string FormatPpt(int ppt) {
  if (ppt % 10 == 0) {
    return std::to_string(ppt / 10) + "%";
  }
  return std::to_string(ppt / 10) + "." + std::to_string(ppt % 10) + "%";
}

}  // namespace

void StarForcePanel::SetItem(const EquipInstance* item) {
  item_ = item;
}

ftxui::Element StarForcePanel::Render() const {
  if (item_ == nullptr) {
    return ftxui::window(ftxui::text(" Star Force "),
                         ftxui::text(" (no item) "));
  }

  int stars = item_->stars();
  StarForceRate rate = EquipInstance::RateAt(stars);
  int fail_ppt = 1000 - rate.success_ppt - rate.destroy_ppt;

  return ftxui::window(
      ftxui::text(" Star Force "),
      ftxui::vbox({
          ftxui::text(" " + item_->prototype().name() + " "),
          ftxui::separator(),
          ftxui::text(" " + std::to_string(stars) + "★ → " +
                      std::to_string(stars + 1) + "★ "),
          ftxui::separator(),
          ftxui::text(" Success  " + FormatPpt(rate.success_ppt) + " "),
          ftxui::text(" Fail     " + FormatPpt(fail_ppt) + " "),
          ftxui::text(" Destroy  " + FormatPpt(rate.destroy_ppt) + " "),
          ftxui::separator(),
          ftxui::text(" Press Enter to attempt "),
          ftxui::text(" Esc to cancel          "),
      }));
}

}  // namespace ms
