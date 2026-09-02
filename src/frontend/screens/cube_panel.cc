#include "src/frontend/screens/cube_panel.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/equip_instance.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Rows the shelf keeps whether or not there are cubes to fill them, so the
// panel is the same height today with one cube as it will be with the last
// one the game ever ships, and the card beside it never moves.
constexpr int kShelfRows = 6;

// The three columns, each as wide as the widest thing that goes in it.
struct ShelfWidths {
  int name = 0;
  int type = 0;
  int cost = 0;
};

ShelfWidths Widths() {
  ShelfWidths widths = {4, 4, 4};  // "Name", "Type", "Cost"
  for (const Cube& cube : kCubes) {
    widths.name = std::max<int>(widths.name, CubeName(cube.type).size());
    widths.type = std::max<int>(widths.type, CubeTrackName(cube.track).size());
    widths.cost = std::max<int>(widths.cost, FormatMeso(cube.cost).size());
  }
  return widths;
}

// One row of the shelf, cursor and all. The cost is its own cell so a price
// out of reach can be said in red while the rest of the row greys -- the
// reason and the door it closes, drawn apart.
ftxui::Element ShelfRow(const Cube& cube, const ShelfWidths& widths,
                        bool selected, bool affordable) {
  ftxui::Element label = ftxui::text(
      (selected ? "> " : "  ") + PadRight(CubeName(cube.type), widths.name) +
      "  " + PadRight(CubeTrackName(cube.track), widths.type) + "  ");
  if (!affordable) {
    label = std::move(label) | ftxui::dim;
  }
  ftxui::Element cost = RedUnless(
      ftxui::text(PadLeft(FormatMeso(cube.cost), widths.cost)), affordable);
  // The margin the window's own border needs, asked for here: the rows are
  // laid out cell by cell, so nothing else is in a position to leave it.
  return ftxui::hbox({std::move(label), std::move(cost), ftxui::text(" ")});
}

}  // namespace

void CubePanel::SetItem(const EquipInstance* item, int64_t meso) {
  item_ = item;
  meso_ = meso;
  // The reroll that emptied the purse is also the one that moves the cursor:
  // asked here rather than where the cube is bought, so the window is right
  // however the purse came to be short.
  if (confirm_.open() && !Affordable()) {
    confirm_.FocusCancel();
  }
}

void CubePanel::Reset() {
  selected_ = 0;
  confirm_.Close();
}

CubeType CubePanel::selected_cube() const {
  return kCubes[selected_].type;
}

int64_t CubePanel::Cost() const {
  return kCubes[selected_].cost;
}

bool CubePanel::Affordable() const {
  return meso_ >= Cost();
}

void CubePanel::MoveCursor(int delta) {
  if (confirm_.open()) {
    return;
  }
  selected_ = StepCursor(selected_, delta, std::size(kCubes));
}

ftxui::Element CubePanel::Render(bool focused) const {
  const ShelfWidths widths = Widths();
  std::vector<ftxui::Element> rows = {
      ftxui::text("  " + PadRight("Name", widths.name) + "  " +
                  PadRight("Type", widths.type) + "  " +
                  PadLeft("Cost", widths.cost) + " "),
      ThemedSeparator(),
  };
  for (int i = 0; i < static_cast<int>(std::size(kCubes)); ++i) {
    rows.push_back(
        ShelfRow(kCubes[i], widths, i == selected_, meso_ >= kCubes[i].cost));
  }
  // The shelf stands at its full height with or without cubes to fill it.
  for (int i = std::size(kCubes); i < kShelfRows; ++i) {
    rows.push_back(ftxui::text(""));
  }
  // The purse rides in the title, where it is read against every row's Cost
  // and cannot be taken for one of them.
  return ThemedWindow(" Cube Selection — " + FormatMeso(meso_) + " ",
                      ftxui::vbox(std::move(rows)), focused);
}

std::vector<ftxui::Element> CubePanel::LineRows() const {
  const Potential& potential = item_->potential();
  if (potential.rank() == POTENTIAL_RANK_UNSPECIFIED) {
    // An item with no potential has no lines to show, and the rows stand
    // empty rather than being left out: the window is the same size before
    // the first cube as after it.
    return std::vector<ftxui::Element>(
        kPotentialLines, CenteredRow(ftxui::text("—") | ftxui::dim));
  }
  const int level = item_->prototype().required_level();
  std::vector<std::pair<std::string, std::string>> lines;
  int name_width = 0;
  int value_width = 0;
  for (const PotentialLine& line : potential.lines()) {
    std::string name = PotentialLineName(line.type());
    std::string value = PotentialLineValueText(line, level);
    name_width = std::max<int>(name_width, name.size());
    value_width = std::max<int>(value_width, value.size());
    lines.push_back({std::move(name), std::move(value)});
  }
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    // The rank dot the inspect card gives each line, so the same three lines
    // read the same way in the window and on the card behind it.
    rows.push_back(CenteredRow(ftxui::hbox({
        ftxui::text("◼ ") |
            ftxui::color(RarityColor(potential.lines(i).rank())),
        ftxui::text(PadRight(lines[i].first, name_width) + "  " +
                    PadLeft(lines[i].second, value_width)),
    })));
  }
  return rows;
}

ftxui::Element CubePanel::RenderConfirm() const {
  const bool fresh = item_ == nullptr ||
                     item_->potential().rank() == POTENTIAL_RANK_UNSPECIFIED;
  std::vector<ftxui::Element> body = {
      CenteredRow(fresh ? "Grant potential?" : "Reroll these lines?"),
      ThemedSeparator(),
  };
  if (item_ != nullptr) {
    for (ftxui::Element& row : LineRows()) {
      body.push_back(std::move(row));
    }
  }
  body.push_back(ThemedSeparator());
  body.push_back(RedUnless(CenteredRow(FormatMeso(Cost())), Affordable()));
  return DialogWindow(" " + CubeName(selected_cube()) + " ", std::move(body),
                      ConfirmButtons(confirm_.focus(), Affordable()));
}

ConfirmChoice CubePanel::OnEvent(ftxui::Event event) {
  if (!confirm_.open()) {
    if (IsForward(event)) {
      // A cube the purse cannot cover opens the question all the same, with
      // its Confirm greyed: it is the same window a player rerolls their way
      // into, and refusing to draw it would say the shelf had gone away.
      confirm_.Open(/*cancel_selected=*/!Affordable());
    }
    return ConfirmChoice::kPending;
  }
  ConfirmChoice choice = confirm_.OnEvent(std::move(event), Affordable());
  if (choice == ConfirmChoice::kConfirmed) {
    // The one Confirm in the game that leaves its window standing: what it
    // buys is another roll of the lines the window is showing.
    confirm_.Open(/*cancel_selected=*/false);
  }
  return choice;
}

}  // namespace ms
