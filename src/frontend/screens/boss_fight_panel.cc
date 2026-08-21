#include "src/frontend/screens/boss_fight_panel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/box.hpp"
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

// One panel and the cell it stands in.
struct ArenaCell {
  int x = 0;
  int y = 0;
};

// The arena's own layout: the panels handed to it are spread over whatever box
// it is given, each one centred in its cell of an `columns` x `rows` grid.
//
// A grid rather than a row of stretched gaps, because the cells have to line
// up DOWN the screen as well as across it: Zakum's arms stand four to a side
// with the player between them, and a row of two would otherwise share out its
// spare room differently from a row of three.
//
// Panels keep the size they asked for. Where the cells are too narrow to hold
// them apart the panels are pushed right, in order, so they touch rather than
// overlap -- a bar half-drawn over another one names neither.
class ArenaNode : public ftxui::Node {
 public:
  ArenaNode(ftxui::Elements panels, std::vector<ArenaCell> cells, int columns,
            int rows)
      : ftxui::Node(std::move(panels)),
        cells_(std::move(cells)),
        columns_(std::max(1, columns)),
        rows_(std::max(1, rows)) {
  }

  void ComputeRequirement() override {
    requirement_ = ftxui::Requirement();
    std::map<int, int> row_width;
    int tallest = 0;
    for (std::size_t i = 0; i < children_.size(); ++i) {
      children_[i]->ComputeRequirement();
      row_width[cells_[i].y] += children_[i]->requirement().min_x;
      tallest = std::max(tallest, children_[i]->requirement().min_y);
    }
    for (const std::pair<const int, int>& row : row_width) {
      requirement_.min_x = std::max(requirement_.min_x, row.second);
    }
    requirement_.min_y = tallest * static_cast<int>(row_width.size());
    // It is the arena: it takes the room it is offered rather than the room
    // its bars happen to need.
    requirement_.flex_grow_x = 1;
    requirement_.flex_grow_y = 1;
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    std::map<int, std::vector<std::size_t>> by_row;
    for (std::size_t i = 0; i < children_.size(); ++i) {
      by_row[cells_[i].y].push_back(i);
    }
    for (std::pair<const int, std::vector<std::size_t>>& row : by_row) {
      PlaceRow(box, row.first, row.second);
    }
  }

 private:
  // Where a cell's centre falls, as a share of the box it is drawn in.
  static int CentreOf(int cell, int cells, int low, int high) {
    double span = static_cast<double>(high - low + 1) / cells;
    return low + static_cast<int>((cell + 0.5) * span);
  }

  void PlaceRow(ftxui::Box box, int y, std::vector<std::size_t> row) {
    std::sort(row.begin(), row.end(), [this](std::size_t a, std::size_t b) {
      return cells_[a].x < cells_[b].x;
    });
    int height = children_[row.front()]->requirement().min_y;
    int top = CentreOf(y, rows_, box.y_min, box.y_max) - height / 2;
    top = std::clamp(top, box.y_min, std::max(box.y_min, box.y_max - height));
    // Filled from the left: the first panel takes its own place, and each one
    // after it stands where the cell asks or against its neighbour.
    int taken = box.x_min;
    for (std::size_t i : row) {
      int width = children_[i]->requirement().min_x;
      int left =
          CentreOf(cells_[i].x, columns_, box.x_min, box.x_max) - width / 2;
      left =
          std::max(std::clamp(left, box.x_min, box.x_max - width + 1), taken);
      children_[i]->SetBox({left, left + width - 1, top, top + height - 1});
      taken = left + width;
    }
  }

  std::vector<ArenaCell> cells_;
  int columns_ = 1;
  int rows_ = 1;
};

// The arena: every bar of the phase in the cell the fight gave it, and the
// player among them, spread over the whole of the screen under the clock.
ftxui::Element Arena(const BossRun& run) {
  const std::vector<BossSlot>& slots = run.slots();
  // One height for every panel in the phase, monsters and player alike, so a
  // row of bars sits on one line however many rows their names took.
  int rows = kPanelBorder + std::max(MobBarRows(slots), kPlayerBarRows);
  int height = std::max(run.arena_height(), run.player_spot().y() + 1);
  ftxui::Elements panels;
  std::vector<ArenaCell> cells;
  for (const BossSlot& slot : slots) {
    if (!slot.visible || slot.y < 0 || slot.y >= height) {
      continue;
    }
    panels.push_back(MobBar(slot, MobBarRows(slots)) |
                     ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows));
    cells.push_back({slot.x, slot.y});
  }
  panels.push_back(PlayerPanel(run) |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows));
  cells.push_back({run.player_spot().x(),
                   std::clamp(run.player_spot().y(), 0, height - 1)});
  return std::make_shared<ArenaNode>(std::move(panels), std::move(cells),
                                     run.arena_width(), height);
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
  // The arena takes everything under the clock: what it does with the room is
  // the phase's own business, and a fight drawn small in the middle of a wide
  // screen is not what standing in an arena looks like.
  return ftxui::vbox({
      ProgressBar(static_cast<float>(run.phase_hp_fraction()), kRed,
                  FightHeading(run)),
      ClockPanel(run),
      Arena(run) | ftxui::flex,
  });
}

}  // namespace ms
