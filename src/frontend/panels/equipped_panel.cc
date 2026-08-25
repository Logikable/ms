#include "src/frontend/panels/equipped_panel.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/arcane_force.h"
#include "src/character/progression.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/widgets/equipped_list.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

const char* const kTabLabels[] = {"Gear", "Symbols"};

}  // namespace

EquippedPanel::EquippedPanel(CharacterInstance& character,
                             AccountInstance& account, int& panel_focus)
    : character_(character),
      account_(account),
      panel_focus_(panel_focus),
      menu_({"Unequip", "Inspect", "Scroll", "Star Force", "Close"}),
      symbol_menu_({"Unequip", "Inspect", "Level Up", "Close"}) {
}

ItemMenu& EquippedPanel::menu() {
  return active_tab_ == kSymbolTab ? symbol_menu_ : menu_;
}

std::vector<int> EquippedPanel::VisibleTabs() const {
  std::vector<int> tabs = {kGearTab};
  // Symbols arrives with Arcane River. Before then there is nothing that could
  // ever go in it, and a tab that can only be empty is not a tab.
  if (Unlocked(Feature::kSymbols, character_, account_)) {
    tabs.push_back(kSymbolTab);
  }
  return tabs;
}

void EquippedPanel::StepTab(int direction) {
  std::vector<int> tabs = VisibleTabs();
  std::vector<int>::iterator at =
      std::find(tabs.begin(), tabs.end(), active_tab_);
  if (at == tabs.end()) {
    active_tab_ = kGearTab;
    return;
  }
  int next = static_cast<int>(at - tabs.begin()) + direction;
  if (next < 0 || next >= static_cast<int>(tabs.size())) {
    return;  // the ends of the bar are walls, not wrapping points
  }
  active_tab_ = tabs[next];
  selected_ = 0;
}

std::vector<EquippedRow> EquippedPanel::Rows(
    std::chrono::steady_clock::duration slide) const {
  return active_tab_ == kSymbolTab ? SymbolRows(character_, selected_, slide)
                                   : EquippedRows(character_, selected_, slide);
}

int EquippedPanel::ListCount() const {
  return static_cast<int>(
      Rows(std::chrono::steady_clock::duration::zero()).size());
}

bool EquippedPanel::HasTabBar() const {
  return VisibleTabs().size() > 1;
}

int EquippedPanel::CursorStop() const {
  return zone_ == kZoneTabs ? 0 : selected_ + 1;
}

void EquippedPanel::MoveCursor(int delta) {
  int count = ListCount();
  if (!HasTabBar()) {
    // No bar to step onto, so the list is a ring on its own.
    zone_ = kZoneList;
    selected_ = StepCursor(selected_, delta, count);
    return;
  }
  int next = StepCursor(CursorStop(), delta, 1 + count);
  if (next == 0) {
    zone_ = kZoneTabs;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - 1;
}

void EquippedPanel::OpenMenu() {
  if (active_tab_ == kSymbolTab) {
    symbol_menu_.Reset();
    EquipSlot slot = selected_slot();
    // Greyed until the duplicates are in: the entry standing there dim is how
    // the player learns that combining comes first.
    if (slot == EQUIP_SLOT_UNSPECIFIED ||
        !SymbolCanLevelUp(character_.equipped().at(slot).equip_state())) {
      symbol_menu_.Disable(kSymbolMenuLevelUp);
    }
    return;
  }
  // Opening the menu on the worn weapon is the trail's first step walked: the
  // player looked, and what they were being sent to look at is on screen.
  if (selected_slot() == EQUIP_SLOT_PRIMARY_WEAPON) {
    FollowedToWeapon(character_, account_);
  }
  menu_.Reset();
  // Entries the player has not reached yet are not drawn at all, ahead of any
  // question about this particular item: a character who has just been handed
  // this panel has no bag to unequip into yet, and asking about scrolls means
  // nothing to them for a long while after that.
  if (!Unlocked(Feature::kUnequip, character_, account_)) {
    menu_.Hide(kGearMenuUnequip);
  }
  if (!Unlocked(Feature::kScrolling, character_, account_)) {
    menu_.Hide(kGearMenuScroll);
  }
  if (!Unlocked(Feature::kStarForce, character_, account_)) {
    menu_.Hide(kGearMenuStarForce);
  }
  EquipSlot slot = selected_slot();
  if (slot != EQUIP_SLOT_UNSPECIFIED) {
    const EquipInstance& item = character_.equipped().at(slot);
    // Both ask the prototype: an upgrade the item refuses outright is worth no
    // row, and everything else keeps one. So a weapon and a piece of armour
    // carry the same two entries however far along either of them is.
    if (!Supports(item.prototype(), UPGRADE_SCROLL)) {
      menu_.Hide(kGearMenuScroll);
    }
    if (!Supports(item.prototype(), UPGRADE_STAR_FORCE)) {
      menu_.Hide(kGearMenuStarForce);
    } else if (!item.CanStarForce()) {
      // Greyed, not gone: stars come after the slots are spent, and a row that
      // stands there dim is how the player learns the order.
      menu_.Disable(kGearMenuStarForce);
    }
  }
  // Gold on an upgrade the player has been handed but never used, which is
  // where the trail from the level-up card ends. Last, so it lands on the
  // entries as they finally stand.
  if (LeadToAction(Feature::kScrolling, character_, account_)) {
    menu_.Highlight(kGearMenuScroll);
  }
  if (LeadToAction(Feature::kStarForce, character_, account_)) {
    menu_.Highlight(kGearMenuStarForce);
  }
}

Screen EquippedPanel::OnMenuEvent(ftxui::Event event,
                                  ScrollPanel& scroll_panel) {
  ItemMenu& open = menu();
  if (IsBack(event)) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    open.Up();
    return kItemMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    open.Down();
    return kItemMenu;
  }
  if (!IsForward(event)) {
    return kItemMenu;
  }
  // Unequip and Inspect are the first two entries of both menus, so neither
  // has to ask which one is open.
  if (open.selected() == kGearMenuUnequip) {
    character_.Unequip(selected_slot());
    return kMain;
  }
  if (open.selected() == kGearMenuInspect) {
    return kInspect;
  }
  if (active_tab_ == kSymbolTab) {
    return open.selected() == kSymbolMenuLevelUp ? kSymbolLevel : kMain;
  }
  if (open.selected() == kGearMenuScroll) {
    // Followed whether or not there is a scroll to show: they pressed the
    // entry, which is what the gold was asking them to do.
    FollowedToAction(Feature::kScrolling, account_);
    if (scroll_panel.SetFilterForPrototype(
            character_.equipped().at(selected_slot()).prototype())) {
      return kScrollSelect;
    }
  }
  if (open.selected() == kGearMenuStarForce) {
    FollowedToAction(Feature::kStarForce, account_);
    return kStarForce;
  }
  return kMain;
}

EquipSlot EquippedPanel::selected_slot() const {
  std::vector<EquippedRow> rows =
      Rows(std::chrono::steady_clock::duration::zero());
  if (selected_ < 0 || selected_ >= static_cast<int>(rows.size())) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  return rows[selected_].slot;
}

ftxui::Element EquippedPanel::RenderRow(const ftxui::EntryState& state) {
  int idx = state.index;
  // Drawn from selected_, not from state.focused. The Menu keeps its own idea
  // of the current row, and the panel moves the cursor itself -- a move the
  // Menu never sees. The two then disagree, and the caret points at the row
  // the player left while Enter acts on the one they are on.
  bool on_cursor =
      idx == selected_ && zone_ == kZoneList && panel_focus_ == kEquipPanel;
  std::string cursor = on_cursor ? "> " : "  ";
  ftxui::Element row = ftxui::text(cursor + state.label);
  if (idx >= 0 && idx < static_cast<int>(led_.size()) && led_[idx]) {
    // The name alone, not the whole row: it is the item being pointed at, and
    // the columns after it say what they always said. Split on the byte count
    // RebuildRows kept -- a name may hold multibyte characters, so its column
    // width is no guide to its length.
    size_t bytes =
        std::min(static_cast<size_t>(name_bytes_[idx]), state.label.size());
    row = ftxui::hbox({
        ftxui::text(cursor + state.label.substr(0, bytes)) |
            ftxui::color(kYellow),
        ftxui::text(state.label.substr(bytes)),
    });
  }
  if (idx == selected_) {
    // Records where this row lands so the item menu can open beside it.
    row = std::move(row) | ftxui::reflect(cursor_box_);
  }
  if (idx >= 0 && idx < static_cast<int>(inactive_.size()) && inactive_[idx]) {
    // Worn but contributing nothing. Dimmed rather than hidden, so it says so
    // without taking the numbers away.
    row |= ftxui::dim;
  }
  return row;
}

const char* EquippedPanel::Header() const {
  return active_tab_ == kSymbolTab ? kSymbolHeader : kEquippedHeader;
}

void EquippedPanel::RebuildRows() {
  // The menu writes selected_ behind this panel's back, so a move is noticed
  // here rather than hooked at the keypress.
  name_clock_.Follow(selected_);
  entries_.clear();
  inactive_.clear();
  name_bytes_.clear();
  led_.clear();
  // Asked once for the whole list rather than per row: it is a fact about the
  // character, and only the worn weapon's row acts on it.
  bool lead = LeadToWeapon(character_, account_);
  for (const EquippedRow& row : Rows(name_clock_.Elapsed())) {
    inactive_.push_back(row.inactive);
    name_bytes_.push_back(row.name_bytes);
    led_.push_back(lead && row.slot == EQUIP_SLOT_PRIMARY_WEAPON);
    entries_.push_back(row.text);
  }
  if (!entries_.empty()) {
    selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
  } else if (HasTabBar()) {
    // Nothing to stand on. The bar is the only stop left, and leaving the
    // cursor in the list would make both arrow keys do nothing.
    zone_ = kZoneTabs;
  }
}

ftxui::Element EquippedPanel::RenderTabBar(bool row_selected) const {
  std::vector<TabSpec> specs;
  int active = 0;
  for (int tab : VisibleTabs()) {
    if (tab == active_tab_) {
      active = static_cast<int>(specs.size());
    }
    specs.push_back({kTabLabels[tab]});
  }
  // No width limit: two chips fit several times over in a row this wide.
  return TabBar(specs, active, row_selected, /*width=*/0);
}

ftxui::Element EquippedPanel::RenderContent(ftxui::Component menu) {
  // Rebuilt from equipped() on every render, so the display stays in step with
  // whatever the item menu did.
  RebuildRows();
  bool focused = panel_focus_ == kEquipPanel;
  std::vector<ftxui::Element> rows;
  // The bar is drawn only once there is a second tab to reach: one chip over a
  // list says nothing the window title has not already said.
  if (HasTabBar()) {
    rows.push_back(RenderTabBar(focused && zone_ == kZoneTabs));
    rows.push_back(PanelSeparator(highlighted_));
  }
  if (entries_.empty()) {
    rows.push_back(EmptyState("empty"));
    return AccentWindow(" Equipped ", ftxui::vbox(std::move(rows)),
                        PanelAccent(highlighted_), focused);
  }
  rows.push_back(ftxui::text(Header()));
  rows.push_back(PanelSeparator(highlighted_));
  // Only the items scroll; the header row and the rule stay put.
  // ftxui::Menu marks its selected entry, which is what the frame scrolls to,
  // so the cursor cannot walk out of view.
  rows.push_back(menu->Render() | ftxui::vscroll_indicator | ftxui::yframe |
                 ftxui::flex);
  return AccentWindow(" Equipped ", ftxui::vbox(std::move(rows)),
                      PanelAccent(highlighted_), focused);
}

bool EquippedPanel::OnTabBarEvent(const ftxui::Event& event) {
  if (event == ftxui::Event::ArrowLeft) {
    StepTab(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    StepTab(+1);
    return true;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Swallow the rest, or it leaks to the hidden Menu and silently moves its
  // selection while the bar holds focus.
  return true;
}

bool EquippedPanel::OnListEvent(const ftxui::Event& event,
                                const std::function<void()>& on_enter) {
  // Take the two ends of the list and leave everything between them to the
  // ftxui::Menu, which scrolls the view to follow its own cursor and would
  // stop doing so if its keys were taken away.
  bool up = event == ftxui::Event::ArrowUp;
  bool down = event == ftxui::Event::ArrowDown;
  int count = ListCount();
  if ((up && selected_ == 0) || (down && selected_ >= count - 1)) {
    MoveCursor(up ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::Character(' ')) {
    if (count > 0) {
      on_enter();
    }
    return true;
  }
  return false;
}

ftxui::Component EquippedPanel::MakeComponent(std::function<void()> on_enter) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  // Also suppresses the default inversion, so the caret looks the same whether
  // or not the item menu is open.
  opt.entries_option.transform = [this](ftxui::EntryState state) {
    return RenderRow(state);
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // Focusable whether or not anything is worn. Container::Tab asks its active
  // panel whether it is focusable and drops every key when the answer is no,
  // and an ftxui::Menu says no on an empty list -- which would leave this
  // panel deaf the moment the player strips down.
  ftxui::Component renderer = AlwaysFocusable(ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); }));
  return ftxui::CatchEvent(renderer, [this, on_enter](ftxui::Event event) {
    if (zone_ == kZoneTabs) {
      return OnTabBarEvent(event);
    }
    return OnListEvent(event, on_enter);
  });
}

}  // namespace ms
