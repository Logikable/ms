#include "src/frontend/screens/stat_inspect_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {
namespace {

constexpr int kCardChrome = 2;  // the two border columns
// "Final Stat", a gap, and room for a nine-digit value, with a gutter each
// side. Wide enough for the " Stat Inspect " title and "Magic Attack".
constexpr int kContentWidth = 24;

// 4.53 reads "453%", 0.155 "15.5%": two decimals with trailing zeros trimmed,
// since most %stat comes in whole percents.
std::string PercentText(double fraction) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", fraction * 100.0);
  std::string text = buf;
  text.erase(text.find_last_not_of('0') + 1);
  if (text.back() == '.') {
    text.pop_back();
  }
  return text + "%";
}

// A label against the left gutter and its value against the right one.
ftxui::Element Line(const std::string& label, const std::string& value) {
  int gap = kContentWidth - 2 - static_cast<int>(value.size());
  return ftxui::text(" " + PadRight(label, std::max(0, gap)) + value + " ");
}

}  // namespace

int StatInspectPanel::Columns() {
  return kCardChrome + kContentWidth;
}

ftxui::Element StatInspectPanel::Render() const {
  std::vector<ftxui::Element> rows = {
      CenteredRow(InspectStatName(stat_)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth),
      ThemedSeparator(),
      Line("Base Stat", std::to_string(breakdown_.base)),
      Line("% Stat", PercentText(breakdown_.pct)),
      Line("Final Stat", std::to_string(breakdown_.final_stat)),
      ThemedSeparator(),
      Line("Total", std::to_string(breakdown_.total)),
  };
  return ThemedWindow(" Stat Inspect ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
