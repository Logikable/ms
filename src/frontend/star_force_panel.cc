#include "src/frontend/star_force_panel.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"

namespace ms {
namespace {

// Formats a rate in hundredths of a percent (10000=100%) as "XX%", "XX.X%",
// or "XX.XX%" depending on how many decimal places are needed.
std::string FormatRate(int hundredths) {
  int whole = hundredths / 100;
  int frac = hundredths % 100;
  if (frac == 0) {
    return std::to_string(whole) + "%";
  }
  if (frac % 10 == 0) {
    return std::to_string(whole) + "." + std::to_string(frac / 10) + "%";
  }
  std::string frac_str = (frac < 10 ? "0" : "") + std::to_string(frac);
  return std::to_string(whole) + "." + frac_str + "%";
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
  rows.push_back(ftxui::text(" Success  " + FormatRate(rate.success) + " "));
  rows.push_back(ftxui::text(" Fail     " + FormatRate(fail) + " "));
  if (rate.destroy > 0) {
    rows.push_back(ftxui::text(" Destroy  " + FormatRate(rate.destroy) + " "));
  }
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(" Press Enter to attempt "));
  rows.push_back(ftxui::text(" Esc to cancel          "));
  return ftxui::window(ftxui::text(" Star Force "),
                       ftxui::vbox(std::move(rows)));
}

}  // namespace ms
