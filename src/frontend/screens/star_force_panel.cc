#include "src/frontend/screens/star_force_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/star_force_cost.h"

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

// One line of a two-column block: the label left-aligned in its column, the
// number right-aligned in its own, and the pair centred together. Both widths
// come from the whole block, so the numbers stand in one column however many
// digits each of them has.
ftxui::Element TwoColumnRow(const std::string& label, int label_width,
                            const std::string& value, int value_width) {
  return CenteredRow(PadRight(label, label_width) + "  " +
                     PadLeft(value, value_width));
}

}  // namespace

void StarForcePanel::SetItem(const EquipInstance* item, int64_t meso) {
  item_ = item;
  meso_ = meso;
}

int64_t StarForcePanel::Cost() const {
  if (item_ == nullptr) {
    return 0;
  }
  return StarForceCost(item_->prototype().required_level(), item_->stars());
}

bool StarForcePanel::Affordable() const {
  return meso_ >= Cost();
}

bool StarForcePanel::OnCancel() const {
  return cancel_selected_ || !Affordable();
}

// One row per stat the next star adds, in two columns: the names down the
// left of theirs, the gains against the right of theirs.
std::vector<ftxui::Element> StatGainRows(const EquipStats& before,
                                         const EquipStats& after) {
  std::vector<std::pair<std::string, std::string>> gains;
  int label_width = 0;
  int value_width = 0;
  for (const DisplayStat& stat : kDisplayStats) {
    int delta = stat.GetFrom(after) - stat.GetFrom(before);
    if (delta <= 0) {
      continue;
    }
    std::string value = "+" + std::to_string(delta);
    label_width = std::max(label_width, static_cast<int>(strlen(stat.label)));
    value_width = std::max(value_width, static_cast<int>(value.size()));
    gains.push_back({stat.label, std::move(value)});
  }
  std::vector<ftxui::Element> rows;
  for (const std::pair<std::string, std::string>& gain : gains) {
    rows.push_back(
        TwoColumnRow(gain.first, label_width, gain.second, value_width));
  }
  return rows;
}

// The three ways the attempt can land, in the same two columns the stats
// above them stand in.
std::vector<ftxui::Element> OddsRows(const StarForceRate& rate) {
  std::string success = FormatRate(rate.success);
  std::string fail = FormatRate(10000 - rate.success - rate.destroy);
  std::string destroy = rate.destroy > 0 ? FormatRate(rate.destroy) : "";
  constexpr int kLabelWidth = 7;  // "Destroy", the longest of the three
  int value_width =
      static_cast<int>(std::max({success.size(), fail.size(), destroy.size()}));
  std::vector<ftxui::Element> rows;
  rows.push_back(TwoColumnRow("Success", kLabelWidth, success, value_width) |
                 ftxui::color(kGreen));
  rows.push_back(TwoColumnRow("Fail", kLabelWidth, fail, value_width) |
                 ftxui::color(kMutedYellow));
  if (rate.destroy > 0) {
    rows.push_back(TwoColumnRow("Destroy", kLabelWidth, destroy, value_width) |
                   ftxui::color(kRed));
  }
  return rows;
}

ftxui::Element StarForcePanel::Render() const {
  if (item_ == nullptr) {
    return ThemedWindow(" Star Force ", EmptyState("no item"));
  }

  int stars = item_->stars();
  std::string name = item_->prototype().name();

  if (stars >= item_->max_stars()) {
    // The name and the star count are one heading, so no rule between them.
    return ThemedWindow(" Star Force ",
                        ftxui::vbox({
                            CenteredRow(name),
                            CenteredRow(std::to_string(stars) + "★ (max)"),
                            ThemedSeparator(),
                            CenteredRow("Maximum stars reached."),
                        }));
  }

  std::vector<ftxui::Element> rows;
  // The name and the star it is going for are one heading, so no rule between
  // them: what the rules separate is the heading, the stats, the odds and the
  // price.
  rows.push_back(CenteredRow(name));
  std::string arrow =
      std::to_string(stars) + "★ → " + std::to_string(stars + 1) + "★";
  rows.push_back(CenteredRow(arrow));
  rows.push_back(ThemedSeparator());
  for (ftxui::Element& row :
       StatGainRows(item_->StarForceStatGains(stars),
                    item_->StarForceStatGains(stars + 1))) {
    rows.push_back(std::move(row));
  }
  rows.push_back(ThemedSeparator());
  for (ftxui::Element& row : OddsRows(EquipInstance::RateAt(stars))) {
    rows.push_back(std::move(row));
  }
  rows.push_back(ThemedSeparator());
  // What the attempt takes, charged whichever of the three ways it lands. Its
  // own section, between the odds and the button: it is the last thing the
  // player reads before pressing. Red when the purse will not cover it, which
  // is the reason the button below is greyed.
  ftxui::Element price = CenteredRow(FormatMeso(Cost()));
  if (!Affordable()) {
    price = std::move(price) | ftxui::color(kRed);
  }
  rows.push_back(std::move(price));
  rows.push_back(ThemedSeparator());
  rows.push_back(CenteredRow(ButtonRow("Enhance", "Cancel",
                                       /*go_focused=*/!OnCancel(),
                                       /*leave_focused=*/OnCancel(),
                                       /*go_enabled=*/Affordable())));
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
    // The prompt's own Cancel closes the prompt and no more: the player is
    // backing out of the question, not out of the screen.
    if (confirm_.OnEvent(event) == ConfirmChoice::kConfirmed) {
      confirmed_ = true;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    // Recorded whether or not it lands: OnCancel holds the cursor on [Cancel]
    // while the price is out of reach, and lets it go the moment it is not.
    cancel_selected_ = false;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    cancel_selected_ = true;
    return true;
  }
  if (IsForward(event)) {
    if (OnCancel()) {
      cancelled_ = true;
    } else {
      confirm_.Open();
    }
    return true;
  }
  return false;  // Esc and other events pass through to caller
}

bool StarForcePanel::TakeConfirmed() {
  bool v = confirmed_;
  confirmed_ = false;
  return v;
}

bool StarForcePanel::TakeCancelled() {
  bool v = cancelled_;
  cancelled_ = false;
  return v;
}

void StarForcePanel::ResetConfirm() {
  confirm_.Close();
  confirmed_ = false;
  cancelled_ = false;
  cancel_selected_ = false;
}

ftxui::Element StarForcePanel::RenderResult(const StarForceResult& r) const {
  // The whole window takes the outcome's colour -- gold for a star gained, red
  // for an item lost -- so the player knows which of the three happened before
  // reading anything. A plain failure keeps the steel-blue frame: it is the
  // outcome where nothing changed.
  std::string outcome_text;
  ftxui::Color outcome_color;
  ftxui::Color accent = kTheme;
  if (r.outcome == kStarForceSuccess) {
    outcome_text = " SUCCESS ";
    outcome_color = kYellow;
    accent = kYellow;
  } else if (r.outcome == kStarForceFail) {
    outcome_text = " FAILED ";
    outcome_color = kMutedYellow;
  } else if (r.outcome == kStarForceNoMeso) {
    // The screen no longer lets a player press for an attempt they cannot pay
    // for -- the button is greyed. This is the backstop for a purse that
    // emptied some other way between the render and the keypress.
    outcome_text = " NOT ENOUGH MESO ";
    outcome_color = kRed;
  } else {
    outcome_text = " DESTROYED ";
    outcome_color = kRed;
    accent = kRed;
  }
  std::string stars_text;
  if (r.outcome == kStarForceSuccess) {
    stars_text = std::to_string(r.stars_before) + "★ → " +
                 std::to_string(r.stars_after) + "★";
  } else if (r.outcome == kStarForceFail || r.outcome == kStarForceNoMeso) {
    stars_text = std::to_string(r.stars_before) + "★";
  } else {
    stars_text = "lost at " + std::to_string(r.stars_before) + "★";
  }
  return ResultWindow(
      " Result ", r.equip_name,
      {
          CenteredRow(outcome_text) | ftxui::color(outcome_color),
          CenteredRow(stars_text),
      },
      accent);
}

}  // namespace ms
