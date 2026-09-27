#include "src/frontend/screens/box_panel.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/shop.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// The shop's widths, so a piece reads the same in both.
constexpr int kNameWidth = 26;
constexpr int kTypeWidth = 16;
constexpr int kLevelWidth = 7;
constexpr int kContentWidth = 2 + kNameWidth + 2 + kTypeWidth + 2 + kLevelWidth;

// Armour has no equip type, so it shows its slot, as the shop does.
std::string TypeCell(const EquipPrototype& proto) {
  std::string type = FormatEquipType(proto.equip_type());
  return type.empty() ? FormatSlot(proto.equip_slot()) : type;
}

}  // namespace

BoxPanel::BoxPanel(const CharacterInstance& character,
                   const std::map<std::string, EquipPrototype>& equips)
    : character_(character), equips_(equips) {
}

void BoxPanel::Reset(const ItemPrototype& box) {
  box_ = box;
  stock_.clear();
  for (const std::string& key : BoxStock(box, equips_)) {
    if (character_.MeetsJob(equips_.at(key))) {
      stock_.push_back(key);
    }
  }
  selected_ = 0;
  confirm_.Close();
}

bool BoxPanel::OnEvent(const ftxui::Event& event) {
  int delta = 0;
  if (event == ftxui::Event::ArrowUp) {
    delta = -1;
  } else if (event == ftxui::Event::ArrowDown) {
    delta = 1;
  } else {
    return false;
  }
  if (!stock_.empty()) {
    selected_ = StepCursor(selected_, delta, static_cast<int>(stock_.size()));
  }
  return true;
}

const EquipPrototype* BoxPanel::selected() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &equips_.at(stock_[selected_]);
}

ftxui::Element BoxPanel::Render() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                             PadRight("Type", kTypeWidth) + "  " +
                             PadRight("Level", kLevelWidth)));
  rows.push_back(ThemedSeparator());
  if (stock_.empty()) {
    rows.push_back(EmptyState("empty", /*gutter=*/2));
  }
  for (int i = 0; i < static_cast<int>(stock_.size()); ++i) {
    const EquipPrototype& proto = equips_.at(stock_[i]);
    bool on_cursor = i == selected_;
    // Red on a level not reached yet, as in the shop: the piece is still the
    // player's to pick, and to wear later.
    ftxui::Element level = RedUnless(
        ftxui::text(PadRight("Lv" + std::to_string(proto.required_level()),
                             kLevelWidth)),
        character_.MeetsLevel(proto));
    ftxui::Element row = ftxui::hbox({
        ftxui::text(std::string(on_cursor ? "> " : "  ") +
                    PadRight(proto.name(), kNameWidth) + "  " +
                    PadRight(TypeCell(proto), kTypeWidth) + "  "),
        std::move(level),
    });
    rows.push_back(HighlightRow(std::move(row), on_cursor));
  }
  return ThemedWindow(
      " " + box_.name() + " ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth));
}

void BoxPanel::OpenConfirm() {
  confirm_.Open(/*cancel_selected=*/false);
}

ConfirmChoice BoxPanel::OnConfirmEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event));
}

ftxui::Element BoxPanel::RenderConfirm() const {
  const EquipPrototype* pick = selected();
  std::string name = pick == nullptr ? "" : pick->name();
  // No title: the question names the piece, and a title would say it twice.
  return DialogWindow("", {CenteredRow("Pick " + name + "?")},
                      confirm_.Render());
}

}  // namespace ms
