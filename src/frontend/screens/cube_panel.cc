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
// panel is the same height now with one cube as it will be with more, and the
// card beside it never moves.
constexpr int kShelfRows = 6;

// The three columns, each as wide as its widest content.
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

// One row of the shelf, cursor included. The cost is its own cell so an
// unaffordable price can be red while the rest of the row is grey: the reason
// and what it blocks, drawn separately.
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
  // The margin for the window's own border is requested here, because the rows
  // are laid out cell by cell and nothing else can leave it.
  return HighlightRow(
      ftxui::hbox({std::move(label), std::move(cost), ftxui::text(" ")}),
      selected);
}

}  // namespace

void CubePanel::SetItem(const EquipInstance* item, int64_t meso) {
  item_ = item;
  meso_ = meso;
  // The reroll that emptied the purse is also what moves the cursor. Checked
  // here rather than where the cube is bought, so the window is right however
  // the purse became short.
  if (confirm_.open() && !Affordable()) {
    confirm_.FocusCancel();
  }
}

void CubePanel::Reset() {
  selected_ = 0;
  rank_up_ = false;
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
  // The shelf keeps its full height with or without cubes to fill it.
  for (int i = std::size(kCubes); i < kShelfRows; ++i) {
    rows.push_back(ftxui::text(""));
  }
  return ThemedWindow(" Cube Selection ", ftxui::vbox(std::move(rows)),
                      focused);
}

std::vector<ftxui::Element> CubePanel::LineRows() const {
  const Potential& potential = item_->potential();
  if (potential.rank() == POTENTIAL_RANK_UNSPECIFIED) {
    // An item with no potential has no lines to show, and the rows stay empty
    // instead of being removed, so the window is the same size before the first
    // cube as after.
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
    // The rank dot the inspect card shows on each line, so the same three lines
    // look the same in the window and on the card behind it.
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
  // Gold on a rank up, steel blue otherwise. The body's own rules use it too:
  // an AccentWindow draws its content white, so a themed rule inside a gold
  // window would look like a seam.
  const ftxui::Color accent = PanelAccent(rank_up_);
  std::vector<ftxui::Element> body = {
      CenteredRow(fresh ? "Grant potential?" : "Reroll these lines?"),
      AccentSeparator(accent),
  };
  if (item_ != nullptr) {
    for (ftxui::Element& row : LineRows()) {
      body.push_back(std::move(row));
    }
  }
  body.push_back(AccentSeparator(accent));
  // The purse above the price: the window is the only thing on screen saying
  // what a reroll leaves the player with, and it explains the grey Confirm
  // below.
  body.push_back(PriceBlock(meso_, Cost(), Affordable()));
  return DialogWindow(" " + CubeName(selected_cube()) + " ", std::move(body),
                      ConfirmButtons(confirm_.focus(), Affordable()), accent);
}

ConfirmChoice CubePanel::OnEvent(ftxui::Event event) {
  if (!confirm_.open()) {
    if (IsForward(event)) {
      // A cube the purse can't cover still opens the question, with Confirm
      // greyed. It is the same window a player rerolls their way into, and not
      // showing it would look like the shelf had gone away.
      confirm_.Open(/*cancel_selected=*/!Affordable());
    }
    return ConfirmChoice::kPending;
  }
  ConfirmChoice choice = confirm_.OnEvent(std::move(event), Affordable());
  if (choice == ConfirmChoice::kConfirmed) {
    // The window stays open: Confirm buys another roll of the lines it shows.
    confirm_.Open(/*cancel_selected=*/false);
  }
  return choice;
}

}  // namespace ms
