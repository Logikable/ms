#include "src/frontend/panels/level_up_popup_panel.h"

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// The narrowest the card's content is allowed to be. A terminal cell is about
// twice as tall as it is wide, so a card that reads as square on screen is
// about twice as wide as it is high -- which this is, against the eight rows
// the card stands at.
//
// A minimum rather than a fixed padding, so the card does not breathe in and
// out as the level count grows a digit.
constexpr int kMinContentWidth = 15;

// One "+N LABEL" line, or nothing at all when the level paid none of it.
// Returns nullptr for the caller to drop rather than an empty row, which would
// leave a blank line where the reason for it is invisible.
ftxui::Element GainRow(int amount, const std::string& label) {
  if (amount <= 0) {
    return nullptr;
  }
  return CenteredRow("+" + std::to_string(amount) + " " + label);
}

}  // namespace

ftxui::Element LevelUpPopupPanel(int from_level, int to_level, int ap, int sp) {
  std::vector<ftxui::Element> rows;
  // A blank row at each end. This card is the one thing on screen asking to be
  // noticed from across a room, and room around what it says is most of what
  // makes it carry.
  rows.push_back(ftxui::text(""));
  // The arrow rather than the new level alone: a player who was not watching
  // wants to know how far they came, and after an idle stretch that can be
  // more than one level.
  rows.push_back(CenteredRow(std::to_string(from_level) + "  →  " +
                             std::to_string(to_level)));
  rows.push_back(AccentSeparator(kYellow));
  // AP above SP, in the order the character panel spends them.
  ftxui::Element ap_row = GainRow(ap, "AP");
  if (ap_row != nullptr) {
    rows.push_back(std::move(ap_row));
  }
  ftxui::Element sp_row = GainRow(sp, "SP");
  if (sp_row != nullptr) {
    rows.push_back(std::move(sp_row));
  }
  rows.push_back(ftxui::text(""));
  return AccentWindow(
      " Level Up ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, kMinContentWidth),
      kYellow);
}

}  // namespace ms
