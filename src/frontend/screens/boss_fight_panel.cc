#include "src/frontend/screens/boss_fight_panel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// The border a panel draws and the column of clearance it keeps inside it,
// both sides: what a name laid on a bar has to fit inside.
constexpr int kPanelClearance = 4;
// The two rows of border a panel spends before it draws anything.
constexpr int kPanelBorder = 2;

// A whole percent, rounded down so a bar with anything left never reads 0.
int Percent(double fraction) {
  int pct = static_cast<int>(fraction * 100.0);
  if (pct == 0 && fraction > 0.0) {
    return 1;
  }
  return std::clamp(pct, 0, 100);
}

std::vector<std::string> BarLines(const std::string& text, int rows) {
  std::vector<std::string> lines =
      WrapBalanced(text, kBossPanelWidth - kPanelClearance);
  lines.resize(rows);
  return lines;
}

// How many rows every monster bar in the phase takes: one, unless a name in it
// needs two. One number for the whole phase, so the arena's rows stay square
// and a part that dies cannot change the height of the row it stood in.
int MobBarRows(const std::vector<BossSlot>& slots) {
  int rows = 1;
  for (const BossSlot& slot : slots) {
    int needed = static_cast<int>(
        WrapBalanced(slot.name, kBossPanelWidth - kPanelClearance).size());
    rows = std::max(rows, std::min(needed, kMaxMobBarRows));
  }
  return rows;
}

// A monster's bar: what is left of it, with its name wrapped over the fill the
// way the player's swing is. The percent goes in the title, where the name
// will not fit and a number reads as a badge on the frame.
ftxui::Element MobBar(const BossSlot& slot, int rows) {
  ftxui::Element bar = ProgressBar(static_cast<float>(slot.hp_fraction), kRed,
                                   BarLines(slot.name, rows));
  return ThemedWindow(" " + std::to_string(Percent(slot.hp_fraction)) + "% ",
                      std::move(bar)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBossPanelWidth);
}

// The player's own panel: whatever they are winding up, under their name. No
// HP -- nothing in a boss fight hits back yet.
ftxui::Element PlayerPanel(const BossRun& run) {
  std::string label = run.attack_name();
  if (run.state() == BossRunState::kCountdown) {
    // The count-in stands where the swing name will: it is the one thing on
    // screen that is about to change, so it belongs where the eye already is.
    label = std::to_string(
        static_cast<int>(std::ceil(std::max(0.0, run.countdown_left()))));
  }
  ftxui::Element bar = ProgressBar(static_cast<float>(run.attack_fraction()),
                                   kTheme, BarLines(label, kPlayerBarRows));
  return ThemedWindow(" You ", std::move(bar)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBossPanelWidth);
}

// One panel and the spot it stands in, while a row is being laid out.
struct ArenaCell {
  int x = 0;
  ftxui::Element panel;
};

// One row of the arena: its panels in their columns, and empty space
// everywhere else. Fixed height, so a row of short bars leaves the row below
// it where it was.
ftxui::Element ArenaRow(std::vector<ArenaCell> cells, int width, int height) {
  std::sort(cells.begin(), cells.end(),
            [](const ArenaCell& a, const ArenaCell& b) { return a.x < b.x; });
  ftxui::Elements row;
  int column = 0;
  for (ArenaCell& cell : cells) {
    int start = std::max(column, cell.x * kArenaStep);
    if (start > column) {
      row.push_back(ftxui::text(std::string(start - column, ' ')));
    }
    row.push_back(std::move(cell.panel));
    column = start + kBossPanelWidth;
  }
  int columns = width * kArenaStep;
  if (columns > column) {
    row.push_back(ftxui::text(std::string(columns - column, ' ')));
  }
  return ftxui::hbox(std::move(row)) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, height);
}

// The arena: every bar of the phase in the spot the fight gave it, and the
// player among them. Every row is the height of the tallest panel that could
// stand in it, so the grid is square and nothing shifts as parts die.
ftxui::Element Arena(const BossRun& run) {
  const std::vector<BossSlot>& slots = run.slots();
  int mob_rows = MobBarRows(slots);
  int row_height = kPanelBorder + std::max(mob_rows, kPlayerBarRows);
  int height = std::max(run.arena_height(), run.player_spot().y() + 1);
  std::vector<std::vector<ArenaCell>> rows(height);
  for (const BossSlot& slot : slots) {
    if (!slot.visible || slot.y < 0 || slot.y >= height) {
      continue;
    }
    rows[slot.y].push_back({slot.x, MobBar(slot, mob_rows)});
  }
  int player_row = std::clamp(run.player_spot().y(), 0, height - 1);
  rows[player_row].push_back({run.player_spot().x(), PlayerPanel(run)});

  ftxui::Elements arena;
  for (std::vector<ArenaCell>& row : rows) {
    arena.push_back(ArenaRow(std::move(row), run.arena_width(), row_height));
  }
  return ftxui::vbox(std::move(arena)) | ftxui::center;
}

// The clock, in a box of its own under the heading.
ftxui::Element ClockPanel(const BossRun& run) {
  return ThemedWindow("", CenteredRow(FightClock(run.seconds_left()))) |
         ftxui::center;
}

}  // namespace

std::string FightClock(double seconds_left) {
  int total = static_cast<int>(std::ceil(std::max(0.0, seconds_left)));
  int minutes = total / 60;
  int seconds = total % 60;
  std::string text = std::to_string(minutes) + ":";
  if (seconds < 10) {
    text += "0";
  }
  return text + std::to_string(seconds);
}

std::string FightHeading(const BossRun& run) {
  switch (run.state()) {
    case BossRunState::kWon:
      return run.title() + " - Cleared";
    case BossRunState::kTimedOut:
      return run.title() + " - Out of Time";
    case BossRunState::kAborted:
      return run.title() + " - Left";
    default:
      return run.title() + " - P" + std::to_string(run.phase()) + " - " +
             std::to_string(Percent(run.phase_hp_fraction())) + "%";
  }
}

ftxui::Element BossFightPanel(const BossRun& run) {
  return ftxui::vbox({
      ProgressBar(static_cast<float>(run.phase_hp_fraction()), kRed,
                  FightHeading(run)),
      ClockPanel(run),
      ftxui::filler(),
      Arena(run),
      ftxui::filler(),
  });
}

}  // namespace ms
