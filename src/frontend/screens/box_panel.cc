#include "src/frontend/screens/box_panel.h"

#include <algorithm>
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
#include "src/frontend/widgets/text_columns.h"
#include "src/item/ring_box.h"
#include "src/item/shop.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// The shop's widths, so a piece reads the same in both. The shop scrolls a
// name too long for its column; a box has no clock, so its Name column widens
// to its longest piece instead.
constexpr int kNameWidth = 26;
constexpr int kTypeWidth = 16;
constexpr int kLevelWidth = 7;

// Armour has no equip type, so it shows its slot, as the shop does.
std::string TypeCell(const EquipPrototype& proto) {
  std::string type = FormatEquipType(proto.equip_type());
  return type.empty() ? FormatSlot(proto.equip_slot()) : type;
}

}  // namespace

BoxPanel::BoxPanel(const CharacterInstance& character,
                   const std::map<std::string, EquipPrototype>& equips,
                   const std::map<std::string, ItemPrototype>& items)
    : character_(character),
      equips_(equips),
      items_(items),
      menu_({"Inspect", "Select", "Close"}) {
}

void BoxPanel::Reset(const ItemPrototype& box) {
  box_ = box;
  stock_.clear();
  if (box.has_ring_box()) {
    ListRingBox();
  }
  for (const std::string& key : BoxStock(box, equips_)) {
    const EquipPrototype& piece = equips_.at(key);
    if (character_.MeetsJob(piece)) {
      stock_.push_back({piece.name(), &piece, "", ""});
    }
  }
  name_width_ = kNameWidth;
  for (const BoxChoice& choice : stock_) {
    name_width_ = std::max(name_width_, TextColumns(choice.name));
  }
  selected_ = 0;
  confirm_.Close();
  menu_open_ = false;
}

// A skill the catalog has no ring for is left out rather than offered and
// refused on Confirm.
void BoxPanel::ListRingBox() {
  const RingBox& rings = box_.ring_box();
  for (const std::string& skill : rings.skills()) {
    if (rings.levels_size() > 0 &&
        RingAt(skill, rings.levels(0).level(), equips_) != nullptr) {
      stock_.push_back(
          {skill + " " + RingLevelRange(rings), nullptr, skill, ""});
    }
  }
  for (const std::string& key : rings.items()) {
    if (items_.count(key) > 0) {
      stock_.push_back({items_.at(key).name(), nullptr, "", key});
    }
  }
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

const BoxChoice* BoxPanel::selected() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &stock_[selected_];
}

// A ring row reads its slot and level off the lowest ring it can roll, which
// every level shares. An Etc item has neither.
const EquipPrototype* BoxPanel::ChoiceEquip(const BoxChoice& choice) const {
  if (choice.ring_skill.empty()) {
    return choice.equip;
  }
  return RingAt(choice.ring_skill, box_.ring_box().levels(0).level(), equips_);
}

const EquipPrototype* BoxPanel::selected_equip() const {
  const BoxChoice* pick = selected();
  return pick == nullptr ? nullptr : ChoiceEquip(*pick);
}

const ItemPrototype* BoxPanel::selected_item() const {
  const BoxChoice* pick = selected();
  if (pick == nullptr || pick->item_key.empty()) {
    return nullptr;
  }
  return &items_.at(pick->item_key);
}

ftxui::Element BoxPanel::RenderRow(const BoxChoice& choice,
                                   bool on_cursor) const {
  const EquipPrototype* piece = ChoiceEquip(choice);
  std::string type = piece == nullptr ? "Etc" : TypeCell(*piece);
  // Red on a level not reached yet, as in the shop: the piece is still the
  // player's to pick, and to wear later.
  ftxui::Element level =
      piece == nullptr
          ? ftxui::text(PadRight("", kLevelWidth))
          : RedUnless(ftxui::text(PadRight(
                          "Lv" + std::to_string(piece->required_level()),
                          kLevelWidth)),
                      character_.MeetsLevel(*piece));
  return ftxui::hbox({
      ftxui::text(std::string(on_cursor ? "> " : "  ") +
                  PadRight(choice.name, name_width_) + "  " +
                  PadRight(type, kTypeWidth) + "  "),
      std::move(level),
  });
}

ftxui::Element BoxPanel::Render() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text("  " + PadRight("Name", name_width_) + "  " +
                             PadRight("Type", kTypeWidth) + "  " +
                             PadRight("Level", kLevelWidth)));
  rows.push_back(ThemedSeparator());
  if (stock_.empty()) {
    rows.push_back(EmptyState("empty", /*gutter=*/2));
  }
  for (int i = 0; i < static_cast<int>(stock_.size()); ++i) {
    bool on_cursor = i == selected_;
    rows.push_back(HighlightRow(RenderRow(stock_[i], on_cursor), on_cursor));
  }
  ftxui::Element window = ThemedWindow(
      " " + box_.name() + " ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
                      2 + name_width_ + 2 + kTypeWidth + 2 + kLevelWidth));
  if (!menu_open_) {
    return window;
  }
  // Below the border, header and divider; over the Type column, as the shop's
  // covers its price, so the row's name stays readable. Floated for the shop's
  // reason too: on the last rows it hangs past the border rather than
  // stretching the window.
  constexpr int kFirstRow = 3;
  return ftxui::dbox({
      std::move(window),
      Floating(menu_.Render(kFirstRow + selected_, 1 + 2 + name_width_)),
  });
}

void BoxPanel::OpenMenu() {
  if (selected() == nullptr) {
    return;
  }
  menu_.Reset();
  menu_open_ = true;
}

Screen BoxPanel::OnMenuEvent(const ftxui::Event& event) {
  if (IsBack(event)) {
    menu_open_ = false;
    return kBoxOpen;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kBoxMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kBoxMenu;
  }
  if (IsForward(event)) {
    // Closed whichever entry was chosen, so the next screen isn't drawn with
    // the menu still over the list.
    menu_open_ = false;
    if (menu_.selected() == kBoxMenuInspect) {
      return kBoxInspect;
    }
    if (menu_.selected() == kBoxMenuSelect) {
      return kBoxConfirm;
    }
    return kBoxOpen;
  }
  // Consume everything else, since the menu is modal over the list.
  return kBoxMenu;
}

void BoxPanel::OpenConfirm() {
  confirm_.Open(/*cancel_selected=*/false);
}

ConfirmChoice BoxPanel::OnConfirmEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event));
}

ftxui::Element BoxPanel::RenderConfirm() const {
  const BoxChoice* pick = selected();
  std::string name = pick == nullptr ? "" : pick->name;
  // No title: the question names the piece, and a title would say it twice.
  return DialogWindow("", {CenteredRow("Pick " + name + "?")},
                      confirm_.Render());
}

}  // namespace ms
