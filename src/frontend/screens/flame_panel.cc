#include "src/frontend/screens/flame_panel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/reroll_prompt.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The two columns, each as wide as its widest content.
struct ShelfWidths {
  int name = 4;  // "Name"
  int cost = 4;  // "Cost"
};

ShelfWidths Widths() {
  ShelfWidths widths;
  for (const Flame& flame : kFlames) {
    widths.name = std::max<int>(widths.name, FlameName(flame.type).size());
    widths.cost = std::max<int>(widths.cost, FormatMeso(flame.cost).size());
  }
  return widths;
}

// One row of the shelf, cursor included. The cost is its own cell so an
// unaffordable price can be red while the rest of the row is grey.
ftxui::Element ShelfRow(const Flame& flame, const ShelfWidths& widths,
                        bool selected, bool affordable) {
  ftxui::Element label =
      ftxui::text((selected ? "> " : "  ") +
                  PadRight(FlameName(flame.type), widths.name) + "  ");
  if (!affordable) {
    label = std::move(label) | ftxui::dim;
  }
  ftxui::Element cost = RedUnless(
      ftxui::text(PadLeft(FormatMeso(flame.cost), widths.cost)), affordable);
  return HighlightRow(
      ftxui::hbox({std::move(label), std::move(cost), ftxui::text(" ")}),
      selected);
}

struct LineWidths {
  int name = 0;
  int value = 0;
};

void MeasureLine(const FlameLine& line, const EquipPrototype& proto,
                 LineWidths& widths) {
  widths.name =
      std::max<int>(widths.name, TextColumns(FlameStatName(line.stat())));
  widths.value =
      std::max<int>(widths.value, TextColumns(FlameLineValueText(line, proto)));
}

// Measured over every line the item's pool rolls at the highest tier any flame
// reaches, where each is widest, so a reroll never moves the columns.
LineWidths MeasureLines(const EquipPrototype& proto) {
  int top = 0;
  for (const Flame& flame : kFlames) {
    top = std::max(top, flame.min_tier + kFlameTiers - 1);
  }
  LineWidths widths;
  for (FlameStat stat : FlamePool(proto)) {
    FlameLine line;
    line.set_stat(stat);
    line.set_tier(top);
    MeasureLine(line, proto, widths);
  }
  return widths;
}

// The four lines of `lines`, or four placeholders for none yet, so the window
// is the same size before the first flame as after.
void AppendLineRows(std::vector<ftxui::Element>& rows, const FlameLines& lines,
                    const EquipPrototype& proto, const LineWidths& widths) {
  if (lines.empty()) {
    // One node per row: ftxui draws a shared node only in the last box.
    for (int i = 0; i < kFlameLines; ++i) {
      rows.push_back(CenteredRow(ftxui::text("—") | ftxui::dim));
    }
    return;
  }
  for (const FlameLine& line : lines) {
    rows.push_back(CenteredRow(ftxui::hbox({
        ftxui::text(PadRight(FlameStatName(line.stat()), widths.name) + "  "),
        ftxui::text(PadLeft(FlameLineValueText(line, proto), widths.value)) |
            ftxui::color(kTeal),
        ftxui::text("  T" + std::to_string(line.tier())) | ftxui::dim,
    })));
  }
}

}  // namespace

void FlamePanel::SetItem(const EquipInstance* item, int64_t meso) {
  item_ = item;
  meso_ = meso;
  if (prompt_.open() && !Affordable()) {
    prompt_.FocusCancel();
  }
}

void FlamePanel::Reset() {
  selected_ = 0;
  after_.reset();
  prompt_.Close();
}

FlameType FlamePanel::selected_flame() const {
  return kFlames[selected_].type;
}

void FlamePanel::SetAfter(FlameLines after) {
  after_ = std::move(after);
}

FlameLines FlamePanel::TakeAfter() {
  FlameLines after = after_.value_or(FlameLines());
  after_.reset();
  return after;
}

const FlameLines& FlamePanel::ItemLines() const {
  return item_->equip_state().flame();
}

int64_t FlamePanel::Cost() const {
  return FlameOf(selected_flame()).cost;
}

bool FlamePanel::Affordable() const {
  return meso_ >= Cost();
}

void FlamePanel::MoveCursor(int delta) {
  if (prompt_.open()) {
    return;
  }
  selected_ = StepCursor(selected_, delta, std::size(kFlames));
}

ftxui::Element FlamePanel::Render(bool focused) const {
  const ShelfWidths widths = Widths();
  std::vector<ftxui::Element> rows = {
      ftxui::text("  " + PadRight("Name", widths.name) + "  " +
                  PadLeft("Cost", widths.cost) + " "),
      ThemedSeparator(),
  };
  for (int i = 0; i < static_cast<int>(std::size(kFlames)); ++i) {
    rows.push_back(
        ShelfRow(kFlames[i], widths, i == selected_, meso_ >= kFlames[i].cost));
  }
  return ThemedWindow(" Flame Selection ", ftxui::vbox(std::move(rows)),
                      focused);
}

std::string FlamePanel::Prompt() const {
  return item_ == nullptr || ItemLines().empty() ? "Grant a flame?"
                                                 : "Reroll these lines?";
}

ftxui::Element FlamePanel::RenderChoice() const {
  const EquipPrototype& proto = item_->prototype();
  const FlameLines empty;
  const LineWidths widths = MeasureLines(proto);
  std::vector<ftxui::Element> body = {
      CenteredRow(Prompt()),
      TitledSeparator(" Before ", kTheme),
  };
  AppendLineRows(body, ItemLines(), proto, widths);
  body.push_back(ThemedSeparator());
  body.push_back(CenteredRow(prompt_.KeepButtons(after_.has_value())));
  body.push_back(TitledSeparator(" After ", kTheme));
  AppendLineRows(body, after_.has_value() ? *after_ : empty, proto, widths);
  body.push_back(ThemedSeparator());
  body.push_back(PriceBlock(meso_, Cost(), Affordable()));
  return DialogWindow(" " + FlameName(selected_flame()) + " ", std::move(body),
                      ConfirmButtons(prompt_.confirm_focus(), Affordable()));
}

ftxui::Element FlamePanel::RenderConfirm() const {
  if (item_ != nullptr && FlameOf(selected_flame()).choose) {
    return RenderChoice();
  }
  std::vector<ftxui::Element> body = {
      CenteredRow(Prompt()),
      ThemedSeparator(),
  };
  if (item_ != nullptr) {
    AppendLineRows(body, ItemLines(), item_->prototype(),
                   MeasureLines(item_->prototype()));
  }
  body.push_back(ThemedSeparator());
  body.push_back(PriceBlock(meso_, Cost(), Affordable()));
  return DialogWindow(" " + FlameName(selected_flame()) + " ", std::move(body),
                      ConfirmButtons(prompt_.confirm_focus(), Affordable()));
}

RerollAction FlamePanel::OnEvent(ftxui::Event event) {
  const RerollAction action =
      prompt_.OnEvent(std::move(event), Affordable(), after_.has_value());
  if (action == RerollAction::kKeepBefore || action == RerollAction::kClosed) {
    after_.reset();
  }
  return action;
}

}  // namespace ms
