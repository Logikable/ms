#include "src/frontend/screens/boss_fight_panel.h"

#include <algorithm>
#include <chrono>
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
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/boss.pb.h"

namespace ms {
namespace {

// The border and the blank column inside it, on both sides: the space a name
// drawn on a bar has to fit inside.
constexpr int kPanelClearance = 4;
// The two border rows a panel uses before drawing anything.
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

// How many rows every monster bar in the phase takes: one, unless a name needs
// two. One number for the whole phase, so the arena's rows stay even and a part
// that dies can't change the height of its row.
int MobBarRows(const std::vector<BossSlot>& slots) {
  int rows = 1;
  for (const BossSlot& slot : slots) {
    int needed = static_cast<int>(
        WrapBalanced(slot.name, kBossPanelWidth - kPanelClearance).size());
    rows = std::max(rows, std::min(needed, kMaxMobBarRows));
  }
  return rows;
}

// A monster's bar: its remaining HP, with its name wrapped over the fill like
// the player's attack name. The percent goes in the title, since the name won't
// fit there and a number reads well as a badge on the frame.
ftxui::Element MobBar(const BossSlot& slot, int rows) {
  ftxui::Element bar = ProgressBar(static_cast<float>(slot.hp_fraction), kRed,
                                   BarLines(slot.name, rows));
  return ThemedWindow(" " + std::to_string(Percent(slot.hp_fraction)) + "% ",
                      std::move(bar)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBossPanelWidth);
}

// Seconds as a duration for the marquee. The run counts in doubles, and
// everything that scrolls takes a duration.
std::chrono::steady_clock::duration Since(double seconds) {
  return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(seconds));
}

// One player's panel: whatever they are charging, under their name. No HP,
// since bosses don't attack players. `self` is the player at this screen, whose
// panel is bright and shows the countdown.
ftxui::Element MemberPanel(const BossRun& run, const FightMember& member,
                           bool self, bool buff_dots) {
  std::string label = member.attack_name;
  if (self && run.state() == BossRunState::kCountdown) {
    // The countdown sits where the attack name will be: it is the one thing on
    // screen about to change, so it belongs where the eye already is.
    label = std::to_string(
        static_cast<int>(std::ceil(std::max(0.0, run.countdown_left()))));
  }
  ftxui::Color accent = self ? kTheme : kFaintTheme;
  ftxui::Element bar = ProgressBar(static_cast<float>(member.attack_fraction),
                                   accent, BarLines(label, kPlayerBarRows),
                                   buff_dots ? member.buff_count : 0);
  // Everyone else is named; the player is not, since they know who they are. A
  // name longer than the panel scrolls on the run's own clock. There is no
  // cursor here to start it, so it scrolls all fight.
  std::string title =
      self ? "You"
           : ScrollingWindow(member.name, kBossPanelWidth - kPanelClearance,
                             Since(run.elapsed_seconds()));
  return AccentWindow(" " + title + " ", std::move(bar), accent) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBossPanelWidth);
}

// A spot the player could stand on but doesn't. Dim, unframed and the size of a
// bar so the arena's cells stay even. It shows how far movement goes, not that
// anything is there.
ftxui::Element EmptySpot(int rows) {
  return ftxui::vbox({
             ftxui::filler(),
             ftxui::text("· · ·") | ftxui::center | ftxui::dim,
             ftxui::filler(),
         }) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBossPanelWidth) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows);
}

// Whether someone is standing on the spot at `index`, which makes it not empty.
bool Stood(const std::vector<FightMember>& members, int index) {
  for (const FightMember& member : members) {
    if (member.spot == index) {
      return true;
    }
  }
  return false;
}

// One panel and its cell.
struct ArenaCell {
  int x = 0;
  int y = 0;
};

// Numbers drawn directly onto the screen instead of built from rows, because
// how many fit isn't known until the arena has placed everything else, and
// SetBox runs after rows would have been built.
//
// The box covers every row they could take, and the arena says which to draw,
// so a row holding a bar costs only that one number. Stacked upwards from the
// last row, so the earliest hit is at the bottom.
class DamageNumbersNode : public ftxui::Node {
 public:
  // A party member's stack: one of their attacks on one monster, showing one
  // strike at a time. Drawn faint, well below the player's own numbers.
  explicit DamageNumbersNode(const DamageStack& stack) : faint_(true) {
    // Only the current strike is drawn, but the box is as tall and wide as the
    // largest strike, since a stack that changed shape as it flashed would move
    // every frame.
    std::pair<int, int> showing = stack.StrikeAt(stack.age);
    rows_ = std::max(1, stack.TallestStrike());
    for (int i = 0; i < static_cast<int>(stack.lines.size()); ++i) {
      int width =
          static_cast<int>(std::to_string(stack.lines[i].damage).size());
      width_ = std::max(width_, width);
      if (i >= showing.first && i < showing.second) {
        numbers_.push_back(
            {std::to_string(stack.lines[i].damage), stack.lines[i].crit});
      }
    }
  }

  // The player's own column above one monster. Every row is drawn: a row holds
  // whichever number is there now, and an empty row holds a gap so the rows
  // above keep their place.
  explicit DamageNumbersNode(const std::vector<DamageRow>& column) {
    rows_ = std::max(1, static_cast<int>(column.size()));
    for (const DamageRow& row : column) {
      std::string text =
          row.filled ? std::to_string(row.number.damage) : std::string();
      width_ = std::max(width_, static_cast<int>(text.size()));
      numbers_.push_back({std::move(text), row.filled && row.number.crit});
    }
  }

  // Which numbers to draw, one flag per row. Nothing is drawn until the arena
  // sets this, so a stack it never placed draws nothing.
  void DrawRows(std::vector<bool> rows) {
    drawn_ = std::move(rows);
  }

  void ComputeRequirement() override {
    requirement_.min_x = width_;
    requirement_.min_y = rows_;
  }

  void Render(ftxui::Screen& screen) override {
    if (box_.x_max < box_.x_min) {
      return;
    }
    for (std::size_t i = 0; i < numbers_.size(); ++i) {
      int row = rows_ - 1 - static_cast<int>(i);
      if (row < 0 || row >= static_cast<int>(drawn_.size()) || !drawn_[row]) {
        continue;
      }
      DrawRow(screen, box_.y_min + row, numbers_[i]);
    }
  }

 private:
  struct Number {
    std::string text;
    bool crit = false;
  };

  void DrawRow(ftxui::Screen& screen, int y, const Number& number) {
    if (number.text.empty()) {
      return;
    }
    // Right-aligned, so the digits of every row line up.
    int left = box_.x_max - static_cast<int>(number.text.size()) + 1;
    for (std::size_t i = 0; i < number.text.size(); ++i) {
      int x = left + static_cast<int>(i);
      if (x < box_.x_min || x > box_.x_max) {
        continue;
      }
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = std::string(1, number.text[i]);
      if (faint_) {
        px.foreground_color = number.crit ? kFaintOrange : kFaintTheme;
      } else {
        px.foreground_color = number.crit ? kOrange : kTheme;
      }
      px.bold = number.crit && !faint_;
    }
  }

  std::vector<Number> numbers_;
  std::vector<bool> drawn_;
  // The rows in the box, which is the tallest strike rather than what is drawn
  // this frame. A shorter strike uses the last of them and leaves the rest
  // untouched.
  int rows_ = 1;
  int width_ = 1;
  bool faint_ = false;
};

// One party member's stack and the panel it goes beside.
struct ArenaStack {
  std::size_t owner = 0;  // index into the arena's panels: the monster it hit
  int preference = 0;     // which side of that panel to try first
  std::shared_ptr<DamageNumbersNode> node;
};

// The player's own column of numbers and the monster bar it sits over.
struct ArenaColumn {
  std::size_t owner = 0;
  std::shared_ptr<DamageNumbersNode> node;
};

// The sides of a bar where a party member's stack can go, in the order the
// arena tries them. Each stack's own preference rotates the list, so two stacks
// on one monster don't both try the same side first.
enum class Side { kBelow, kLeft, kRight };
constexpr Side kSides[] = {Side::kBelow, Side::kLeft, Side::kRight};

bool Overlaps(const ftxui::Box& a, const ftxui::Box& b) {
  return a.x_min <= b.x_max && b.x_min <= a.x_max && a.y_min <= b.y_max &&
         b.y_min <= a.y_max;
}

// The arena's layout: each panel centred in its cell of a `columns` x `rows`
// grid. A grid rather than stretched gaps, because cells must line up down the
// screen as well as across; otherwise a row of two would share its spare room
// differently from a row of three.
//
// Panels keep the size they asked for, and where cells are too narrow they are
// pushed right in order so they touch instead of overlapping.
class ArenaNode : public ftxui::Node {
 public:
  ArenaNode(ftxui::Elements panels, std::vector<ArenaCell> cells, int columns,
            int rows, std::size_t mobs, std::vector<ArenaColumn> numbers,
            std::vector<ArenaStack> stacks, ftxui::Element clock)
      : ftxui::Node(std::move(panels)),
        cells_(std::move(cells)),
        columns_(std::max(1, columns)),
        rows_(std::max(1, rows)),
        panels_(children_.size()),
        mobs_(mobs),
        numbers_(std::move(numbers)),
        stacks_(std::move(stacks)) {
    for (const ArenaColumn& column : numbers_) {
      children_.push_back(column.node);
    }
    for (const ArenaStack& stack : stacks_) {
      children_.push_back(stack.node);
    }
    // Last, so it is drawn over anything that reached its rows: the clock must
    // always be readable.
    clock_ = children_.size();
    children_.push_back(std::move(clock));
  }

  void ComputeRequirement() override {
    requirement_ = ftxui::Requirement();
    std::map<int, int> row_width;
    int tallest = 0;
    // The numbers are measured too, since they are children, but they ask the
    // arena for nothing. They go in the space the bars leave.
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
    // It is the arena, so it takes the space it is offered rather than the
    // space its bars need.
    requirement_.flex_grow_x = 1;
    requirement_.flex_grow_y = 1;
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    ftxui::Box clock = PlaceClock(box);
    // The bars are laid out below the clock. Only the numbers reach up into its
    // rows.
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
    PlaceNumbers(box, clock);
  }

 private:
  // Where a stack could go, and how many of its rows would show there.
  struct Spot {
    ftxui::Box box;
    int rows = 0;
  };

  // Where a cell's centre falls, as a proportion of the box it is drawn in.
  static int CentreOf(int cell, int cells, int low, int high) {
    double span = static_cast<double>(high - low + 1) / cells;
    return low + static_cast<int>((cell + 0.5) * span);
  }

  // Places the clock centred across the top of the arena. Its box is returned
  // so nothing is placed under it: a stack may share its rows, but not any cell
  // of the clock itself, border included.
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
    // Filled from the left: the first panel takes its own place, and each later
    // one goes where its cell asks or against its neighbour.
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

  // Places the numbers: the player's own columns over their monsters first,
  // since that space is theirs, then the party's in the space left beside the
  // bars.
  void PlaceNumbers(ftxui::Box box, ftxui::Box clock) {
    std::vector<ftxui::Box> taken;
    taken.reserve(panels_ + numbers_.size() + stacks_.size() + 1);
    for (std::size_t i = 0; i < panels_; ++i) {
      taken.push_back(panel_box_[i]);
    }
    taken.push_back(clock);
    for (const ArenaColumn& column : numbers_) {
      PlaceColumn(box, column, taken);
    }
    // The rows over a monster's bar belong to this player whether or not they
    // hold a number now, so a party member's stack doesn't jump aside the
    // moment the player lands a hit.
    std::vector<ftxui::Box> reserved = taken;
    for (std::size_t i = 0; i < mobs_; ++i) {
      reserved.push_back({panel_box_[i].x_min, panel_box_[i].x_max, box.y_min,
                          panel_box_[i].y_min - 1});
    }
    for (const ArenaStack& stack : stacks_) {
      PlaceBeside(box, stack, taken, reserved);
    }
  }

  // Places the player's column over the monster it hit, with the bottom row
  // against the bar. A row outside the arena or on top of something else is not
  // drawn, since these numbers belong over their target.
  void PlaceColumn(ftxui::Box arena, const ArenaColumn& column,
                   std::vector<ftxui::Box>& taken) {
    ftxui::Box owner = panel_box_[column.owner];
    int width = column.node->requirement().min_x;
    int height = column.node->requirement().min_y;
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
    column.node->DrawRows(std::move(drawn));
    column.node->SetBox({left, left + width - 1, top, top + height - 1});
  }

  // Places a party member's stack in the first free space its preference
  // reaches. One that fits nowhere whole takes the side that shows the most of
  // it.
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
    // The rows that fit are the box's last ones, since the stack reads upwards:
    // a cramped side loses the top of the stack. The rows above hang off the
    // top of the spot and are never drawn.
    int height = stack.node->requirement().min_y;
    std::vector<bool> drawn(height, false);
    for (int row = height - best.rows; row < height; ++row) {
      drawn[row] = true;
    }
    stack.node->DrawRows(std::move(drawn));
    int top = best.box.y_min - (height - best.rows);
    stack.node->SetBox({best.box.x_min, best.box.x_max, top, top + height - 1});
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

  // The space `side` of `owner` offers a stack of `want`, inside `arena` and
  // clear of `blocked`. Rows are dropped from the far end until it fits, so a
  // cramped side shows part of the stack instead of nothing.
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
      // Sideways, the width must fit inside the arena: a stack that runs off
      // the edge shows a number the player can't read.
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
  // How many of the children are panels. The stacks come after them, and the
  // clock is the very last child.
  std::size_t panels_ = 0;
  std::size_t clock_ = 0;
  // How many of those panels are monster bars. They come first, so the bar
  // under a reserved column is one of the first `mobs_` boxes.
  std::size_t mobs_ = 0;
  // Where each panel was placed. Kept because a Node doesn't expose its box,
  // and the stacks have to be placed clear of them.
  std::vector<ftxui::Box> panel_box_;
  std::vector<ArenaColumn> numbers_;
  std::vector<ArenaStack> stacks_;
};

// The clock, which the arena places across its own top row instead of giving it
// a separate strip of the screen.
ftxui::Element ClockPanel(const BossRun& run) {
  return ThemedWindow("", CenteredRow(FormatClock(run.seconds_left())));
}

// The arena: every bar of the phase in the cell the fight gave it, the player
// among them, and the clock above, filling the screen below the heading.
ftxui::Element Arena(const BossRun& run, bool buff_dots) {
  const std::vector<BossSlot>& slots = run.slots();
  // One height for every panel in the phase, monsters and player alike, so a
  // row of bars lines up however many rows their names took.
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
  // The monster bars come first, and the arena relies on that: the column over
  // each is kept clear for the player's hits on that monster.
  std::size_t mobs = panels.size();
  std::vector<ArenaSpot> spots = run.player_spots();
  const std::vector<FightMember>& members = run.members();
  for (int i = 0; i < static_cast<int>(spots.size()); ++i) {
    if (Stood(members, i) || spots[i].y() < 0 || spots[i].y() >= height) {
      continue;
    }
    panels.push_back(EmptySpot(rows));
    cells.push_back({spots[i].x(), spots[i].y()});
  }
  // The player at this screen is first in the list and drawn last, so a panel
  // pushed aside for room is someone else's.
  for (std::size_t i = members.size(); i > 0; --i) {
    const FightMember& member = members[i - 1];
    ArenaSpot standing;
    if (member.spot >= 0 && member.spot < static_cast<int>(spots.size())) {
      standing = spots[member.spot];
    }
    panels.push_back(MemberPanel(run, member, i == 1, buff_dots) |
                     ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows));
    cells.push_back({standing.x(), std::clamp(standing.y(), 0, height - 1)});
  }
  // After the panels, so the numbers are placed knowing where every bar is,
  // including the one their monster is on.
  std::vector<ArenaColumn> numbers;
  for (const std::pair<const int, std::size_t>& slot : panel_of_slot) {
    std::vector<DamageRow> column =
        DamageColumn(run.damage_writes(), slot.first);
    if (column.empty()) {
      continue;
    }
    numbers.push_back(
        {slot.second, std::make_shared<DamageNumbersNode>(column)});
  }
  std::vector<ArenaStack> stacks;
  for (const DamageStack& stack : run.damage_stacks()) {
    std::map<int, std::size_t>::const_iterator it =
        panel_of_slot.find(stack.mob_id);
    if (it == panel_of_slot.end() || stack.lines.empty()) {
      continue;
    }
    stacks.push_back({it->second, stack.preference,
                      std::make_shared<DamageNumbersNode>(stack)});
  }
  return std::make_shared<ArenaNode>(
      std::move(panels), std::move(cells), run.arena_width(), height, mobs,
      std::move(numbers), std::move(stacks), ClockPanel(run));
}

}  // namespace

std::string FightHeading(const BossRun& run) {
  // Practice comes first in the heading rather than last: the end holds the
  // phase and the percent, which change, and what this run is worth shouldn't
  // have to be found among them.
  std::string practice = run.practice() ? "Practice - " : "";
  switch (run.state()) {
    case BossRunState::kWon:
      return practice + run.title() + " - Cleared";
    case BossRunState::kTimedOut:
      return practice + run.title() + " - Out of Time";
    case BossRunState::kAborted:
      return practice + run.title() + " - Left";
    default: {
      // The phase is shown only for a fight with more than one: "P1" on a
      // one-room boss adds nothing.
      std::string phase =
          run.phase_count() > 1 ? " - P" + std::to_string(run.phase()) : "";
      return practice + run.title() + phase + " - " +
             std::to_string(Percent(run.phase_hp_fraction())) + "%";
    }
  }
}

ftxui::Element BossFightPanel(const BossRun& run, bool buff_dots) {
  // The arena takes everything below the heading, clock included. A fight drawn
  // small in the middle of a wide screen doesn't look like standing in an
  // arena.
  return ftxui::vbox({
      ProgressBar(static_cast<float>(run.phase_hp_fraction()), kRed,
                  FightHeading(run)),
      Arena(run, buff_dots) | ftxui::flex,
  });
}

}  // namespace ms
