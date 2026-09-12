#include "src/frontend/screens/options_panel.h"

#include <algorithm>
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
    case Option::kMapBgmVolume:
      return "Map BGM Volume";
    case Option::kBossBgmVolume:
      return "Boss BGM Volume";
  }
  return "";
}

Option OptionAt(int row) {
  return static_cast<Option>(row);
}

// `text` pushed right into `width` columns, with a gutter after it.
std::string RightAligned(const std::string& text, int width) {
  std::string padded(std::max<int>(0, width - 1 - text.size()), ' ');
  return padded + text + " ";
}

}  // namespace

OptionsPanel::OptionsPanel(AccountInstance& account) : account_(account) {
}

void OptionsPanel::Reset() {
  row_ = 0;
}

void OptionsPanel::MoveRow(int delta) {
  // The Close button is one more stop after the settings.
  row_ = StepCursor(row_, delta, kOptionCount + 1);
}

Option OptionsPanel::selected_option() const {
  return OptionAt(row_);
}

bool OptionsPanel::IsVolume(Option option) {
  return option == Option::kMapBgmVolume || option == Option::kBossBgmVolume;
}

bool OptionsPanel::IsOn(Option option) const {
  switch (option) {
    case Option::kPanelTitleBlink:
      return account_.panel_title_blink();
    case Option::kMapBgmVolume:
    case Option::kBossBgmVolume:
      return false;
  }
  return false;
}

int OptionsPanel::VolumeOf(Option option) const {
  switch (option) {
    case Option::kMapBgmVolume:
      return account_.map_bgm_volume();
    case Option::kBossBgmVolume:
      return account_.boss_bgm_volume();
    case Option::kPanelTitleBlink:
      return 0;
  }
  return 0;
}

void OptionsPanel::Toggle() {
  if (on_close() || IsVolume(selected_option())) {
    return;
  }
  switch (selected_option()) {
    case Option::kPanelTitleBlink:
      account_.SetPanelTitleBlink(!account_.panel_title_blink());
      return;
    case Option::kMapBgmVolume:
    case Option::kBossBgmVolume:
      return;
  }
}

void OptionsPanel::Adjust(int delta) {
  if (on_close() || !IsVolume(selected_option())) {
    return;
  }
  // The account clamps, so a held key runs into the end and stays there.
  switch (selected_option()) {
    case Option::kMapBgmVolume:
      account_.SetMapBgmVolume(account_.map_bgm_volume() + delta);
      return;
    case Option::kBossBgmVolume:
      account_.SetBossBgmVolume(account_.boss_bgm_volume() + delta);
      return;
    case Option::kPanelTitleBlink:
      return;
  }
}

ftxui::Element OptionsPanel::RenderBar(int volume) const {
  float filled = static_cast<float>(volume) / kMaxBgmVolume;
  return ftxui::hbox({
      ProgressBar(filled, kTheme, "") |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBarWidth),
      ftxui::text(RightAligned(std::to_string(volume), kValueWidth)),
  });
}

ftxui::Element OptionsPanel::RenderRow(Option option, int row) const {
  bool selected = row == row_ && !on_close();
  ftxui::Element name = ftxui::text(" " + OptionName(option)) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kNameWidth);
  ftxui::Element value =
      IsVolume(option)
          ? RenderBar(VolumeOf(option))
          : ftxui::text(IsOn(option) ? kChecked : kUnchecked) | ftxui::center;
  value =
      std::move(value) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kValueColumn);
  // A band rather than an inversion: inverting a bar swaps what is filled for
  // what is not, which reads as the opposite volume.
  return HighlightRow(ftxui::hbox({std::move(name), std::move(value)}),
                      selected);
}

ftxui::Element OptionsPanel::Render() const {
  ftxui::Elements rows;
  for (int i = 0; i < kOptionCount; ++i) {
    rows.push_back(RenderRow(OptionAt(i), i));
  }
  // The room the settings still to come will take.
  for (int i = kOptionCount; i < kListRows; ++i) {
    rows.push_back(ftxui::text(""));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(CenteredRow(ActionButton("Close", on_close())));
  return ThemedWindow(" Options ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
