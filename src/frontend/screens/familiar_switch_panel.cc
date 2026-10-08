#include "src/frontend/screens/familiar_switch_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/types/span.h"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/familiar.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/familiar_list.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

// Borders, header, two rules and [Close]: everything but the roster's rows.
constexpr int kChromeRows = 6;

}  // namespace

void FamiliarSwitchPanel::Reset(const std::string& current) {
  current_ = current;
  cursor_ = 0;
  const absl::Span<const FamiliarSpecies> roster = FamiliarRoster();
  for (int i = 0; i < static_cast<int>(roster.size()); ++i) {
    if (roster[i].name == current) {
      cursor_ = i;
    }
  }
  confirm_.Close();
}

void FamiliarSwitchPanel::SetFamiliars(const FamiliarBook* book,
                                       std::vector<std::string> in_use) {
  book_ = book;
  in_use_ = std::move(in_use);
}

int FamiliarSwitchPanel::Stops() const {
  return static_cast<int>(FamiliarRoster().size()) + 1;
}

bool FamiliarSwitchPanel::OnClose() const {
  return cursor_ == Stops() - 1;
}

std::string FamiliarSwitchPanel::selected() const {
  if (OnClose()) {
    return "";
  }
  return FamiliarRoster()[cursor_].name;
}

ftxui::Element FamiliarSwitchPanel::Render(bool focused) const {
  const FamiliarBook empty;
  const FamiliarBook& book = book_ != nullptr ? *book_ : empty;
  const FamiliarColumns columns =
      FitFamiliarColumns(kFamiliarSwitchWidth - 2, /*in_use=*/true);
  std::vector<ftxui::Element> rows;
  const absl::Span<const FamiliarSpecies> roster = FamiliarRoster();
  for (int i = 0; i < static_cast<int>(roster.size()); ++i) {
    const std::string species = roster[i].name;
    const bool on_cursor = i == cursor_;
    const bool in_use = absl::c_linear_search(in_use_, species);
    ftxui::Element row =
        FamiliarRowElement(on_cursor ? "> " : "  ",
                           FamiliarRowCells(book, species, columns, in_use));
    if (species == current_) {
      row = std::move(row) | ftxui::dim;
    }
    row = HighlightRow(std::move(row), on_cursor);
    rows.push_back(i == cursor_ ? std::move(row) | ftxui::focus
                                : std::move(row));
  }
  ftxui::Element list =
      ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator | ftxui::yframe;
  if (max_rows_ > 0) {
    list = std::move(list) | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN,
                                         std::max(1, max_rows_ - kChromeRows));
  }
  return ThemedWindow(" Switch Familiars ",
                      ftxui::vbox({
                          ftxui::text(FamiliarHeader(columns)),
                          ThemedSeparator(),
                          std::move(list) | ftxui::flex_shrink,
                          ThemedSeparator(),
                          CenteredRow(ActionButton("Close", OnClose())),
                      }),
                      focused) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kFamiliarSwitchWidth);
}

ftxui::Element FamiliarSwitchPanel::RenderConfirm() const {
  const std::string species = selected();
  const std::string name = book_ != nullptr && !species.empty()
                               ? FamiliarDisplayName(*book_, species)
                               : species;
  return DialogWindow(" Switch ", {CenteredRow("Equip " + name + "?")},
                      ConfirmButtons(confirm_.focus()));
}

FamiliarSwitchPanel::Action FamiliarSwitchPanel::OnEvent(
    const ftxui::Event& event) {
  if (confirm_.open()) {
    return confirm_.OnEvent(event) == ConfirmChoice::kConfirmed ? Action::kEquip
                                                                : Action::kNone;
  }
  if (IsBack(event)) {
    return Action::kClose;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    cursor_ =
        StepCursor(cursor_, event == ftxui::Event::ArrowUp ? -1 : 1, Stops());
    return Action::kNone;
  }
  if (IsForward(event)) {
    if (OnClose()) {
      return Action::kClose;
    }
    if (selected() != current_) {
      confirm_.Open();
    }
  }
  return Action::kNone;
}

}  // namespace ms
