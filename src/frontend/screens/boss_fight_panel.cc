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
#include "src/protos/boss.pb.h"

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

// Somewhere the player may stand and is not. Dim and unframed, the size of a
// bar so the arena's cells stay square: what it says is that the walk goes
// this far, not that anything is standing here.
ftxui::Element EmptySpot(int rows) {
  return ftxui::vbox({
             ftxui::filler(),
             ftxui::text("· · ·") | ftxui::center | ftxui::dim,
             ftxui::filler(),
         }) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBossPanelWidth) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows);
}

// One panel and the cell it stands in.
struct ArenaCell {
  int x = 0;
  int y = 0;
};

// A stack of damage numbers, drawn straight onto the screen rather than built
// out of rows: which of them there is room for is not known until the arena
// has placed everything else, and SetBox runs after the rows would have been
// built.
//
// The box is the whole stack's worth of rows, its n'th number on its n'th row,
// and the arena says which of those rows to draw. So a row with a bar in it
// costs that one number and leaves the rest where they were.
class DamageStackNode : public ftxui::Node {
 public:
  explicit DamageStackNode(const DamageStack& stack) {
    for (const DamageNumber& line : stack.lines) {
      numbers_.push_back({std::to_string(line.damage), line.crit});
      width_ = std::max(width_, static_cast<int>(numbers_.back().text.size()));
    }
  }

  // Which of the numbers to draw, one flag per row. Nothing until the arena
  // has said, so a stack it never placed draws nothing.
  void DrawRows(std::vector<bool> rows) {
    drawn_ = std::move(rows);
  }

  void ComputeRequirement() override {
    requirement_.min_x = width_;
    requirement_.min_y = static_cast<int>(numbers_.size());
  }

  void Render(ftxui::Screen& screen) override {
    if (box_.x_max < box_.x_min) {
      return;
    }
    for (std::size_t row = 0; row < numbers_.size(); ++row) {
      if (row >= drawn_.size() || !drawn_[row]) {
        continue;
      }
      DrawRow(screen, box_.y_min + static_cast<int>(row), numbers_[row]);
    }
  }

 private:
  struct Number {
    std::string text;
    bool crit = false;
  };

  void DrawRow(ftxui::Screen& screen, int y, const Number& number) {
    int width = box_.x_max - box_.x_min + 1;
    int left = box_.x_min + (width - static_cast<int>(number.text.size())) / 2;
    for (std::size_t i = 0; i < number.text.size(); ++i) {
      int x = left + static_cast<int>(i);
      if (x < box_.x_min || x > box_.x_max) {
        continue;
      }
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = std::string(1, number.text[i]);
      px.foreground_color = number.crit ? kOrange : kIceBlue;
      px.bold = number.crit;
    }
  }

  std::vector<Number> numbers_;
  std::vector<bool> drawn_;
  int width_ = 1;
};

// One stack and the panel it belongs beside.
struct ArenaStack {
  std::size_t owner = 0;  // index into the arena's panels
  // Whether this is the character's own swing. The swing always stands over
  // its monster; nothing else may.
  bool swing = false;
  int preference = 0;  // which side of that panel to try first
  std::shared_ptr<DamageStackNode> node;
};

// The sides of a bar a stack that is not the swing can stand on, in the order
// the arena tries them. A stack's own preference rotates the list, so two
// landing on one monster do not both reach for the same side first.
enum class Side { kBelow, kLeft, kRight };
constexpr Side kSides[] = {Side::kBelow, Side::kLeft, Side::kRight};

bool Overlaps(const ftxui::Box& a, const ftxui::Box& b) {
  return a.x_min <= b.x_max && b.x_min <= a.x_max && a.y_min <= b.y_max &&
         b.y_min <= a.y_max;
}

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
            int rows, std::size_t mobs, std::vector<ArenaStack> stacks,
            ftxui::Element clock)
      : ftxui::Node(std::move(panels)),
        cells_(std::move(cells)),
        columns_(std::max(1, columns)),
        rows_(std::max(1, rows)),
        panels_(children_.size()),
        mobs_(mobs),
        stacks_(std::move(stacks)) {
    for (const ArenaStack& stack : stacks_) {
      children_.push_back(stack.node);
    }
    // Last, so it is drawn over anything that reached its rows: the clock is
    // the one thing on this screen that must always be readable.
    clock_ = children_.size();
    children_.push_back(std::move(clock));
  }

  void ComputeRequirement() override {
    requirement_ = ftxui::Requirement();
    std::map<int, int> row_width;
    int tallest = 0;
    // The stacks are measured too, since they are children, but they ask the
    // arena for nothing: they stand in the room the bars left over.
    for (const ftxui::Element& child : children_) {
      child->ComputeRequirement();
    }
    for (std::size_t i = 0; i < panels_; ++i) {
      row_width[cells_[i].y] += children_[i]->requirement().min_x;
      tallest = std::max(tallest, children_[i]->requirement().min_y);
    }
    for (const std::pair<const int, int>& row : row_width) {
      requirement_.min_x = std::max(requirement_.min_x, row.second);
    }
    requirement_.min_y = tallest * static_cast<int>(row_width.size()) +
                         children_[clock_]->requirement().min_y;
    // It is the arena: it takes the room it is offered rather than the room
    // its bars happen to need.
    requirement_.flex_grow_x = 1;
    requirement_.flex_grow_y = 1;
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    ftxui::Box clock = PlaceClock(box);
    // The bars are laid out under the clock, as they were when it sat in a row
    // of its own. Only the numbers reach up into its rows.
    ftxui::Box body = box;
    body.y_min = std::min(clock.y_max + 1, box.y_max);
    std::map<int, std::vector<std::size_t>> by_row;
    for (std::size_t i = 0; i < panels_; ++i) {
      by_row[cells_[i].y].push_back(i);
    }
    panel_box_.assign(panels_, ftxui::Box());
    for (std::pair<const int, std::vector<std::size_t>>& row : by_row) {
      PlaceRow(body, row.first, row.second);
    }
    PlaceStacks(box, clock);
  }

 private:
  // Where a stack could stand, and how many of its rows would show there.
  struct Spot {
    ftxui::Box box;
    int rows = 0;
  };

  // Where a cell's centre falls, as a share of the box it is drawn in.
  static int CentreOf(int cell, int cells, int low, int high) {
    double span = static_cast<double>(high - low + 1) / cells;
    return low + static_cast<int>((cell + 0.5) * span);
  }

  // Stands the clock across the top of the arena, centred. Its box is handed
  // back so nothing is placed under it: a stack may share its rows, but not a
  // cell of the clock itself, border included.
  ftxui::Box PlaceClock(ftxui::Box box) {
    const ftxui::Element& clock = children_[clock_];
    int width = std::min(clock->requirement().min_x, box.x_max - box.x_min + 1);
    int height =
        std::min(clock->requirement().min_y, box.y_max - box.y_min + 1);
    int left = box.x_min + (box.x_max - box.x_min + 1 - width) / 2;
    ftxui::Box at = {left, left + width - 1, box.y_min, box.y_min + height - 1};
    clock->SetBox(at);
    return at;
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
      panel_box_[i] = {left, left + width - 1, top, top + height - 1};
      children_[i]->SetBox(panel_box_[i]);
      taken = left + width;
    }
  }

  // Stands the stacks: the swings over their own monsters first, since that
  // space is theirs, then everything else in what is left beside the bars.
  void PlaceStacks(ftxui::Box box, ftxui::Box clock) {
    std::vector<ftxui::Box> taken;
    taken.reserve(panels_ + stacks_.size() + 1);
    for (std::size_t i = 0; i < panels_; ++i) {
      taken.push_back(panel_box_[i]);
    }
    taken.push_back(clock);
    for (const ArenaStack& stack : stacks_) {
      if (stack.swing) {
        PlaceSwing(box, stack, taken);
      }
    }
    // The columns over a monster's bar are its swing's, whether or not one is
    // holding numbers just now. Kept clear rather than merely given up first,
    // so a summon's stack does not jump aside the moment a swing lands.
    std::vector<ftxui::Box> reserved = taken;
    for (std::size_t i = 0; i < mobs_; ++i) {
      reserved.push_back({panel_box_[i].x_min, panel_box_[i].x_max, box.y_min,
                          panel_box_[i].y_min - 1});
    }
    for (const ArenaStack& stack : stacks_) {
      if (!stack.swing) {
        PlaceBeside(box, stack, taken, reserved);
      }
    }
  }

  // Stands the character's swing over the monster it hit, centred, its bottom
  // row against the bar. A row of it that falls outside the arena or onto
  // something else is simply not drawn -- the stack does not slide out of the
  // way, because a swing's numbers belong over the thing they were dealt to.
  void PlaceSwing(ftxui::Box arena, const ArenaStack& stack,
                  std::vector<ftxui::Box>& taken) {
    ftxui::Box owner = panel_box_[stack.owner];
    int width = stack.node->requirement().min_x;
    int height = stack.node->requirement().min_y;
    int left = owner.x_min + (owner.x_max - owner.x_min + 1 - width) / 2;
    left = std::clamp(left, arena.x_min,
                      std::max(arena.x_min, arena.x_max - width + 1));
    int top = owner.y_min - height;
    std::vector<bool> drawn(height, false);
    for (int row = 0; row < height; ++row) {
      ftxui::Box line = {left, left + width - 1, top + row, top + row};
      if (line.y_min < arena.y_min || Blocked(line, taken)) {
        continue;
      }
      drawn[row] = true;
      taken.push_back(line);
    }
    stack.node->DrawRows(std::move(drawn));
    stack.node->SetBox({left, left + width - 1, top, top + height - 1});
  }

  // Stands everything that is not the swing in the first free space its own
  // preference reaches for. One that fits nowhere whole takes the side showing
  // the most of it.
  void PlaceBeside(ftxui::Box arena, const ArenaStack& stack,
                   std::vector<ftxui::Box>& taken,
                   const std::vector<ftxui::Box>& reserved) {
    std::vector<ftxui::Box> blocked = reserved;
    blocked.insert(blocked.end(), taken.begin() + panels_, taken.end());
    Spot best;
    int sides = static_cast<int>(std::size(kSides));
    for (int i = 0; i < sides; ++i) {
      Side side = kSides[(stack.preference + i) % sides];
      Spot spot = SpotOn(side, panel_box_[stack.owner],
                         stack.node->requirement(), arena, blocked);
      if (spot.rows > best.rows) {
        best = spot;
      }
      if (best.rows == stack.node->requirement().min_y) {
        break;
      }
    }
    int height = stack.node->requirement().min_y;
    std::vector<bool> drawn(height, false);
    for (int row = 0; row < best.rows; ++row) {
      drawn[row] = true;
    }
    stack.node->DrawRows(std::move(drawn));
    stack.node->SetBox({best.box.x_min, best.box.x_max, best.box.y_min,
                        best.box.y_min + height - 1});
    if (best.rows > 0) {
      taken.push_back(best.box);
    }
  }

  static bool Blocked(ftxui::Box candidate,
                      const std::vector<ftxui::Box>& taken) {
    for (const ftxui::Box& other : taken) {
      if (Overlaps(candidate, other)) {
        return true;
      }
    }
    return false;
  }

  // The room `side` of `owner` offers a stack of the size `want` asks for,
  // inside `arena` and clear of everything in `blocked`. Rows are dropped from
  // the far end until what is left fits, so a cramped side shows the numbers
  // nearest the monster rather than nothing.
  static Spot SpotOn(Side side, ftxui::Box owner, ftxui::Requirement want,
                     ftxui::Box arena, const std::vector<ftxui::Box>& blocked) {
    Spot spot;
    int width = want.min_x;
    int height = want.min_y;
    int left = side == Side::kLeft ? owner.x_min - width
               : side == Side::kRight
                   ? owner.x_max + 1
                   : owner.x_min + (owner.x_max - owner.x_min + 1 - width) / 2;
    int top = side == Side::kBelow
                  ? owner.y_max + 1
                  : owner.y_min + (owner.y_max - owner.y_min + 1 - height) / 2;
    if (side != Side::kBelow &&
        (left < arena.x_min || left + width - 1 > arena.x_max)) {
      // Sideways, the width is what the arena has to hold: a stack that runs
      // off the edge names a number the player cannot read.
      return spot;
    }
    left = std::clamp(left, arena.x_min,
                      std::max(arena.x_min, arena.x_max - width + 1));
    top = std::max(top, arena.y_min);
    for (int rows = height; rows >= 1; --rows) {
      ftxui::Box candidate = {left, left + width - 1, top, top + rows - 1};
      if (candidate.y_max > arena.y_max || Blocked(candidate, blocked)) {
        continue;
      }
      spot.box = candidate;
      spot.rows = rows;
      return spot;
    }
    return spot;
  }

  std::vector<ArenaCell> cells_;
  int columns_ = 1;
  int rows_ = 1;
  // How many of the children are panels. The stacks follow them, and the clock
  // is the last child of all.
  std::size_t panels_ = 0;
  std::size_t clock_ = 0;
  // How many of those panels are monster bars. They come first, so a swing's
  // reserved column is one of the first `mobs_` boxes.
  std::size_t mobs_ = 0;
  // Where each panel was put, kept because a Node does not hand its box back
  // and the stacks have to be placed clear of them.
  std::vector<ftxui::Box> panel_box_;
  std::vector<ArenaStack> stacks_;
};

// The clock, which the arena stands across its own top row rather than taking
// a strip of the screen for.
ftxui::Element ClockPanel(const BossRun& run) {
  return ThemedWindow("", CenteredRow(FightClock(run.seconds_left())));
}

// The arena: every bar of the phase in the cell the fight gave it, the player
// among them, and the clock over them all, spread over the whole of the
// screen under the heading.
ftxui::Element Arena(const BossRun& run) {
  const std::vector<BossSlot>& slots = run.slots();
  // One height for every panel in the phase, monsters and player alike, so a
  // row of bars sits on one line however many rows their names took.
  int rows = kPanelBorder + std::max(MobBarRows(slots), kPlayerBarRows);
  int height = std::max(run.arena_height(), run.player_spot().y() + 1);
  ftxui::Elements panels;
  std::vector<ArenaCell> cells;
  std::map<int, std::size_t> panel_of_slot;
  for (const BossSlot& slot : slots) {
    if (!slot.visible || slot.y < 0 || slot.y >= height) {
      continue;
    }
    panel_of_slot[slot.id] = panels.size();
    panels.push_back(MobBar(slot, MobBarRows(slots)) |
                     ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows));
    cells.push_back({slot.x, slot.y});
  }
  // The monster bars come first, and the arena counts on it: the column over
  // each of them is kept clear for that monster's swing.
  std::size_t mobs = panels.size();
  ArenaSpot standing = run.player_spot();
  for (const ArenaSpot& spot : run.player_spots()) {
    if (spot.x() == standing.x() && spot.y() == standing.y()) {
      continue;
    }
    if (spot.y() < 0 || spot.y() >= height) {
      continue;
    }
    panels.push_back(EmptySpot(rows));
    cells.push_back({spot.x(), spot.y()});
  }
  panels.push_back(PlayerPanel(run) |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows));
  cells.push_back({standing.x(), std::clamp(standing.y(), 0, height - 1)});
  // After the panels, so a stack is placed knowing where every bar stands --
  // including the one whose monster it belongs to.
  std::vector<ArenaStack> stacks;
  for (const DamageStack& stack : run.damage_stacks()) {
    std::map<int, std::size_t>::const_iterator it =
        panel_of_slot.find(stack.mob_id);
    if (it == panel_of_slot.end() || stack.lines.empty()) {
      continue;
    }
    stacks.push_back({it->second, stack.source.origin == DamageOrigin::kSwing,
                      stack.preference,
                      std::make_shared<DamageStackNode>(stack)});
  }
  return std::make_shared<ArenaNode>(std::move(panels), std::move(cells),
                                     run.arena_width(), height, mobs,
                                     std::move(stacks), ClockPanel(run));
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
  // The arena takes everything under the heading, the clock included: what it
  // does with the room is the phase's own business, and a fight drawn small in
  // the middle of a wide screen is not what standing in an arena looks like.
  return ftxui::vbox({
      ProgressBar(static_cast<float>(run.phase_hp_fraction()), kRed,
                  FightHeading(run)),
      Arena(run) | ftxui::flex,
  });
}

}  // namespace ms
