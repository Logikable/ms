#include "src/frontend/panels/inventory_panel.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/progression.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// The seen-key `tab` announces itself under, or "" for one with nothing to
// announce. Equip has a key per advancement that HANDS SOMETHING OVER, so the
// tab stays quiet at the 3rd and 4th rather than sending the player to look at
// a bag nothing arrived in.
std::string TabKey(int tab, const CharacterInstance& character) {
  if (tab == kShopTab) {
    return kShopTabKey;
  }
  if (tab == kEquipTab && character.proto().job_stage() > 0 &&
      !StarterEquipsFor(character.proto().job()).empty()) {
    return EquipGiftTabKey(character.proto().job_stage());
  }
  return "";
}

// Room for any row index under one tab, so folding the tab and the row into
// one key cannot make two different selections collide.
constexpr int kNameClockTabStride = 4096;

}  // namespace

InventoryPanel::InventoryPanel(CharacterInstance& character,
                               AccountInstance& account, int& panel_focus)
    : character_(character),
      account_(account),
      panel_focus_(panel_focus),
      menu_({"Equip", "Inspect", "Combine", "Scroll", "Hammer", "Star Force",
             "Cube", "Recover", "Sell", "Multi-Sell", "Close"}),
      sell_menu_({"Inspect", "Sell", "Multi-Sell", "Close"}),
      tab_menu_({"Sort", "Close"}) {
}

ItemMenu& InventoryPanel::menu() {
  if (active_tab_ == kEquipTab) {
    return menu_;
  }
  return sell_menu_;
}

std::vector<int> InventoryPanel::VisibleTabs() const {
  // Token stands in the bar from the first frame, as Etc does. It is where a
  // currency lives, and a bar whose tabs came and went with what the bag
  // happened to hold would move the others under the player's hand.
  std::vector<int> tabs = {kEquipTab, kTokenTab, kEtcTab};
  // The shop is a place in the world rather than a page of the bag, and it is
  // not open to a character who has nothing to spend and nothing to spend it
  // on. Until then the bar simply ends at Etc.
  if (Unlocked(Feature::kShop, character_, account_)) {
    tabs.push_back(kShopTab);
  }
  // Shared storage, and the same: a door, and not one worth showing until
  // there is a second character to share with.
  if (Unlocked(Feature::kBank, character_, account_)) {
    tabs.push_back(kBankTab);
  }
  return tabs;
}

void InventoryPanel::StepTab(int direction) {
  TabStop next =
      StepTabRing(VisibleTabs(), {active_tab_, on_expand_}, direction);
  on_expand_ = next.on_door;
  if (next.tab != active_tab_) {
    active_tab_ = next.tab;
    selected_stack_ = 0;
    currency_scroll_ = 0;
    MarkActiveTabSeen();
  }
}

void InventoryPanel::MarkActiveTabSeen() {
  std::string key = TabKey(active_tab_, character_);
  if (!key.empty()) {
    account_.MarkSeen(key);
  }
}

bool InventoryPanel::on_stackable_tab() const {
  return active_tab_ == kEtcTab;
}

int InventoryPanel::EtcRowCount() const {
  return static_cast<int>(character_.stackables().size());
}

int InventoryPanel::selected_stack() const {
  bool in_range = selected_stack_ >= 0 && selected_stack_ < EtcRowCount();
  return in_range ? selected_stack_ : -1;
}

int InventoryPanel::menu_column() const {
  // The border, then the row up to the end of the slot cell: the caret, the
  // name and the slot, with the gaps in front of each.
  ItemColumns columns = Columns();
  return 1 + kItemListCursor + columns.name_width + kItemCellGap +
         columns.Width(ItemColumn::kSlot) + kItemCellGap;
}

bool InventoryPanel::on_shop_tab() const {
  return active_tab_ == kShopTab;
}

bool InventoryPanel::on_bank_tab() const {
  return active_tab_ == kBankTab;
}

bool InventoryPanel::ActiveTabEmpty() const {
  if (on_expand_) {
    return true;  // a door has nothing under it to walk down into
  }
  if (active_tab_ == kEquipTab) {
    return character_.inventory().size() == 0;
  }
  if (active_tab_ == kShopTab || active_tab_ == kBankTab) {
    // Nothing of the player's to descend into; Enter goes through the door.
    return true;
  }
  if (active_tab_ == kTokenTab) {
    // A balance sheet rather than a list: there is nothing on it to act on,
    // so the cursor stays up on the bar.
    return true;
  }
  return EtcRowCount() == 0;
}

int InventoryPanel::ListCount() const {
  if (on_expand_) {
    return 0;
  }
  if (active_tab_ == kEquipTab) {
    return character_.inventory().size();
  }
  if (active_tab_ == kShopTab || active_tab_ == kBankTab ||
      active_tab_ == kTokenTab) {
    return 0;
  }
  return EtcRowCount();
}

int InventoryPanel::CursorStop() const {
  if (zone_ == kZoneTabs) {
    return 0;
  }
  return (active_tab_ == kEquipTab ? selected_ : selected_stack_) + 1;
}

void InventoryPanel::MoveCursor(int delta) {
  int next = StepCursor(CursorStop(), delta, 1 + ListCount());
  if (next == 0) {
    zone_ = kZoneTabs;
    return;
  }
  zone_ = kZoneList;
  if (active_tab_ == kEquipTab) {
    selected_ = next - 1;
  } else {
    selected_stack_ = next - 1;
  }
}

void InventoryPanel::SortActiveTab() {
  if (active_tab_ == kEquipTab) {
    character_.SortEquipTab();
  } else if (active_tab_ == kEtcTab) {
    character_.SortStackTab();
  }
  // Nothing for the Token tab: the purse re-files itself on every change, so
  // its sheet is never out of order to begin with.
}

ftxui::Element InventoryPanel::RenderExpandTab(bool row_selected) const {
  // Its label is the state Enter would leave the bag in, the way the buff
  // switch reads. Drawn as its own layer rather than as another chip of the
  // bar, so it keeps the far right past the meso counter.
  return TabChip(expanded_ ? "Close" : "Expand", on_expand_, row_selected);
}

// The Etc {Sell, Close} menu, for whatever stack the cursor is on.
void InventoryPanel::OpenStackMenu() {
  sell_menu_.Reset();
  // Multi-Sell arrives with the shop: it sells across the whole bag, and the
  // shelf a mis-sale is undone at is the shop's. Selling one stack has never
  // waited for it.
  if (!Unlocked(Feature::kShop, character_, account_)) {
    sell_menu_.Hide(kStackMultiSell);
  }
  if (selected_stack() < 0) {
    sell_menu_.Disable(kStackSell);
    sell_menu_.Disable(kStackMultiSell);
  }
}

// What the Equip tab's menu offers on a spare Arcane Symbol. Neither upgrade
// path touches one, and Equip and Combine trade places: only one of each area
// is ever worn, so a second copy has nowhere to go but into the first.
void InventoryPanel::OpenSymbolMenu(const EquipInstance& symbol) {
  menu_.Hide(kMenuScroll);
  menu_.Hide(kMenuHammer);
  menu_.Hide(kMenuStarForce);
  menu_.Hide(kMenuCube);
  if (character_.equipped().count(symbol.prototype().equip_slot()) > 0) {
    menu_.Hide(kMenuAction);
  } else {
    menu_.Hide(kMenuCombine);
    if (!character_.CanEquip(symbol.prototype())) {
      menu_.Disable(kMenuAction);
    }
  }
}

// The Equip tab's menu, for the item or trace the cursor is on.
void InventoryPanel::HideLockedFeatures() {
  if (!Unlocked(Feature::kScrolling, character_, account_)) {
    menu_.Hide(kMenuScroll);
  }
  if (!Unlocked(Feature::kHammer, character_, account_)) {
    menu_.Hide(kMenuHammer);
  }
  if (!Unlocked(Feature::kStarForce, character_, account_)) {
    menu_.Hide(kMenuStarForce);
  }
  if (!Unlocked(Feature::kPotential, character_, account_)) {
    menu_.Hide(kMenuCube);
  }
  // Recovery has no level of its own: owning a trace already means an item
  // exploded at the 16th star, so the ITEM is the gate. Selling arrives with
  // the shop, and takes no gold -- the Shop tab lighting up says it.
  if (!Unlocked(Feature::kShop, character_, account_)) {
    menu_.Hide(kMenuSell);
    menu_.Hide(kMenuMultiSell);
  }
}

// What the item refuses is the prototype's answer, so armour and weapons carry
// the same entries however far along a particular drop is.
void InventoryPanel::HideRefusedUpgrades(const EquipInstance& equip) {
  // Nowhere to go is as good a reason to grey Equip as a level too low, and
  // an item naming no slot at all is the one thing that has nowhere.
  if (!character_.CanEquip(equip.prototype()) ||
      character_.SlotToFill(equip.prototype()) == EQUIP_SLOT_UNSPECIFIED) {
    menu_.Disable(kMenuAction);
  }
  if (!Supports(equip.prototype(), UPGRADE_SCROLL)) {
    menu_.Hide(kMenuScroll);
  }
  // A hammer widens a shelf; it cannot build one. On a piece with no slots to
  // begin with there is nothing for it to do, so it is not on the menu.
  if (!TakesUpgradeSlots(equip.prototype())) {
    menu_.Hide(kMenuHammer);
  } else if (!equip.CanHammer()) {
    // Greyed, not gone: both hammers are in, and a row that vanished at the
    // second one would read as the feature going away.
    menu_.Disable(kMenuHammer);
  }
  // Where a piece is worn is what decides whether a cube goes into it, and the
  // slots that refuse one -- the medal, the badge, the pocket -- refuse it for
  // good.
  if (!equip.CanCube()) {
    menu_.Hide(kMenuCube);
  }
  if (!Supports(equip.prototype(), UPGRADE_STAR_FORCE)) {
    menu_.Hide(kMenuStarForce);
  } else if (!equip.CanStarForce()) {
    // Greyed, not gone: stars come after the slots are spent, and a row that
    // stands there dim is how the player learns the order.
    menu_.Disable(kMenuStarForce);
  }
}

// Gold on an upgrade the player has been handed but never used, which is where
// the trail from the level-up card ends.
void InventoryPanel::HighlightUnusedUpgrades() {
  if (LeadToAction(Feature::kScrolling, character_, account_)) {
    menu_.Highlight(kMenuScroll);
  }
  if (LeadToAction(Feature::kHammer, character_, account_)) {
    menu_.Highlight(kMenuHammer);
  }
  if (LeadToAction(Feature::kStarForce, character_, account_)) {
    menu_.Highlight(kMenuStarForce);
  }
  if (LeadToAction(Feature::kPotential, character_, account_)) {
    menu_.Highlight(kMenuCube);
  }
}

void InventoryPanel::OpenEquipMenu() {
  menu_.Reset();
  HideLockedFeatures();
  const EquipInstance* eq = character_.inventory().equip_instance(selected_);
  if (eq == nullptr) {
    // Traces can only be inspected or recovered.
    menu_.Disable(kMenuAction);
    menu_.Hide(kMenuCombine);
    menu_.Hide(kMenuScroll);
    menu_.Hide(kMenuHammer);
    menu_.Hide(kMenuStarForce);
    menu_.Hide(kMenuCube);
    return;
  }
  menu_.Hide(kMenuRecover);  // live items cannot be recovered
  if (IsArcaneSymbol(eq->prototype())) {
    OpenSymbolMenu(*eq);
    return;
  }
  menu_.Hide(kMenuCombine);
  HideRefusedUpgrades(*eq);
  HighlightUnusedUpgrades();
}

void InventoryPanel::OpenTabMenu() {
  tab_menu_.Reset();
}

void InventoryPanel::OpenMenu() {
  if (active_tab_ == kEquipTab) {
    OpenEquipMenu();
  } else {
    OpenStackMenu();
  }
}

Screen InventoryPanel::OnTabMenuEvent(ftxui::Event event) {
  if (IsBack(event)) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    tab_menu_.Up();
    return kItemMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    tab_menu_.Down();
    return kItemMenu;
  }
  if (IsForward(event)) {
    if (tab_menu_.selected() == kTabMenuSort) {
      SortActiveTab();
    }
    return kMain;
  }
  return kItemMenu;
}

Screen InventoryPanel::OnStackMenuEvent(ftxui::Event event) {
  if (IsBack(event)) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    sell_menu_.Up();
    return kItemMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    sell_menu_.Down();
    return kItemMenu;
  }
  if (!IsForward(event)) {
    return kItemMenu;
  }
  if (sell_menu_.selected() == kStackInspect) {
    return kItemInspect;
  }
  if (sell_menu_.selected() == kStackSell) {
    return kSell;
  }
  if (sell_menu_.selected() == kStackMultiSell) {
    return kMultiSell;
  }
  return kMain;
}

Screen InventoryPanel::OnEquipMenuEvent(ftxui::Event event,
                                        ScrollPanel& scroll_panel,
                                        StatPreset gear) {
  if (IsBack(event)) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kItemMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kItemMenu;
  }
  if (!IsForward(event)) {
    return kItemMenu;
  }
  if (menu_.selected() == kMenuAction) {
    character_.Equip(selected_, gear);
    return kMain;
  }
  if (menu_.selected() == kMenuInspect) {
    return kInspect;
  }
  if (menu_.selected() == kMenuCombine) {
    return kSymbolCombine;
  }
  if (menu_.selected() == kMenuScroll) {
    // Followed whether or not there is a scroll to show: they pressed the
    // entry, which is what the gold was asking them to do.
    FollowedToAction(Feature::kScrolling, account_);
    if (scroll_panel.SetFilterForPrototype(
            character_.inventory()[selected_].prototype())) {
      return kScrollSelect;
    }
  }
  if (menu_.selected() == kMenuHammer) {
    FollowedToAction(Feature::kHammer, account_);
    return kHammer;
  }
  if (menu_.selected() == kMenuStarForce) {
    FollowedToAction(Feature::kStarForce, account_);
    return kStarForce;
  }
  if (menu_.selected() == kMenuCube) {
    FollowedToAction(Feature::kPotential, account_);
    return kCubing;
  }
  if (menu_.selected() == kMenuRecover) {
    return kTraceRecover;
  }
  if (menu_.selected() == kMenuSell) {
    return kSellEquip;
  }
  if (menu_.selected() == kMenuMultiSell) {
    return kMultiSell;
  }
  return kMain;
}

Screen InventoryPanel::OnMenuEvent(ftxui::Event event,
                                   ScrollPanel& scroll_panel, StatPreset gear) {
  return active_tab_ == kEquipTab ? OnEquipMenuEvent(event, scroll_panel, gear)
                                  : OnStackMenuEvent(event);
}

ItemColumns InventoryPanel::Columns() const {
  ItemListOptions options;
  options.bag = true;
  options.scrolling = Unlocked(Feature::kScrolling, character_, account_);
  options.star_force = Unlocked(Feature::kStarForce, character_, account_);
  options.potential = Unlocked(Feature::kPotential, character_, account_);
  // Less the two borders: the width the panel was given is the column's, and
  // the list is drawn inside it.
  return FitItemColumns(width_ - 2, options);
}

ftxui::Element InventoryPanel::RenderOwnEquipList(ftxui::Component menu) {
  ItemColumns columns = Columns();
  rows_ = BuildEquipRows(character_, character_.inventory(), selected_,
                         name_clock_.Elapsed(), columns);
  entries_.clear();
  for (const InventoryRowState& row : rows_) {
    entries_.push_back(row.label.text);
  }
  if (entries_.empty()) {
    return ftxui::vbox({EmptyState("empty", /*gutter=*/2), ftxui::filler()});
  }
  selected_ = std::min(selected_, character_.inventory().size() - 1);
  return ftxui::vbox({
      EquipHeader(columns),
      PanelSeparator(highlighted_),
      // Only the items scroll; the header row and the rule stay put.
      // ftxui::Menu marks its selected entry, which is what the frame scrolls
      // to, so the cursor cannot walk out of view.
      menu->Render() | ftxui::vscroll_indicator | ftxui::yframe | ftxui::flex,
  });
}

int InventoryPanel::CurrencyRowCount() const {
  const CurrencyPurse& purse = character_.currencies();
  return static_cast<int>(
      std::max(CurrenciesOf(purse, ITEM_KIND_TOKEN).size(),
               CurrenciesOf(purse, ITEM_KIND_SOUL_SHARD).size()));
}

int InventoryPanel::CurrencySheetHeight() const {
  // One frame behind, which is right: a key pressed now scrolls the sheet the
  // player is looking at. One row until the sheet has been drawn once.
  return std::max(1, sheet_box_.y_max - sheet_box_.y_min + 1);
}

void InventoryPanel::ScrollCurrencySheet(int delta) {
  int last = std::max(0, CurrencyRowCount() - CurrencySheetHeight());
  currency_scroll_ = std::max(0, std::min(last, currency_scroll_ + delta));
}

ftxui::Element InventoryPanel::RenderCurrencySheet() {
  const std::vector<CurrencyAmount>& held = character_.currencies().entries();
  std::vector<int> tokens =
      CurrenciesOf(character_.currencies(), ITEM_KIND_TOKEN);
  std::vector<int> shards =
      CurrenciesOf(character_.currencies(), ITEM_KIND_SOUL_SHARD);
  int count = CurrencyRowCount();
  if (count == 0) {
    // No headings over nothing, as on an empty Equip or Etc tab: column names
    // are there to tell rows apart, and there are no rows to tell apart.
    return ftxui::vbox({EmptyState("empty", /*gutter=*/2), ftxui::filler()});
  }
  int height = CurrencySheetHeight();
  // The tab can lose rows while it is open -- the last of a token spent at the
  // shop -- so the offset is held to what there is to show.
  currency_scroll_ = std::max(0, std::min(count - height, currency_scroll_));
  std::vector<ftxui::Element> above;
  std::vector<ftxui::Element> window;
  std::vector<ftxui::Element> below;
  // As long as the taller column: the two run out at different heights, and
  // the shorter one simply leaves its half of the row blank.
  for (int i = 0; i < count; ++i) {
    ftxui::Element row = RenderCurrencyRow(
        i < static_cast<int>(tokens.size()) ? &held[tokens[i]] : nullptr,
        i < static_cast<int>(shards.size()) ? &held[shards[i]] : nullptr);
    std::vector<ftxui::Element>& part =
        i < currency_scroll_ ? above
                             : (i < currency_scroll_ + height ? window : below);
    part.push_back(std::move(row));
  }
  // A frame scrolls to what is focused and centres it, so the rows that should
  // be on screen are handed over as one block. The focus is the SCROLL
  // POSITION here, not a selection.
  ftxui::Element body = ftxui::vbox({
      ftxui::vbox(std::move(above)),
      ftxui::vbox(std::move(window)) | ftxui::focus,
      ftxui::vbox(std::move(below)),
  });
  return ftxui::vbox({
      CurrencyHeader(),
      PanelSeparator(highlighted_),
      // Only the rows scroll; the header and its rule stay put.
      std::move(body) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::reflect(sheet_box_) | ftxui::flex,
  });
}

ftxui::Element InventoryPanel::RenderContent(ftxui::Component menu) {
  // A list that emptied under the cursor has no row left to stand on, so the
  // cursor returns to the tab bar. Left where it was, no highlight anywhere
  // would say where the keys go.
  if (zone_ == kZoneList && ActiveTabEmpty()) {
    zone_ = kZoneTabs;
  }
  bool focused = panel_focus_ == kInventoryPanel;
  // The tab rides in the key beside the row, so the same row of another tab
  // counts as a different name and starts from its own head.
  name_clock_.Follow(
      active_tab_ * kNameClockTabStride +
          (active_tab_ == kEquipTab ? selected_ : selected_stack_),
      focused && zone_ == kZoneList);
  ftxui::Element body;
  if (on_expand_) {
    // A door rather than a page, so where the other tabs list what the player
    // has, this one says how to go through it.
    body = ftxui::vbox({CenteredRow(expanded_ ? "Hit Enter to close Inventory"
                                              : "Hit Enter to fullscreen "
                                                "Inventory"),
                        ftxui::filler()});
  } else if (active_tab_ == kShopTab || active_tab_ == kBankTab) {
    // Both are screens of their own, so where the other tabs list what the
    // player has, these say how to get there. Over a filler because the
    // window is taller than this one line and the line belongs at the top.
    body = ftxui::vbox({CenteredRow(std::string("Hit Enter to open ") +
                                    kInventoryTabLabels[active_tab_]),
                        ftxui::filler()});
  } else if (active_tab_ == kTokenTab) {
    body = RenderCurrencySheet();
  } else if (active_tab_ == kEtcTab) {
    // Keep the cursor in range as stacks are sold off.
    selected_stack_ = std::min(selected_stack_, std::max(0, EtcRowCount() - 1));
    // The stack cursor shows only while the list zone holds focus, so it never
    // competes with the white tab-bar highlight.
    body = RenderStackList(character_.stackables(), AllRows(EtcRowCount()),
                           selected_stack_, focused && zone_ == kZoneList,
                           cursor_box_, highlighted_, name_clock_.Elapsed());
  } else {
    body = RenderOwnEquipList(menu);
  }
  // -1 while the cursor is out on Expand, so the highlight is in one place.
  int active = -1;
  std::vector<TabSpec> specs;
  for (int tab : VisibleTabs()) {
    if (!on_expand_ && tab == active_tab_) {
      active = static_cast<int>(specs.size());
    }
    // Asking Seen("") would answer no and leave those tabs gold forever.
    std::string key = TabKey(tab, character_);
    specs.push_back(
        {kInventoryTabLabels[tab], !key.empty() && !account_.Seen(key)});
  }
  return AccentWindow(
      " Inventory ",
      ftxui::vbox(
          {RenderBagTabBar(specs, active,
                           RenderBalances(character_.meso(),
                                          character_.CountItem(kSpellTraceName),
                                          character_, account_),
                           focused && zone_ == kZoneTabs, highlighted_,
                           RenderExpandTab(focused && zone_ == kZoneTabs),
                           width_ - 2, bar_box_),
           std::move(body) | ftxui::flex}),
      PanelAccent(highlighted_), focused, account_.panel_title_blink());
}

ftxui::Element InventoryPanel::RenderRow(const ftxui::EntryState& state) {
  int idx = state.index;
  // Drawn from the panel's OWN cursor rather than ftxui's focused entry, which
  // moves only when the Menu handles the key: the two jumps the panel makes
  // for it are exactly the two the Menu never sees, so the caret went missing
  // on arrival from the bar. The conditions after it are the Etc list's.
  bool on_cursor =
      idx == selected_ && zone_ == kZoneList && panel_focus_ == kInventoryPanel;
  if (idx < 0 || idx >= static_cast<int>(rows_.size())) {
    return ftxui::text((on_cursor ? "> " : "  ") + state.label);
  }
  ftxui::Element row = RenderEquipRow(rows_[idx], on_cursor);
  if (idx == selected_) {
    // Records where the highlighted row landed, so the item menu can open
    // beside it.
    row = std::move(row) | ftxui::reflect(cursor_box_);
  }
  return row;
}

bool InventoryPanel::OnTabBarEvent(const ftxui::Event& event,
                                   const std::function<void()>& on_enter,
                                   const std::function<void()>& on_expand) {
  // Left/Right switch tabs; Up and Down step into the list, the bar being a
  // stop in the same ring as the rows. A tab with nothing under it is a ring
  // of one, so both keys leave the cursor where it is.
  if (event == ftxui::Event::ArrowLeft) {
    StepTab(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    StepTab(+1);
    return true;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    int delta = event == ftxui::Event::ArrowUp ? -1 : 1;
    // The Token tab has no row to step down onto, so the keys that would walk
    // a list scroll the sheet under the bar instead.
    if (active_tab_ == kTokenTab && !on_expand_) {
      ScrollCurrencySheet(delta);
      return true;
    }
    MoveCursor(delta);
    return true;
  }
  if (IsForward(event)) {
    // Expand and Shop are doors: Enter goes through rather than raising a menu
    // to ask about a page there is no list under.
    if (on_expand_) {
      if (on_expand != nullptr) {
        if (!expanded_) {
          // The door is not a page, so the wide bag opens on the first tab
          // rather than on the button that would close it again. One step
          // right is exactly that, the bar being a ring.
          StepTab(+1);
        }
        on_expand();
      }
    } else {
      on_enter();
    }
    return true;
  }
  // Swallow the rest, or it leaks to the hidden Equip menu and silently moves
  // its selection while the tab bar holds focus.
  return true;
}

bool InventoryPanel::OnStackListEvent(const ftxui::Event& event,
                                      const std::function<void()>& on_enter) {
  // Etc: the whole ring is ours to walk, there being no ftxui::Menu under
  // these tabs. Navigation is swallowed either way, so the hidden Equip menu
  // stays put.
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsForward(event)) {
    if (ListCount() > 0) {
      on_enter();  // the {Sell, Close} menu
    }
    return true;
  }
  return false;
}

bool InventoryPanel::OnEquipListEvent(const ftxui::Event& event,
                                      const std::function<void()>& on_enter) {
  // Take the two ends of the list and leave everything between them to the
  // ftxui::Menu, which scrolls the view to follow its own cursor and would
  // stop doing so if its keys were taken away.
  bool up = event == ftxui::Event::ArrowUp;
  bool down = event == ftxui::Event::ArrowDown;
  if ((up && selected_ == 0) || (down && selected_ >= ListCount() - 1)) {
    MoveCursor(up ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::Character(' ')) {
    on_enter();
    return true;
  }
  return false;
}

ftxui::Component InventoryPanel::MakeComponent(
    std::function<void()> on_enter, std::function<void()> on_expand) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  // Here rather than at entry-generation time: ftxui::Menu takes only
  // std::string* entries, and this is the one hook that can return a coloured
  // Element. It also suppresses the default inversion.
  opt.entries_option.transform = [this](ftxui::EntryState state) {
    return RenderRow(state);
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // Focusable whether or not the list has rows: Container::Tab drops every key
  // when its active panel says it is not, and an ftxui::Menu says no on an
  // empty list -- which would take the tab bar down with it.
  ftxui::Component renderer = AlwaysFocusable(ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); }));
  return ftxui::CatchEvent(renderer,
                           [this, on_enter, on_expand](ftxui::Event event) {
                             if (zone_ == kZoneTabs) {
                               return OnTabBarEvent(event, on_enter, on_expand);
                             }
                             if (active_tab_ != kEquipTab) {
                               return OnStackListEvent(event, on_enter);
                             }
                             return OnEquipListEvent(event, on_enter);
                           });
}

}  // namespace ms
