#include "src/frontend/panels/level_up_popup_panel.h"

#include <cstdint>
#include <string>
#include <utility>
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
ftxui::Element GainRow(int64_t amount, const std::string& label) {
  if (amount <= 0) {
    return nullptr;
  }
  return CenteredRow("+" + FormatWithCommas(amount) + " " + label);
}

}  // namespace

ftxui::Element LevelUpPopupPanel(int from_level, int to_level, int ap, int sp,
                                 int hyper_sp, int64_t honor,
                                 const std::vector<std::string>& unlocks) {
  // AP above SP, in the order the character panel spends them, the Hyper SP
  // under the SP it is not a stage of, and the honor last: it is the one gain
  // here that is not spent on this card's own screen.
  std::vector<ftxui::Element> body;
  const std::pair<int64_t, const char*> kGains[] = {
      {ap, "AP"}, {sp, "SP"}, {hyper_sp, "Hyper SP"}, {honor, "Honor"}};
  for (const std::pair<int64_t, const char*>& gain : kGains) {
    ftxui::Element row = GainRow(gain.first, gain.second);
    if (row != nullptr) {
      body.push_back(std::move(row));
    }
  }
  // Below what the level paid, and gold against the card's white: a point of
  // AP is the same news every level, and this is not.
  for (const std::string& unlock : unlocks) {
    body.push_back(CenteredRow("Unlocked " + unlock + "!") |
                   ftxui::color(kYellow));
  }

  std::vector<ftxui::Element> rows;
  // The arrow rather than the new level alone: a player who was not watching
  // wants to know how far they came, and after an idle stretch that can be
  // more than one level.
  rows.push_back(CenteredRow(std::to_string(from_level) + "  →  " +
                             std::to_string(to_level)));
  rows.push_back(AccentSeparator(kYellow));
  // The body sits in the middle, with the odd row left over going below it: a
  // lone AP line lands dead centre, and a pair sits off the rule rather than
  // up against it. A climb that opened something can outgrow the three rows,
  // and then the card grows with it -- news worth breaking the shape for.
  int above = (kBodyRows - static_cast<int>(body.size())) / 2;
  for (int i = 0; i < above; ++i) {
    rows.push_back(ftxui::text(""));
  }
  for (ftxui::Element& row : body) {
    rows.push_back(std::move(row));
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
