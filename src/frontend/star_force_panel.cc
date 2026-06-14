#include "src/frontend/star_force_panel.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/panel_util.h"

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

// Right-pads `s` with spaces to `width` characters.
std::string PadTo(const std::string& s, int width) {
  return s + std::string(width - static_cast<int>(s.size()), ' ');
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
  std::string name = item_->prototype().name();

  if (stars >= item_->max_stars()) {
    return ftxui::window(
        ftxui::text(" Star Force "),
        ftxui::vbox({
            ftxui::text(name) | ftxui::hcenter,
            ftxui::separator(),
            ftxui::text(std::to_string(stars) + "★ (max)") | ftxui::hcenter,
            ftxui::separator(),
            ftxui::text("Maximum stars reached.") | ftxui::hcenter,
            ftxui::text(" Esc to close           "),
        }));
  }

  StarForceRate rate = EquipInstance::RateAt(stars);
  int fail = 10000 - rate.success - rate.destroy;

  // Pad all rate strings to the same width so hcenter aligns both columns.
  std::string success_str = FormatRate(rate.success);
  std::string fail_str = FormatRate(fail);
  std::string destroy_str = rate.destroy > 0 ? FormatRate(rate.destroy) : "";
  int rate_w = static_cast<int>(
      std::max({success_str.size(), fail_str.size(), destroy_str.size()}));

  EquipStats before = item_->StarForceStatGains(stars);
  EquipStats after = item_->StarForceStatGains(stars + 1);

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(name) | ftxui::hcenter);
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(std::to_string(stars) + "★ → " +
                             std::to_string(stars + 1) + "★") |
                 ftxui::hcenter);
  rows.push_back(ftxui::separator());
  int label_w = 0;
  for (const DisplayStat& stat : kDisplayStats) {
    if (stat.GetFrom(after) - stat.GetFrom(before) > 0) {
      label_w = std::max(label_w, static_cast<int>(strlen(stat.label)));
    }
  }
  for (const DisplayStat& stat : kDisplayStats) {
    int delta = stat.GetFrom(after) - stat.GetFrom(before);
    if (delta > 0) {
      rows.push_back(ftxui::text(PadTo(stat.label, label_w) + "  +" +
                                 std::to_string(delta)) |
                     ftxui::hcenter);
    }
  }
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text("Success  " + PadTo(success_str, rate_w)) |
                 ftxui::hcenter);
  rows.push_back(ftxui::text("Fail     " + PadTo(fail_str, rate_w)) |
                 ftxui::hcenter);
  if (rate.destroy > 0) {
    rows.push_back(ftxui::text("Destroy  " + PadTo(destroy_str, rate_w)) |
                   ftxui::hcenter);
  }
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(" Press Enter to attempt "));
  rows.push_back(ftxui::text(" Esc to cancel          "));
  return ftxui::window(ftxui::text(" Star Force "),
                       ftxui::vbox(std::move(rows)));
}

ftxui::Element StarForcePanel::RenderResult(const StarForceResult& r) const {
  std::string outcome_text;
  if (r.outcome == kStarForceSuccess) {
    outcome_text = " SUCCESS ";
  } else if (r.outcome == kStarForceFail) {
    outcome_text = " FAILED ";
  } else {
    outcome_text = " DESTROYED ";
  }
  std::string stars_text;
  if (r.outcome == kStarForceSuccess) {
    stars_text = std::to_string(r.stars_before) + "★ → " +
                 std::to_string(r.stars_after) + "★";
  } else if (r.outcome == kStarForceFail) {
    stars_text = std::to_string(r.stars_before) + "★";
  } else {
    stars_text = "lost at " + std::to_string(r.stars_before) + "★";
  }
  return ftxui::window(
      ftxui::text(" Result "),
      ftxui::vbox({
          ftxui::text(" " + r.equip_name + " ") | ftxui::hcenter,
          ftxui::separator(),
          ftxui::text(outcome_text) | ftxui::hcenter,
          ftxui::text(stars_text) | ftxui::hcenter,
          ftxui::text(""),
          ftxui::text(" Press Enter to continue "),
      }));
}

}  // namespace ms
