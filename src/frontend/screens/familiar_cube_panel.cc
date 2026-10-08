#include "src/frontend/screens/familiar_cube_panel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/familiar.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/reroll_prompt.h"
#include "src/frontend/widgets/text_columns.h"

namespace ms {
namespace {

// The item shelf's height, so the two cubing screens are the same size.
constexpr int kShelfRows = 6;

// The widest line name and value any rank can roll, so a reroll never moves
// the columns.
struct LineWidths {
  int name = 0;
  int value = 0;
};

LineWidths MeasureLines() {
  LineWidths widths;
  for (int r = POTENTIAL_RANK_RARE; r <= POTENTIAL_RANK_LEGENDARY; ++r) {
    const PotentialRank rank = static_cast<PotentialRank>(r);
    for (FamiliarLineType type : FamiliarPool(rank)) {
      FamiliarLine line;
      line.set_type(type);
      line.set_rank(rank);
      widths.name =
          std::max(widths.name, TextColumns(FamiliarLineName(line.type())));
      widths.value =
          std::max(widths.value, TextColumns(FamiliarLineValueText(line)));
    }
  }
  return widths;
}

}  // namespace

void FamiliarCubePanel::SetFamiliar(const Familiar* familiar, int64_t meso) {
  familiar_ = familiar;
  meso_ = meso;
  // The reroll that emptied the purse is also what moves the cursor.
  if (prompt_.open() && !Affordable()) {
    prompt_.FocusCancel();
  }
}

void FamiliarCubePanel::Reset() {
  prompt_.Close();
}

bool FamiliarCubePanel::Affordable() const {
  return meso_ >= kFamiliarCubeMeso;
}

ftxui::Element FamiliarCubePanel::Render(bool focused) const {
  const std::string price = FormatMeso(kFamiliarCubeMeso);
  const int name = TextColumns(kFamiliarCubeName);
  const int cost = TextColumns(price);
  std::vector<ftxui::Element> rows = {
      ftxui::text("  " + PadRight("Name", name) + "  " + PadLeft("Cost", cost) +
                  " "),
      ThemedSeparator(),
  };
  ftxui::Element label =
      ftxui::text(std::string("> ") + kFamiliarCubeName + "  ");
  if (!Affordable()) {
    label = std::move(label) | ftxui::dim;
  }
  rows.push_back(
      HighlightRow(ftxui::hbox({std::move(label),
                                RedUnless(ftxui::text(price), Affordable()),
                                ftxui::text(" ")}),
                   /*selected=*/true));
  for (int i = 1; i < kShelfRows; ++i) {
    rows.push_back(ftxui::text(""));
  }
  return ThemedWindow(" Cube Selection ", ftxui::vbox(std::move(rows)),
                      focused);
}

ftxui::Element FamiliarCubePanel::RenderConfirm() const {
  std::vector<ftxui::Element> body = {
      CenteredRow("Reroll these lines?"),
      ThemedSeparator(),
  };
  if (familiar_ != nullptr) {
    const LineWidths widths = MeasureLines();
    // The rank dot the card shows on each line, so the lines look the same in
    // the window and on the card behind it.
    for (const FamiliarLine& line : familiar_->lines()) {
      body.push_back(CenteredRow(ftxui::hbox({
          ftxui::text("◼ ") | ftxui::color(RarityColor(line.rank())),
          ftxui::text(PadRight(FamiliarLineName(line.type()), widths.name) +
                      "  " +
                      PadLeft(FamiliarLineValueText(line), widths.value)),
      })));
    }
  }
  body.push_back(ThemedSeparator());
  // The purse above the price: the window is the only thing on screen saying
  // what a reroll leaves the player with.
  body.push_back(PriceBlock(meso_, kFamiliarCubeMeso, Affordable()));
  return DialogWindow(std::string(" ") + kFamiliarCubeName + " ",
                      std::move(body),
                      ConfirmButtons(prompt_.confirm_focus(), Affordable()));
}

RerollAction FamiliarCubePanel::OnEvent(ftxui::Event event) {
  return prompt_.OnEvent(std::move(event), Affordable(), /*has_after=*/false);
}

}  // namespace ms
