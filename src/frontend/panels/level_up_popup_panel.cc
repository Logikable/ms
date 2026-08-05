#include "src/frontend/panels/level_up_popup_panel.h"

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

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
  // Nothing here pins a width, which is what leaves the banner free to take
  // the whole of whatever it is laid into: an ftxui window fills the box its
  // parent hands it, so the caller decides how wide this is. Reaching both
  // edges is what makes it a banner rather than a card -- peripheral vision
  // catches area, not detail, and a stripe across the terminal is area no
  // small box in the middle of it can match.
  //
  // So do not wrap this in hcenter or give it a fixed size. Tui::RenderFrame
  // holds it between two fillers, which centre it vertically and hand it the
  // full width.
  return AccentWindow(" Level Up ", ftxui::vbox(std::move(rows)), kYellow);
}

}  // namespace ms
