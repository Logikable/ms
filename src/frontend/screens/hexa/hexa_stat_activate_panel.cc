#include "src/frontend/screens/hexa/hexa_stat_activate_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/matrix/hexa_stat.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/text/colors.h"
#include "src/frontend/widgets/text/game_names.h"
#include "src/frontend/widgets/text/keys.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

constexpr char kUnselected[] = "Unselected";
// " M: ", then the name.
constexpr int kNameColumn = 4;
constexpr int kStats = HexaStat_MAX;

HexaStat StatAt(int entry) {
  return static_cast<HexaStat>(entry + 1);
}

}  // namespace

// The names wait for the job, which OpenDropdown knows.
HexaStatActivatePanel::HexaStatActivatePanel()
    : dropdown_(std::vector<std::string>(kStats)) {
}

void HexaStatActivatePanel::Open(const HexaStats& stats, StatField main_stat) {
  stats_ = stats;
  main_stat_ = main_stat;
  draft_ = HexaStatNode();
  row_ = 0;
  cancel_focused_ = false;
  dropdown_open_ = false;
}

bool HexaStatActivatePanel::Complete() const {
  return draft_.primary() != HEXA_STAT_UNSPECIFIED &&
         draft_.first() != HEXA_STAT_UNSPECIFIED &&
         draft_.second() != HEXA_STAT_UNSPECIFIED;
}

HexaStatSlot HexaStatActivatePanel::SlotAt(int row) const {
  return row == 0   ? HexaStatSlot::kPrimary
         : row == 1 ? HexaStatSlot::kFirst
                    : HexaStatSlot::kSecond;
}

HexaStat HexaStatActivatePanel::PickAt(int row) const {
  return row == 0   ? draft_.primary()
         : row == 1 ? draft_.first()
                    : draft_.second();
}

void HexaStatActivatePanel::SetPick(int row, HexaStat stat) {
  if (row == 0) {
    draft_.set_primary(stat);
  } else if (row == 1) {
    draft_.set_first(stat);
  } else {
    draft_.set_second(stat);
  }
}

void HexaStatActivatePanel::OpenDropdown() {
  std::vector<std::string> names;
  for (int i = 0; i < kStats; ++i) {
    names.push_back(HexaStatName(StatAt(i), main_stat_));
  }
  dropdown_ = ItemMenu(std::move(names));
  for (int i = 0; i < kStats; ++i) {
    if (!HexaStatPickAllowed(stats_, draft_, SlotAt(row_), StatAt(i))) {
      dropdown_.Disable(i);
    }
  }
  // On the row's own pick, so reopening a row doesn't lose the place.
  dropdown_.Select(PickAt(row_) == HEXA_STAT_UNSPECIFIED ? 0
                                                         : PickAt(row_) - 1);
  dropdown_open_ = true;
}

HexaStatActivatePanel::Result HexaStatActivatePanel::OnDropdownEvent(
    const ftxui::Event& event) {
  if (event == ftxui::Event::ArrowUp) {
    dropdown_.Up();
  } else if (event == ftxui::Event::ArrowDown) {
    dropdown_.Down();
  } else if (IsBack(event)) {
    dropdown_open_ = false;
  } else if (IsForward(event)) {
    const HexaStat stat = StatAt(dropdown_.selected());
    if (HexaStatPickAllowed(stats_, draft_, SlotAt(row_), stat)) {
      SetPick(row_, stat);
    }
    dropdown_open_ = false;
  }
  return Result::kPending;
}

HexaStatActivatePanel::Result HexaStatActivatePanel::OnEvent(
    const ftxui::Event& event) {
  if (dropdown_open_) {
    return OnDropdownEvent(event);
  }
  if (IsBack(event)) {
    return Result::kCancel;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    row_ = StepCursor(row_, event == ftxui::Event::ArrowUp ? -1 : 1,
                      kButtonsRow + 1);
    return Result::kPending;
  }
  if (row_ == kButtonsRow &&
      (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight)) {
    cancel_focused_ = event == ftxui::Event::ArrowRight;
    return Result::kPending;
  }
  if (!IsForward(event)) {
    return Result::kPending;
  }
  if (row_ < kButtonsRow) {
    OpenDropdown();
    return Result::kPending;
  }
  if (cancel_focused_) {
    return Result::kCancel;
  }
  return Complete() ? Result::kActivate : Result::kPending;
}

ftxui::Element HexaStatActivatePanel::RenderRow(int row) const {
  const bool primary = row == 0;
  const HexaStat pick = PickAt(row);
  ftxui::Element name = ftxui::text(pick == HEXA_STAT_UNSPECIFIED
                                        ? kUnselected
                                        : HexaStatName(pick, main_stat_));
  if (row_ == row) {
    name = std::move(name) | ftxui::inverted;
  } else if (pick == HEXA_STAT_UNSPECIFIED) {
    name = std::move(name) | ftxui::dim;
  }
  ftxui::Element line = ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(primary ? "M: " : "A: ") |
          ftxui::color(primary ? kGold : kGreen),
      std::move(name),
      ftxui::filler(),
  });
  if (row_ == row) {
    line = std::move(line) | ftxui::reflect(row_box_);
  }
  return line;
}

ftxui::Element HexaStatActivatePanel::Render() const {
  std::vector<ftxui::Element> body = {
      CenteredRow("Pick Stats"),
      ThemedSeparator(),
  };
  for (int row = 0; row < kButtonsRow; ++row) {
    body.push_back(RenderRow(row));
  }
  const bool on_buttons = row_ == kButtonsRow;
  return DialogWindow(
      " Activate HEXA Stat ", std::move(body),
      ButtonRow("Activate", "Cancel", on_buttons && !cancel_focused_,
                on_buttons && cancel_focused_, Complete()));
}

ftxui::Element HexaStatActivatePanel::RenderDropdown() const {
  if (!dropdown_open_) {
    return ftxui::text("");
  }
  // Past the widest name, so the dropdown covers the blank after it and the
  // row's tag stays in view. One row up, so the cursor's entry sits beside it.
  int widest = static_cast<int>(std::string(kUnselected).size());
  for (int i = 0; i < kStats; ++i) {
    widest = std::max(
        widest, static_cast<int>(HexaStatName(StatAt(i), main_stat_).size()));
  }
  return dropdown_.Render(std::max(0, row_box_.y_min - 1),
                          row_box_.x_min + kNameColumn + widest + 1);
}

}  // namespace ms
