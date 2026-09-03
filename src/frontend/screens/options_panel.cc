#include "src/frontend/screens/options_panel.h"

#include <string>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

// The box and what stands in it when the switch is thrown.
constexpr char kChecked[] = "[✓]";
constexpr char kUnchecked[] = "[ ]";

std::string OptionName(Option option) {
  switch (option) {
    case Option::kPanelTitleBlink:
      return "Panel Title Blink";
  }
  return "";
}

Option OptionAt(int row) {
  return static_cast<Option>(row);
}

}  // namespace

OptionsPanel::OptionsPanel(AccountInstance& account) : account_(account) {
}

void OptionsPanel::Reset() {
  row_ = 0;
}

void OptionsPanel::MoveRow(int delta) {
  // The Close button is one more stop after the options.
  row_ = StepCursor(row_, delta, kOptionCount + 1);
}

Option OptionsPanel::selected_option() const {
  return OptionAt(row_);
}

bool OptionsPanel::IsOn(Option option) const {
  switch (option) {
    case Option::kPanelTitleBlink:
      return account_.panel_title_blink();
  }
  return false;
}

void OptionsPanel::Toggle() {
  if (on_close()) {
    return;
  }
  switch (selected_option()) {
    case Option::kPanelTitleBlink:
      account_.SetPanelTitleBlink(!account_.panel_title_blink());
      return;
  }
}

ftxui::Element OptionsPanel::RenderRow(Option option, int row) const {
  bool selected = row == row_ && !on_close();
  ftxui::Element name = ftxui::text(" " + OptionName(option)) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kNameWidth);
  ftxui::Element state = ftxui::text(IsOn(option) ? kChecked : kUnchecked) |
                         ftxui::center |
                         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kStateWidth);
  if (selected) {
    // The whole row inverts: the box is what the player is aiming at, but a
    // lit box alone would leave the name it belongs to unmarked.
    return ftxui::hbox({std::move(name), std::move(state)}) | ftxui::inverted;
  }
  return ftxui::hbox({std::move(name), std::move(state)});
}

ftxui::Element OptionsPanel::Render() const {
  ftxui::Elements rows;

  rows.push_back(ftxui::hbox({
                     ftxui::text(" Option") |
                         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kNameWidth),
                     ftxui::text("State") | ftxui::center |
                         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kStateWidth),
                 }) |
                 ftxui::color(kTheme));
  rows.push_back(ThemedSeparator());
  for (int i = 0; i < kOptionCount; ++i) {
    rows.push_back(RenderRow(OptionAt(i), i));
  }
  // The room the options still to come will take.
  for (int i = kOptionCount; i < kListRows; ++i) {
    rows.push_back(ftxui::text(""));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(CenteredRow(ActionButton("Close", on_close())));
  return ThemedWindow(" Options ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
