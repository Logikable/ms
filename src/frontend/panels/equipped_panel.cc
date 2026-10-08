#include "src/frontend/panels/equipped_panel.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/familiar.h"
#include "src/character/progression.h"
#include "src/character/symbol.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/equipped_list.h"
#include "src/frontend/widgets/familiar_list.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/text_field.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

const char* const kTabLabels[] = {"Gear", "Symbols", "Familiar"};
const char* const kSymbolKindLabels[] = {"Arcane", "Sacred"};

}  // namespace

EquippedPanel::EquippedPanel(CharacterInstance& character,
                             AccountInstance& account, int& panel_focus)
    : character_(character),
      account_(account),
      panel_focus_(panel_focus),
      menu_({"Unequip", "Inspect", "Scroll", "Star Force", "Cube", "Flame",
             "Soul", "Close"}),
      symbol_menu_({"Unequip", "Inspect", "Level Up", "Close"}),
      familiar_menu_(
          {"Inspect", "Switch", "Rename", "Level Up", "Cube", "Close"}) {
  // Opens on the preset the character is wearing, so the tab the player sees is
  // their current gear and an item's comparison card compares against it. The
  // autoswap doesn't pick one preset, and its first tab is Farm.
  if (!character_.autoswap_presets()) {
    gear_preset_ = character_.SlotInUse(PresetKind::kEquip);
    familiar_preset_ = character_.SlotInUse(PresetKind::kFamiliars);
  }
}

ItemMenu& EquippedPanel::menu() {
  if (active_tab_ == kFamiliarTab) {
    return familiar_menu_;
  }
  return active_tab_ == kSymbolTab ? symbol_menu_ : menu_;
}

std::vector<int> EquippedPanel::VisibleTabs() const {
  std::vector<int> tabs = {kGearTab};
  // Symbols arrives with Arcane River. Before then nothing could ever go in it,
  // and a tab that can only be empty isn't worth showing.
  if (Unlocked(Feature::kSymbols, character_, account_)) {
    tabs.push_back(kSymbolTab);
  }
  if (Unlocked(Feature::kFamiliars, character_, account_)) {
    tabs.push_back(kFamiliarTab);
  }
  return tabs;
}

void EquippedPanel::StepTab(int direction) {
  TabStop next =
      StepTabRing(VisibleTabs(), {active_tab_, on_expand_}, direction);
  on_expand_ = next.on_door;
  if (next.tab != active_tab_) {
    active_tab_ = next.tab;
    selected_ = 0;
  }
}

std::vector<EquippedRow> EquippedPanel::Rows(
    std::chrono::steady_clock::duration slide) const {
  if (on_expand_) {
    return {};  // Expand has no list to move down into
  }
  if (active_tab_ == kSymbolTab) {
    return SymbolRows(character_, symbol_kind_, selected_, slide);
  }
  if (active_tab_ == kFamiliarTab) {
    return {};  // drawn by RenderFamiliarRow, and counted by ListCount
  }
  return EquippedRows(character_, selected_, slide, Columns(), gear_preset_);
}

ItemColumns EquippedPanel::Columns() const {
  ItemListOptions options =
      EquippedListOptions(character_, account_, gear_preset_);
  // Minus the two borders: the width given is the column's, and the list is
  // drawn inside it.
  return FitItemColumns(width_ - 2, options);
}

int EquippedPanel::ListCount() const {
  if (active_tab_ == kFamiliarTab && !on_expand_) {
    return character_.summoned_familiars(familiar_preset_).size();
  }
  return static_cast<int>(
      Rows(std::chrono::steady_clock::duration::zero()).size());
}

bool EquippedPanel::ShowsSubBar() const {
  if (on_expand_) {
    return false;
  }
  if (active_tab_ == kSymbolTab) {
    return Unlocked(Feature::kSacredSymbols, character_, account_);
  }
  // The presets, and the balances beside them, arrive with the tab.
  if (active_tab_ == kFamiliarTab) {
    return true;
  }
  return Unlocked(Feature::kEquipPresets, character_, account_);
}

// The rows above the list: always the tab bar, plus the row under it when the
// tab has one.
int EquippedPanel::CursorStop() const {
  int bars = ShowsSubBar() ? 2 : 1;
  switch (zone_) {
    case kZoneTabs:
      return 0;
    case kZoneSubBar:
      return 1;
    case kZoneList:
      return bars + selected_;
  }
  return 0;
}

void EquippedPanel::MoveCursor(int delta) {
  int bars = ShowsSubBar() ? 2 : 1;
  int next = StepCursor(CursorStop(), delta, bars + ListCount());
  if (next == 0) {
    zone_ = kZoneTabs;
    return;
  }
  if (bars == 2 && next == 1) {
    zone_ = kZoneSubBar;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - bars;
}

void EquippedPanel::StepPreset(int direction) {
  StatPreset& preset =
      active_tab_ == kFamiliarTab ? familiar_preset_ : gear_preset_;
  preset = StatPresetAt(
      std::clamp(IndexOf(preset) + direction, 0, kNumStatPresets - 1));
}

void EquippedPanel::StepSymbolKind(int direction) {
  SymbolKind next = SymbolKind::kArcane;
  if (direction > 0) {
    next = SymbolKind::kSacred;
  }
  if (next != symbol_kind_) {
    symbol_kind_ = next;
    selected_ = 0;
  }
}

int EquippedPanel::menu_column() const {
  // A familiar's menu covers its mob and lines, past the border, the caret and
  // the name.
  if (active_tab_ == kFamiliarTab) {
    return 1 + kItemListCursor +
           FitFamiliarColumns(width_ - 2, /*in_use=*/false).name + kItemCellGap;
  }
  // The border, then the row up to the end of the slot cell: the caret, the
  // name and the slot, each with its leading gap.
  ItemColumns columns = Columns();
  return 1 + kItemListCursor + columns.name_width + kItemCellGap +
         columns.Width(ItemColumn::kSlot) + kItemCellGap;
}

// Entries the player hasn't reached yet are hidden before anything is checked
// about the item under the cursor. A character who just got this panel has no
// bag to unequip into, and scrolls mean nothing to them for a long while after.
void EquippedPanel::HideLockedEntries() {
  if (!Unlocked(Feature::kUnequip, character_, account_)) {
    menu_.Hide(kGearMenuUnequip);
  }
  if (!Unlocked(Feature::kScrolling, character_, account_)) {
    menu_.Hide(kGearMenuScroll);
  }
  if (!Unlocked(Feature::kStarForce, character_, account_)) {
    menu_.Hide(kGearMenuStarForce);
  }
  if (!Unlocked(Feature::kPotential, character_, account_)) {
    menu_.Hide(kGearMenuCube);
  }
  if (!Unlocked(Feature::kFlame, character_, account_)) {
    menu_.Hide(kGearMenuFlame);
  }
  if (!Unlocked(Feature::kSoul, character_, account_)) {
    menu_.Hide(kGearMenuSoul);
  }
}

// Hides what the worn item can never take. All of these check the prototype: an
// upgrade the item refuses outright gets no row, and everything else keeps one.
// So a weapon and an armour piece show the same entries however far either has
// been upgraded.
void EquippedPanel::HideRefusedEntries(EquipSlot slot) {
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    menu_.Hide(kGearMenuSoul);
    return;
  }
  // A preset can only unequip its own items. An inherited item belongs to the
  // Farm preset and is removed there. The entry is dimmed rather than hidden,
  // so the row shows why instead of quietly losing an entry.
  if (character_.InheritsSlot(gear_preset_, slot)) {
    menu_.Disable(kGearMenuUnequip);
  }
  const EquipInstance& item = *character_.WornAt(gear_preset_, slot);
  if (!Supports(item.prototype(), UPGRADE_SCROLL)) {
    menu_.Hide(kGearMenuScroll);
  }
  // Where an item is worn decides whether it can be cubed, and the slots that
  // refuse cubes (medal, badge, pocket) always refuse them.
  if (!item.CanCube()) {
    menu_.Hide(kGearMenuCube);
  }
  if (!item.CanFlame()) {
    menu_.Hide(kGearMenuFlame);
  }
  if (!item.CanTakeSoul()) {
    menu_.Hide(kGearMenuSoul);
  }
  if (!Supports(item.prototype(), UPGRADE_STAR_FORCE)) {
    menu_.Hide(kGearMenuStarForce);
  } else if (!item.CanStarForce()) {
    // Grey, not hidden: stars come after the scroll slots are used, and a dim
    // entry is how the player learns the order.
    menu_.Disable(kGearMenuStarForce);
  }
}

// Gold on an upgrade the player has unlocked but never used, which is where the
// trail from the level-up card ends.
void EquippedPanel::HighlightTrail() {
  if (LeadToAction(Feature::kScrolling, character_, account_)) {
    menu_.Highlight(kGearMenuScroll);
  }
  if (LeadToAction(Feature::kStarForce, character_, account_)) {
    menu_.Highlight(kGearMenuStarForce);
  }
  if (LeadToCubeMenu(character_, account_)) {
    menu_.Highlight(kGearMenuCube);
  }
  if (LeadToAction(Feature::kFlame, character_, account_)) {
    menu_.Highlight(kGearMenuFlame);
  }
  if (LeadToAction(Feature::kSoul, character_, account_)) {
    menu_.Highlight(kGearMenuSoul);
  }
}

void EquippedPanel::OpenFamiliarMenu() {
  familiar_menu_.Reset();
  const FamiliarBook& book = character_.familiars();
  const std::string species = selected_familiar();
  const int level = FamiliarLevel(book, species);
  // Hidden at the top, where there is no next level; grey while the pool is
  // short, so the entry says what is missing.
  if (level >= kFamiliarMaxLevel) {
    familiar_menu_.Hide(kFamiliarMenuLevelUp);
  } else if (!CanLevelFamiliar(book, species)) {
    familiar_menu_.Disable(kFamiliarMenuLevelUp);
  }
  // A familiar at level 0 has no lines to reroll.
  if (level < 1) {
    familiar_menu_.Hide(kFamiliarMenuCube);
  }
}

void EquippedPanel::OpenMenu() {
  if (active_tab_ == kFamiliarTab) {
    OpenFamiliarMenu();
    return;
  }
  if (active_tab_ == kSymbolTab) {
    symbol_menu_.Reset();
    EquipSlot slot = selected_slot();
    // Grey until the duplicates are combined. The dim entry is how the player
    // learns that combining comes first.
    if (slot == EQUIP_SLOT_UNSPECIFIED ||
        !SymbolCanLevelUp(character_.equipped().at(slot)->prototype(),
                          character_.equipped().at(slot)->equip_state())) {
      symbol_menu_.Disable(kSymbolMenuLevelUp);
    }
    return;
  }
  // Opening the menu on the worn weapon completes the trail's first step: the
  // player looked, and what they were sent to see is on screen.
  if (selected_slot() == EQUIP_SLOT_PRIMARY_WEAPON) {
    FollowedToWeapon(character_, account_);
  }
  menu_.Reset();
  HideLockedEntries();
  HideRefusedEntries(selected_slot());
  HighlightTrail();
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
  if (active_tab_ == kFamiliarTab) {
    switch (open.selected()) {
      case kFamiliarMenuInspect:
        return kFamiliarInspect;
      case kFamiliarMenuSwitch:
        return kFamiliarSwitch;
      case kFamiliarMenuRename:
        rename_field_.BeginEdit();
        return kMain;
      case kFamiliarMenuLevelUp:
        return kFamiliarLevel;
      case kFamiliarMenuCube:
        return kFamiliarCube;
      default:
        return kMain;
    }
  }
  // Unequip and Inspect are the first two entries of the gear and symbol
  // menus, so neither needs to check which of the two is open.
  if (open.selected() == kGearMenuUnequip) {
    character_.Unequip(selected_slot(), gear_preset_);
    return kMain;
  }
  if (open.selected() == kGearMenuInspect) {
    return kInspect;
  }
  if (active_tab_ == kSymbolTab) {
    return open.selected() == kSymbolMenuLevelUp ? kSymbolLevel : kMain;
  }
  if (open.selected() == kGearMenuScroll) {
    // Recorded whether or not there is a scroll to show: they pressed the
    // entry, which is what the gold asked.
    FollowedToAction(Feature::kScrolling, account_);
    if (scroll_panel.SetFilterForPrototype(
            character_.WornAt(gear_preset_, selected_slot())->prototype())) {
      return kScrollSelect;
    }
  }
  if (open.selected() == kGearMenuStarForce) {
    FollowedToAction(Feature::kStarForce, account_);
    return kStarForce;
  }
  if (open.selected() == kGearMenuCube) {
    FollowedToCubeMenu(character_, account_);
    return kCubing;
  }
  if (open.selected() == kGearMenuFlame) {
    FollowedToAction(Feature::kFlame, account_);
    return kFlaming;
  }
  if (open.selected() == kGearMenuSoul) {
    FollowedToAction(Feature::kSoul, account_);
    return kSouling;
  }
  return kMain;
}

std::string EquippedPanel::selected_familiar() const {
  if (active_tab_ != kFamiliarTab || on_expand_) {
    return "";
  }
  const auto& names = character_.summoned_familiars(familiar_preset_);
  if (selected_ < 0 || selected_ >= names.size()) {
    return "";
  }
  return names[selected_];
}

ftxui::Element EquippedPanel::RenderFamiliarRow(int index,
                                                bool on_cursor) const {
  const FamiliarColumns columns =
      FitFamiliarColumns(width_ - 2, /*in_use=*/false);
  const std::string& species =
      character_.summoned_familiars(familiar_preset_)[index];
  const FamiliarCells cells =
      FamiliarRowCells(character_.familiars(), species, columns, false);
  ftxui::Element name = nullptr;
  if (index == selected_ && rename_field_.editing()) {
    // As the character's name field draws itself: inverted, with a caret
    // after the typed text, so an empty field still looks like a field.
    name = ftxui::text(PadRight(rename_field_.text() + "_", columns.name)) |
           ftxui::inverted;
  }
  return FamiliarRowElement(on_cursor ? "> " : "  ", cells, std::move(name));
}

ftxui::Element EquippedPanel::RenderFamiliarBalances() const {
  const FamiliarBook& book = character_.familiars();
  // Read-only on another player's sheet, which carries their familiars but not
  // their pool.
  std::string text = "Total Lv " + std::to_string(TotalFamiliarLevels(book));
  if (!read_only_) {
    text += "   " + FormatWithCommas(book.exp()) + " EXP";
  }
  return ftxui::text(text) | ftxui::color(kTheme);
}

bool EquippedPanel::OnRenameEvent(const ftxui::Event& event) {
  const std::string species = selected_familiar();
  const TextEntry entry = rename_field_.OnEvent(event);
  if (entry == TextEntry::kCommitted && !species.empty()) {
    character_.RenameFamiliar(species, rename_field_.text());
    rename_field_.EndEdit();
  }
  // Up and Down discard the typed name and then move, as on the character's
  // name field.
  if (entry == TextEntry::kCancelled &&
      (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
  }
  return true;
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
  // Drawn from selected_, not state.focused. The Menu tracks its own current
  // row, and the panel moves the cursor itself without the Menu seeing. The two
  // then disagree, and the caret would point at the row the player left while
  // Enter acts on the one they are on.
  bool on_cursor =
      idx == selected_ && zone_ == kZoneList && panel_focus_ == kEquipPanel;
  if (active_tab_ == kFamiliarTab && !on_expand_ && idx >= 0 &&
      idx < ListCount()) {
    ftxui::Element row = RenderFamiliarRow(idx, on_cursor);
    if (idx == selected_) {
      row = std::move(row) | ftxui::reflect(cursor_box_);
    }
    return HighlightRow(std::move(row), on_cursor);
  }
  std::string cursor = on_cursor ? "> " : "  ";
  if (idx < 0 || idx >= static_cast<int>(row_texts_.size())) {
    return HighlightRow(ftxui::text(cursor + state.label), on_cursor);
  }
  // Only the name goes gold, not the whole row, since the name is the item
  // being pointed at.
  ftxui::Element row =
      ItemRowElement(cursor, row_texts_[idx],
                     led_[idx] ? ftxui::color(kYellow) : ftxui::nothing);
  if (idx == selected_) {
    // Records where this row lands so the item menu can open beside it.
    row = std::move(row) | ftxui::reflect(cursor_box_);
  }
  if (idx >= 0 && idx < static_cast<int>(inactive_.size()) && inactive_[idx]) {
    // Worn but contributing nothing. Dimmed rather than hidden, so it shows
    // that without removing the numbers.
    row |= ftxui::dim;
  }
  // Applied last, so the band sits under everything on the row, including the
  // dimming. A piece contributing nothing still needs a visible cursor.
  return HighlightRow(std::move(row), on_cursor);
}

std::string EquippedPanel::Header() const {
  if (active_tab_ == kSymbolTab) {
    return SymbolHeader(symbol_kind_);
  }
  if (active_tab_ == kFamiliarTab) {
    return FamiliarHeader(FitFamiliarColumns(width_ - 2, /*in_use=*/false));
  }
  return ItemListHeader(Columns());
}

void EquippedPanel::RebuildRows() {
  // The menu changes selected_ without telling this panel, so a move is noticed
  // here rather than at the keypress. A name scrolls only while its row is
  // drawn as selected, the same test that draws the cursor.
  name_clock_.Follow(selected_,
                     panel_focus_ == kEquipPanel && zone_ == kZoneList);
  entries_.clear();
  inactive_.clear();
  row_texts_.clear();
  led_.clear();
  // Checked once for the whole list rather than per row, since it is a fact
  // about the character and only the worn weapon's row uses it. Never shown on
  // someone else's gear: the trail is about the reader's own upgrades.
  bool lead = !read_only_ && LeadToWeapon(character_, account_);
  if (active_tab_ == kFamiliarTab && !on_expand_) {
    for (const std::string& species :
         character_.summoned_familiars(familiar_preset_)) {
      entries_.push_back(FamiliarDisplayName(character_.familiars(), species));
      inactive_.push_back(false);
      row_texts_.push_back({});
      led_.push_back(false);
    }
  }
  for (const EquippedRow& row : Rows(name_clock_.Elapsed())) {
    inactive_.push_back(row.inactive || row.inherited);
    row_texts_.push_back(row.text);
    led_.push_back(lead && row.slot == EQUIP_SLOT_PRIMARY_WEAPON);
    entries_.push_back(row.text.text);
  }
  if (!entries_.empty()) {
    selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
  } else if (zone_ == kZoneList) {
    // Nothing to select, so the cursor moves up to the bar instead of sitting
    // on a row that isn't drawn. Only from the list: the bar is its own stop,
    // and an empty list doesn't affect it.
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
  // Its label is the state Enter would leave the panel in. It is drawn as its
  // own layer rather than as another chip, so it stays at the far right however
  // many tabs come and go to its left.
  ftxui::Element expand =
      TabChip(expanded_ ? "Close" : "Expand", on_expand_, row_selected);
  // Two layers over one row: the chips from the left, Expand from the right.
  // The chips get no width limit, since two of them fit easily in a row this
  // wide. The bar has no active chip while the cursor is on Expand, so only one
  // thing is highlighted.
  return ftxui::dbox({
      TabBar(specs, on_expand_ ? -1 : active, row_selected, /*width=*/0),
      ftxui::hbox({ftxui::filler(), std::move(expand)}),
  });
}

ftxui::Element EquippedPanel::RenderSubBar(bool row_selected) const {
  std::vector<TabSpec> specs;
  if (active_tab_ == kSymbolTab) {
    for (const char* label : kSymbolKindLabels) {
      specs.push_back({label});
    }
    return TabBar(specs, static_cast<int>(symbol_kind_), row_selected,
                  /*width=*/0);
  }
  const PresetKind kind =
      active_tab_ == kFamiliarTab ? PresetKind::kFamiliars : PresetKind::kEquip;
  for (int i = 0; i < kNumStatPresets; ++i) {
    const StatPreset slot = StatPresetAt(i);
    specs.push_back(
        {PresetSlotLabel(slot, character_.autoswap_presets(),
                         character_.SlotInUse(kind) == slot, kind)});
  }
  if (active_tab_ != kFamiliarTab) {
    return TabBar(specs, IndexOf(gear_preset_), row_selected, /*width=*/0);
  }
  // The balances sit in the middle of the row, as the bag's currencies sit in
  // its tab row, moved right only to keep a gutter from the last chip.
  ftxui::Element chips =
      TabBar(specs, IndexOf(familiar_preset_), row_selected, /*width=*/0);
  ftxui::Element balances = RenderFamiliarBalances();
  const int chips_width = ftxui::Dimension::Fit(chips).dimx;
  const int balances_width = ftxui::Dimension::Fit(balances).dimx;
  const int inner = width_ - 2;
  const int room = std::max(0, inner - chips_width - balances_width);
  const int lead = std::min(
      room,
      std::max(kBalanceGutter, (inner - balances_width) / 2 - chips_width));
  return ftxui::hbox({std::move(chips), ftxui::text(std::string(lead, ' ')),
                      std::move(balances), ftxui::filler()});
}

ftxui::Element EquippedPanel::RenderContent(ftxui::Component menu) {
  // Rebuilt from equipped() on every render, so the display stays in sync with
  // whatever the item menu did.
  RebuildRows();
  bool focused = panel_focus_ == kEquipPanel;
  std::vector<ftxui::Element> rows;
  // Drawn from the level the panel arrives at, with Gear as the only chip until
  // Symbols. Expand sits at the far right, and fullscreen shouldn't wait for
  // level 200.
  rows.push_back(RenderTabBar(focused && zone_ == kZoneTabs));
  // Under the bar rather than in it: the presets are three versions of one tab,
  // as Arcane and Sacred are, and a separate row shows that.
  if (ShowsSubBar()) {
    rows.push_back(RenderSubBar(focused && zone_ == kZoneSubBar));
  }
  rows.push_back(PanelSeparator(highlighted_));
  if (on_expand_) {
    // Expand is a door rather than a page, so instead of listing gear it says
    // how to go through. A filler follows because the window is taller than
    // this line and the line belongs at the top.
    rows.push_back(CenteredRow(expanded_
                                   ? "Hit Enter to close Equipment"
                                   : "Hit Enter to fullscreen Equipment"));
    rows.push_back(ftxui::filler());
    return AccentWindow(" Equipped ", ftxui::vbox(std::move(rows)),
                        PanelAccent(highlighted_), focused,
                        account_.panel_title_blink());
  }
  if (entries_.empty()) {
    rows.push_back(EmptyState("empty"));
    return AccentWindow(" Equipped ", ftxui::vbox(std::move(rows)),
                        PanelAccent(highlighted_), focused,
                        account_.panel_title_blink());
  }
  rows.push_back(ftxui::text(Header()));
  rows.push_back(PanelSeparator(highlighted_));
  // Only the items scroll. The header row and the rule stay in place.
  // ftxui::Menu marks its selected entry, which the frame scrolls to, so the
  // cursor can't move out of view.
  rows.push_back(menu->Render() | ftxui::vscroll_indicator | ftxui::yframe |
                 ftxui::flex);
  return AccentWindow(" Equipped ", ftxui::vbox(std::move(rows)),
                      PanelAccent(highlighted_), focused,
                      account_.panel_title_blink());
}

bool EquippedPanel::OnTabBarEvent(const ftxui::Event& event,
                                  const std::function<void()>& on_expand) {
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
  // Enter works only on Expand. Gear and Symbols are pages, and the only thing
  // to do with a page is move down into it.
  if (IsForward(event) && on_expand_ && on_expand != nullptr) {
    if (!expanded_) {
      // Expand isn't a page, so the widened panel opens on the first tab rather
      // than on the button that would close it. Since the bar wraps, one step
      // right does exactly that.
      StepTab(+1);
    }
    on_expand();
    return true;
  }
  // Consume everything else, or it reaches the hidden Menu and moves its
  // selection while the bar has focus.
  return true;
}

bool EquippedPanel::OnSubBarEvent(const ftxui::Event& event) {
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    int direction = event == ftxui::Event::ArrowLeft ? -1 : 1;
    if (active_tab_ == kSymbolTab) {
      StepSymbolKind(direction);
    } else {
      StepPreset(direction);
    }
    return true;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Enter wears the preset under the cursor, with no menu, just as Enter on
  // Expand goes through it. With the autoswap on there is nothing to pick: the
  // activity wears the preset it names. The symbol kinds are only lists.
  if (IsForward(event) && active_tab_ == kGearTab &&
      !character_.autoswap_presets() && !read_only_) {
    character_.SetSlotInUse(PresetKind::kEquip, gear_preset_);
    return true;
  }
  if (IsForward(event) && active_tab_ == kFamiliarTab &&
      !character_.autoswap_presets() && !read_only_) {
    character_.SetSlotInUse(PresetKind::kFamiliars, familiar_preset_);
    return true;
  }
  // Consume everything else, for the same reason as on the bar.
  return true;
}

bool EquippedPanel::OnListEvent(const ftxui::Event& event,
                                const std::function<void()>& on_enter) {
  if (rename_field_.editing()) {
    return OnRenameEvent(event);
  }
  // Handle the two ends of the list here and leave the rest to the ftxui::Menu,
  // which scrolls the view to follow its own cursor and would stop if its keys
  // were taken.
  bool up = event == ftxui::Event::ArrowUp;
  bool down = event == ftxui::Event::ArrowDown;
  int count = ListCount();
  if ((up && selected_ == 0) || (down && selected_ >= count - 1)) {
    MoveCursor(up ? -1 : 1);
    return true;
  }
  // Both Enter and Space, handled here rather than by the Menu's on_enter. The
  // Menu responds only after a render has filled its entries, and a key can
  // arrive before the first frame.
  if (IsForward(event) || event == ftxui::Event::Character(' ')) {
    if (count > 0) {
      on_enter();
    }
    return true;
  }
  return false;
}

ftxui::Component EquippedPanel::MakeComponent(std::function<void()> on_enter,
                                              std::function<void()> on_expand) {
  ftxui::MenuOption opt;
  // Also disables the default inversion, so the caret looks the same whether or
  // not the item menu is open.
  opt.entries_option.transform = [this](ftxui::EntryState state) {
    return RenderRow(state);
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // Focusable whether or not anything is worn. Container::Tab drops every key
  // when its active panel isn't focusable, and an ftxui::Menu with an empty
  // list isn't, which would leave this panel unresponsive once the player
  // unequips everything.
  ftxui::Component renderer = AlwaysFocusable(ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); }));
  return ftxui::CatchEvent(renderer,
                           [this, on_enter, on_expand](ftxui::Event event) {
                             if (zone_ == kZoneTabs) {
                               return OnTabBarEvent(event, on_expand);
                             }
                             if (zone_ == kZoneSubBar) {
                               return OnSubBarEvent(event);
                             }
                             return OnListEvent(event, on_enter);
                           });
}

}  // namespace ms
