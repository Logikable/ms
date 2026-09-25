#include "src/frontend/cards/level_up_card.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"

namespace ms {
namespace {

// How many rows are below the divider. Fixed rather than based on what the
// level paid, so the card is the same size whether or not there is SP to show:
// it is seen out of the corner of an eye, and a box that changed shape between
// levels would look like two different things.
constexpr int kBodyRows = 3;

// One "+N LABEL" line, or nullptr when the level paid none of it. The caller
// drops a null rather than showing an empty row, which would leave an
// unexplained blank line.
ftxui::Element GainRow(int64_t amount, const std::string& label) {
  if (amount <= 0) {
    return nullptr;
  }
  return CenteredRow("+" + FormatWithCommas(amount) + " " + label);
}

}  // namespace

ftxui::Element LevelUpCard(int from_level, int to_level, int ap, int sp,
                           int hyper_sp, int64_t honor,
                           const std::vector<std::string>& unlocks) {
  // AP above SP, in the order the character panel spends them, then Hyper SP
  // below the SP it is separate from, then honor last, since it is the only
  // gain here not spent on the card's own screen.
  std::vector<ftxui::Element> body;
  const std::pair<int64_t, const char*> kGains[] = {
      {ap, "AP"}, {sp, "SP"}, {hyper_sp, "Hyper SP"}, {honor, "Honor"}};
  for (const std::pair<int64_t, const char*>& gain : kGains) {
    ftxui::Element row = GainRow(gain.first, gain.second);
    if (row != nullptr) {
      body.push_back(std::move(row));
    }
  }
  // Below the gains, in gold against the card's white: AP is the same news
  // every level, and this isn't.
  for (const std::string& unlock : unlocks) {
    body.push_back(CenteredRow("Unlocked " + unlock + "!") |
                   ftxui::color(kYellow));
  }

  std::vector<ftxui::Element> rows;
  // An arrow from the old level rather than just the new one: a player who
  // wasn't watching wants to know how far they came, and after idling that can
  // be more than one level.
  rows.push_back(CenteredRow(std::to_string(from_level) + "  →  " +
                             std::to_string(to_level)));
  rows.push_back(AccentSeparator(kYellow));
  // The body is centred, with the odd row left over going below: a lone AP line
  // is exactly centred, and a pair sits just under the divider rather than
  // against it. A climb that unlocked something can need more than three rows,
  // and then the card grows, since that news is worth breaking the shape for.
  int above = (kBodyRows - static_cast<int>(body.size())) / 2;
  for (int i = 0; i < above; ++i) {
    rows.push_back(ftxui::text(""));
  }
  for (ftxui::Element& row : body) {
    rows.push_back(std::move(row));
  }
  // Pads out the body so the card is always the same height, even when a level
  // paid nothing.
  while (static_cast<int>(rows.size()) < kBodyRows + 2) {
    rows.push_back(ftxui::text(""));
  }
  // A minimum width rather than fitting the content: Tui::RenderFrame centres
  // the card, which shrinks it to its content, and then it would only be as
  // wide as "12 → 13", too small to catch the eye.
  return AccentWindow(" Level Up ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                      kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
