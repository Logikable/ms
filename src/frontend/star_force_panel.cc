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
  int fail = 10000 - rate.success - rate.destroy;

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" " + item_->prototype().name() + " "));
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(" " + std::to_string(stars) + "★ → " +
                             std::to_string(stars + 1) + "★ "));
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(" Success  " + FormatPpt(rate.success) + " "));
  rows.push_back(ftxui::text(" Fail     " + FormatPpt(fail) + " "));
  if (rate.destroy > 0) {
    rows.push_back(ftxui::text(" Destroy  " + FormatPpt(rate.destroy) + " "));
  }
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(" Press Enter to attempt "));
  rows.push_back(ftxui::text(" Esc to cancel          "));
  return ftxui::window(ftxui::text(" Star Force "),
                       ftxui::vbox(std::move(rows)));
}

}  // namespace ms
