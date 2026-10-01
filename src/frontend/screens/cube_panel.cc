#include "src/frontend/screens/cube_panel.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
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
#include "src/frontend/widgets/text_columns.h"
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

// Measured over every cube, shown or not, so unlocking one doesn't shift the
// columns.
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
                        bool selected, bool affordable, bool gold) {
  ftxui::Element label = ftxui::text(
      (selected ? "> " : "  ") + PadRight(CubeName(cube.type), widths.name) +
      "  " + PadRight(CubeTrackName(cube.track), widths.type) + "  ");
  if (gold) {
    label = std::move(label) | ftxui::color(kGold);
  } else if (!affordable) {
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

// The widest line name and value the item can show on `track`: every line its
// pool rolls at any rank, and the lines it already has. Measured over what
// could roll rather than what did, so a reroll never moves the columns.
struct LineWidths {
  int name = 0;
  int value = 0;
};

void MeasureLine(const PotentialLine& line, int level, LineWidths& widths) {
  widths.name =
      std::max<int>(widths.name, TextColumns(PotentialLineName(line.type())));
  widths.value = std::max<int>(
      widths.value, TextColumns(PotentialLineValueText(line, level)));
}

LineWidths MeasureLines(const EquipInstance& item, PotentialTrack track,
                        std::initializer_list<const Potential*> shown) {
  const int level = item.prototype().required_level();
  const PotentialGroup group = PotentialGroupOf(item.prototype().equip_slot());
  LineWidths widths;
  for (int r = POTENTIAL_RANK_RARE; r <= POTENTIAL_RANK_LEGENDARY; ++r) {
    const PotentialRank rank = static_cast<PotentialRank>(r);
    for (PotentialLineType type : PotentialPool(track, group, rank)) {
      PotentialLine line;
      line.set_type(type);
      line.set_rank(rank);
      MeasureLine(line, level, widths);
    }
  }
  for (const Potential* potential : shown) {
    for (const PotentialLine& line : potential->lines()) {
      MeasureLine(line, level, widths);
    }
  }
  return widths;
}

// The three lines of `potential`, or three placeholders for none yet, so the
// window is the same size before the first cube as after.
void AppendLineRows(std::vector<ftxui::Element>& rows,
                    const Potential& potential, int level,
                    const LineWidths& widths) {
  if (potential.rank() == POTENTIAL_RANK_UNSPECIFIED) {
    // One node per row: ftxui draws a shared node only in the last box.
    for (int i = 0; i < kPotentialLines; ++i) {
      rows.push_back(CenteredRow(ftxui::text("—") | ftxui::dim));
    }
    return;
  }
  for (const PotentialLine& line : potential.lines()) {
    // The rank dot the inspect card shows on each line, so the same three lines
    // look the same in the window and on the card behind it.
    rows.push_back(CenteredRow(ftxui::hbox({
        ftxui::text("◼ ") | ftxui::color(RarityColor(line.rank())),
        ftxui::text(PadRight(PotentialLineName(line.type()), widths.name) +
                    "  " +
                    PadLeft(PotentialLineValueText(line, level), widths.value)),
    })));
  }
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

void CubePanel::SetShelf(std::vector<ShelfEntry> shelf) {
  shelf_ = std::move(shelf);
}

const Potential& CubePanel::SelectedPotential() const {
  return PotentialOf(item_->equip_state(), CubeOf(selected_cube()).track);
}

bool CubePanel::Choosing() const {
  return CubeOf(selected_cube()).choose;
}

void CubePanel::Reset() {
  selected_ = 0;
  rank_up_ = false;
  after_.reset();
  keep_focus_ = KeepFocus::kNone;
  confirm_.Close();
}

void CubePanel::RaiseRankUp() {
  rank_up_ = true;
  rank_up_seconds_ = kRankUpSeconds;
  rank_up_touched_ = false;
}

void CubePanel::AdvanceRankUp(double elapsed_seconds) {
  rank_up_seconds_ = std::max(0.0, rank_up_seconds_ - elapsed_seconds);
}

void CubePanel::TouchRankUp() {
  rank_up_touched_ = true;
}

bool CubePanel::rank_up() const {
  return rank_up_ && (rank_up_seconds_ > 0.0 || !rank_up_touched_);
}

CubeType CubePanel::selected_cube() const {
  return shelf_[std::min<int>(selected_, shelf_.size() - 1)].type;
}

void CubePanel::SetAfter(Potential after) {
  after_ = std::move(after);
}

Potential CubePanel::TakeAfter() {
  Potential after = after_.value_or(Potential());
  after_.reset();
  return after;
}

int64_t CubePanel::Cost() const {
  return CubeOf(selected_cube()).cost;
}

bool CubePanel::Affordable() const {
  return meso_ >= Cost();
}

void CubePanel::MoveCursor(int delta) {
  if (confirm_.open()) {
    return;
  }
  selected_ = StepCursor(selected_, delta, shelf_.size());
}

ftxui::Element CubePanel::Render(bool focused) const {
  const ShelfWidths widths = Widths();
  std::vector<ftxui::Element> rows = {
      ftxui::text("  " + PadRight("Name", widths.name) + "  " +
                  PadRight("Type", widths.type) + "  " +
                  PadLeft("Cost", widths.cost) + " "),
      ThemedSeparator(),
  };
  for (int i = 0; i < static_cast<int>(shelf_.size()); ++i) {
    const Cube& cube = CubeOf(shelf_[i].type);
    rows.push_back(ShelfRow(cube, widths, i == selected_, meso_ >= cube.cost,
                            shelf_[i].lead));
  }
  // The shelf keeps its full height with or without cubes to fill it.
  for (int i = shelf_.size(); i < kShelfRows; ++i) {
    rows.push_back(ftxui::text(""));
  }
  return ThemedWindow(" Cube Selection ", ftxui::vbox(std::move(rows)),
                      focused);
}

std::string CubePanel::Prompt() const {
  const bool fresh = item_ == nullptr ||
                     SelectedPotential().rank() == POTENTIAL_RANK_UNSPECIFIED;
  if (!fresh) {
    return "Reroll these lines?";
  }
  return CubeOf(selected_cube()).track == PotentialTrack::kBonus
             ? "Grant bonus potential?"
             : "Grant potential?";
}

ftxui::Element CubePanel::KeepButtons() const {
  // Spaced so each button starts in the column of the one below it: Confirm is
  // a column wider than Keep.
  ftxui::Element row = ftxui::hbox({
      ftxui::text(" "),
      ActionButton("Keep ↑", keep_focus_ == KeepFocus::kBefore),
      ftxui::text("    "),
      ActionButton("Keep ↓", keep_focus_ == KeepFocus::kAfter),
      ftxui::text(" "),
  });
  return after_.has_value() ? row : std::move(row) | ftxui::dim;
}

ftxui::Element CubePanel::RenderChoice(ftxui::Color accent) const {
  const int level = item_->prototype().required_level();
  const Potential empty;
  const Potential& before = SelectedPotential();
  const Potential& after = after_.has_value() ? *after_ : empty;
  const LineWidths widths =
      MeasureLines(*item_, CubeOf(selected_cube()).track, {&before, &after});
  std::vector<ftxui::Element> body = {
      CenteredRow(Prompt()),
      TitledSeparator(" Before ", accent),
  };
  AppendLineRows(body, before, level, widths);
  body.push_back(AccentSeparator(accent));
  body.push_back(CenteredRow(KeepButtons()));
  body.push_back(TitledSeparator(" After ", accent));
  AppendLineRows(body, after, level, widths);
  body.push_back(AccentSeparator(accent));
  body.push_back(PriceBlock(meso_, Cost(), Affordable()));
  const ConfirmFocus focus =
      keep_focus_ == KeepFocus::kNone ? confirm_.focus() : ConfirmFocus::kNone;
  return DialogWindow(" " + CubeName(selected_cube()) + " ", std::move(body),
                      ConfirmButtons(focus, Affordable()), accent);
}

ftxui::Element CubePanel::RenderConfirm() const {
  // Gold on a rank up, steel blue otherwise. The body's own rules use it too:
  // an AccentWindow draws its content white, so a themed rule inside a gold
  // window would look like a seam.
  const ftxui::Color accent = PanelAccent(rank_up());
  if (item_ != nullptr && Choosing()) {
    return RenderChoice(accent);
  }
  std::vector<ftxui::Element> body = {
      CenteredRow(Prompt()),
      AccentSeparator(accent),
  };
  if (item_ != nullptr) {
    const Potential& potential = SelectedPotential();
    const int level = item_->prototype().required_level();
    AppendLineRows(
        body, potential, level,
        MeasureLines(*item_, CubeOf(selected_cube()).track, {&potential}));
  }
  body.push_back(AccentSeparator(accent));
  // The purse above the price: the window is the only thing on screen saying
  // what a reroll leaves the player with, and it explains the grey Confirm
  // below.
  body.push_back(PriceBlock(meso_, Cost(), Affordable()));
  return DialogWindow(" " + CubeName(selected_cube()) + " ", std::move(body),
                      ConfirmButtons(confirm_.focus(), Affordable()), accent);
}

void CubePanel::FocusConfirmRow(bool cancel) {
  keep_focus_ = KeepFocus::kNone;
  confirm_.Open(/*cancel_selected=*/cancel || !Affordable());
}

CubeAction CubePanel::OnKeepEvent(const ftxui::Event& event) {
  if (event == ftxui::Event::ArrowLeft) {
    keep_focus_ = KeepFocus::kBefore;
  } else if (event == ftxui::Event::ArrowRight) {
    keep_focus_ = KeepFocus::kAfter;
  } else if (event == ftxui::Event::ArrowDown) {
    FocusConfirmRow(/*cancel=*/keep_focus_ == KeepFocus::kAfter);
  } else if (IsForward(event)) {
    const bool keep_after = keep_focus_ == KeepFocus::kAfter;
    // Back to Confirm, whichever was kept: the next thing a player does after
    // keeping is roll again.
    FocusConfirmRow(/*cancel=*/false);
    if (keep_after) {
      return CubeAction::kKeepAfter;
    }
    after_.reset();
    return CubeAction::kKeepBefore;
  }
  return CubeAction::kNone;
}

CubeAction CubePanel::OnEvent(ftxui::Event event) {
  if (!confirm_.open()) {
    if (IsForward(event)) {
      // A cube the purse can't cover still opens the question, with Confirm
      // greyed. It is the same window a player rerolls their way into, and not
      // showing it would look like the shelf had gone away.
      FocusConfirmRow(/*cancel=*/false);
    }
    return CubeAction::kNone;
  }
  if (IsBack(event)) {
    after_.reset();
    keep_focus_ = KeepFocus::kNone;
    confirm_.Close();
    return CubeAction::kClosed;
  }
  if (keep_focus_ != KeepFocus::kNone) {
    return OnKeepEvent(event);
  }
  if (event == ftxui::Event::ArrowUp) {
    if (after_.has_value()) {
      keep_focus_ = confirm_.focus() == ConfirmFocus::kCancel
                        ? KeepFocus::kAfter
                        : KeepFocus::kBefore;
    }
    return CubeAction::kNone;
  }
  switch (confirm_.OnEvent(std::move(event), Affordable())) {
    case ConfirmChoice::kPending:
      return CubeAction::kNone;
    case ConfirmChoice::kConfirmed:
      // The window stays open: Confirm buys another roll of the lines it shows.
      confirm_.Open(/*cancel_selected=*/false);
      return CubeAction::kReroll;
    case ConfirmChoice::kCancelled:
      after_.reset();
      return CubeAction::kClosed;
  }
  return CubeAction::kNone;
}

}  // namespace ms
