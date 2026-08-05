#include "src/frontend/panels/level_up_popup_panel.h"

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// How many rows stand below the rule. Fixed rather than counted off what the
// level paid, so the card is the same size whether or not there is SP to
// report: it is caught out of the corner of an eye, and a box that changed
// shape between one level and the next would read as two different things.
constexpr int kBodyRows = 3;

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
  // AP above SP, in the order the character panel spends them.
  std::vector<ftxui::Element> gains;
  ftxui::Element ap_row = GainRow(ap, "AP");
  if (ap_row != nullptr) {
    gains.push_back(std::move(ap_row));
  }
  ftxui::Element sp_row = GainRow(sp, "SP");
  if (sp_row != nullptr) {
    gains.push_back(std::move(sp_row));
  }

  std::vector<ftxui::Element> rows;
  // The arrow rather than the new level alone: a player who was not watching
  // wants to know how far they came, and after an idle stretch that can be
  // more than one level.
  rows.push_back(CenteredRow(std::to_string(from_level) + "  →  " +
                             std::to_string(to_level)));
  rows.push_back(AccentSeparator(kYellow));
  // The gains sit in the middle of the body, with the odd row left over going
  // below them: a lone AP line lands dead centre, and a pair sits off the rule
  // rather than up against it.
  int above = (kBodyRows - static_cast<int>(gains.size())) / 2;
  for (int i = 0; i < above; ++i) {
    rows.push_back(ftxui::text(""));
  }
  for (ftxui::Element& gain : gains) {
    rows.push_back(std::move(gain));
  }
  // Whatever is left of the body, so the card stands the same height every
  // time -- two rows of it when a level paid nothing at all.
  while (static_cast<int>(rows.size()) < kBodyRows + 2) {
    rows.push_back(ftxui::text(""));
  }
  // The width is a floor rather than a fit: an ftxui window fills the box its
  // parent hands it, and this one is meant to be a card in the middle of the
  // screen, so Tui::RenderFrame centres it -- which shrinks it back to its
  // content. Left at that it would be the width of "12  →  13" and nothing
  // more, which is not enough of a card to catch anyone who is looking at a
  // different window.
  return AccentWindow(" Level Up ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                      kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
