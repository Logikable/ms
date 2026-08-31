#include "src/frontend/screens/analysis_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

std::string StatusText(AnalysisState state) {
  switch (state) {
    case AnalysisState::kStopped:
      return "Stopped";
    case AnalysisState::kWaitingToStart:
      return "Waiting for Respawn to Start";
    case AnalysisState::kRunning:
      return "Running";
    case AnalysisState::kWaitingToStop:
      return "Waiting for Respawn to Stop";
  }
  return "";
}

// The pacing knob as the player reads it: "10x", or "2.5x" if it ever lands
// off a whole number.
std::string SlowdownText(double factor) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%gx", factor);
  return buf;
}

// One row of the panel: the label against the left edge, the value against the
// right, with a column of clearance inside each border.
ftxui::Element DataRow(const std::string& label, const std::string& value,
                       int width) {
  int gap = width - static_cast<int>(value.size());
  return ftxui::text(" " + PadRight(label, std::max(gap, 0)) + value + " ");
}

}  // namespace

std::string FormatClock(double seconds) {
  int64_t whole = static_cast<int64_t>(seconds);
  if (whole < 0) {
    whole = 0;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
           static_cast<long long>(whole / 3600),
           static_cast<long long>(whole / 60 % 60),
           static_cast<long long>(whole % 60));
  return buf;
}

AnalysisPanel::AnalysisPanel(const GameState& state,
                             const BattleAnalysis& analysis)
    : state_(state), analysis_(analysis) {
}

ftxui::Element AnalysisPanel::Render() const {
  std::vector<std::pair<std::string, std::string>> rows = {
      {"Status", StatusText(analysis_.state())},
      {"Game Slowdown Factor",
       SlowdownText(GameSpeedFactor(state_.character.proto().level()))},
  };
  // The measurement itself, under a rule: the two rows above say what the tool
  // is doing, and these ten say what it found.
  std::vector<std::pair<std::string, std::string>> found = {
      {"Time", FormatClock(analysis_.seconds())},
      {"Respawn Cycles", FormatWithCommas(analysis_.cycles())},
      {"Damage Dealt", FormatWithCommas(analysis_.damage())},
      {"DPS", FormatWithCommas(analysis_.damage_per_second())},
      {"Mobs Killed", FormatWithCommas(analysis_.kills())},
      {"Mobs/h", FormatWithCommas(analysis_.kills_per_hour())},
      {"Meso Earned", FormatWithCommas(analysis_.meso())},
      {"Meso/h", FormatWithCommas(analysis_.meso_per_hour())},
      {"EXP Earned", FormatWithCommas(analysis_.exp())},
      {"EXP/h", FormatWithCommas(analysis_.exp_per_hour())},
  };

  ftxui::Elements body;
  for (const std::pair<std::string, std::string>& row : rows) {
    body.push_back(DataRow(row.first, row.second, kContentWidth));
  }
  body.push_back(ThemedSeparator());
  for (const std::pair<std::string, std::string>& row : found) {
    body.push_back(DataRow(row.first, row.second, kContentWidth));
  }
  return DialogWindow(" Battle Analysis ", std::move(body),
                      ActionButton("Close", /*focused=*/true));
}

}  // namespace ms
