#include "src/frontend/screens/star_force_panel.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"

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
    return ThemedWindow(" Star Force ", EmptyState("no item"));
  }

  int stars = item_->stars();
  std::string name = item_->prototype().name();

  if (stars >= item_->max_stars()) {
    return ThemedWindow(" Star Force ",
                        ftxui::vbox({
                            CenteredRow(name),
                            ThemedSeparator(),
                            CenteredRow(std::to_string(stars) + "★ (max)"),
                            ThemedSeparator(),
                            CenteredRow("Maximum stars reached."),
                        }));
  }

  StarForceRate rate = EquipInstance::RateAt(stars);
  int fail = 10000 - rate.success - rate.destroy;

  // Pad all rate strings to the same width so centring aligns both columns.
  std::string success_str = FormatRate(rate.success);
  std::string fail_str = FormatRate(fail);
  std::string destroy_str = rate.destroy > 0 ? FormatRate(rate.destroy) : "";
  int rate_w = static_cast<int>(
      std::max({success_str.size(), fail_str.size(), destroy_str.size()}));

  EquipStats before = item_->StarForceStatGains(stars);
  EquipStats after = item_->StarForceStatGains(stars + 1);

  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(name));
  rows.push_back(ThemedSeparator());
  std::string arrow =
      std::to_string(stars) + "★ → " + std::to_string(stars + 1) + "★";
  rows.push_back(CenteredRow(arrow));
  rows.push_back(ThemedSeparator());
  int label_w = 0;
  for (const DisplayStat& stat : kDisplayStats) {
    if (stat.GetFrom(after) - stat.GetFrom(before) > 0) {
      label_w = std::max(label_w, static_cast<int>(strlen(stat.label)));
    }
  }
  for (const DisplayStat& stat : kDisplayStats) {
    int delta = stat.GetFrom(after) - stat.GetFrom(before);
    if (delta > 0) {
      std::string line =
          PadTo(stat.label, label_w) + "  +" + std::to_string(delta);
      rows.push_back(CenteredRow(line));
    }
  }
  rows.push_back(ThemedSeparator());
  auto RateRow = [&rows, &rate_w](const std::string& label,
                                  const std::string& val, ftxui::Color c) {
    rows.push_back(CenteredRow(label + PadTo(val, rate_w)) | ftxui::color(c));
  };
  RateRow("Success  ", success_str, kGreen);
  RateRow("Fail     ", fail_str, kMutedYellow);
  if (rate.destroy > 0) {
    RateRow("Destroy  ", destroy_str, kRed);
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(CenteredRow(ActionButton("Enhance", /*focused=*/true)));
  // Constrain inner width to at least the confirm prompt's, so the panel
  // never widens when the prompt appears below.
  ftxui::Element content =
      ftxui::vbox(std::move(rows)) |
      ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, kConfirmButtonsWidth);
  ftxui::Element main = ThemedWindow(" Star Force ", std::move(content));
  // Always allocate the same height below so ftxui::center never shifts the
  // panel when the prompt appears.
  ftxui::Element below =
      confirm_.open()
          ? confirm_.RenderWindow()
          : (ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL,
                                           ConfirmPrompt::kWindowHeight));
  return ftxui::vbox({std::move(main), std::move(below)});
}

bool StarForcePanel::OnEvent(ftxui::Event event) {
  if (confirm_.open()) {
    if (confirm_.OnEvent(event) == ConfirmChoice::kConfirmed) {
      confirmed_ = true;
    }
    return true;
  }
  if (IsForward(event)) {
    confirm_.Open();
    return true;
  }
  return false;  // Esc and other events pass through to caller
}

bool StarForcePanel::TakeConfirmed() {
  bool v = confirmed_;
  confirmed_ = false;
  return v;
}

void StarForcePanel::ResetConfirm() {
  confirm_.Close();
  confirmed_ = false;
}

ftxui::Element StarForcePanel::RenderResult(const StarForceResult& r) const {
  std::string outcome_text;
  ftxui::Color outcome_color;
  if (r.outcome == kStarForceSuccess) {
    outcome_text = " SUCCESS ";
    outcome_color = kGreen;
  } else if (r.outcome == kStarForceFail) {
    outcome_text = " FAILED ";
    outcome_color = kMutedYellow;
  } else {
    outcome_text = " DESTROYED ";
    outcome_color = kRed;
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
  return ResultWindow(
      " Result ", r.equip_name,
      {
          CenteredRow(outcome_text) | ftxui::color(outcome_color),
          CenteredRow(stars_text),
      });
}

}  // namespace ms
