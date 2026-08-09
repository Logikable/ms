#include "src/frontend/panels/inventory_panel.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

enum InventoryTab : int {
  kEquipTab = 0,
  kUseTab = 1,
  kEtcTab = 2,
  // Not a list of anything the player owns -- it is the door to the shop, and
  // sits last because it is the only tab that leaves this panel.
  kShopTab = 3,
  kNumInventoryTabs = 4,
};

// Two leading spaces match the "  " / "> " cursor added by the entry transform.
constexpr char kColumnHeader[] =
    "  Name                      "  // 2 cursor + 26 name
    "  Equip Slot"                  // 2 sep + 10 slot
    "  Level  Job          "        // 2 sep + 20 info
    "  Scrolls";                    // 2 sep + label
// Sub-header row: 64 spaces align "Pass/Left/Restore" under the scroll column.
constexpr char kColumnHeader2[] =
    "                              "  // 30
    "                              "  // 30
    "    "                            // 4 → 64 total
    "Pass/Left/Restore";

constexpr const char* kTabLabels[kNumInventoryTabs] = {"Equip", "Use", "Etc",
                                                       "Shop"};

// Renders the left-aligned chip row in the shared tab style, with a centered
// meso counter overlaid in the empty space, over a separator. `tabs` is what
// the character has unlocked, so a locked tab leaves no gap behind it.
ftxui::Element RenderTabBar(const std::vector<int>& tabs, int active_tab,
                            int64_t meso, bool row_selected,
                            const CharacterInstance& character,
                            bool highlighted) {
  std::vector<ftxui::Element> chips;
  for (int tab : tabs) {
    // Only the shop is ever new: the three bag tabs have been there since the
    // first frame of the game.
    bool unseen = tab == kShopTab && !character.TabSeen(kShopTabKey);
    chips.push_back(
        TabChip(kTabLabels[tab], tab == active_tab, row_selected, unseen));
  }
  ftxui::Element tab_row = ftxui::dbox({
      ftxui::hbox(std::move(chips)),
      ftxui::text(FormatMeso(meso)) | ftxui::color(kTheme) | ftxui::hcenter,
  });
  return ftxui::vbox({
      std::move(tab_row),
      PanelSeparator(highlighted),
  });
}

// Room for any row index under one tab, so folding the tab and the row into
// one key cannot make two different selections collide.
constexpr int kNameClockTabStride = 4096;

// Renders a Name/Quantity list of `stacks`, one row per stack, with a "> "
// cursor on the `selected`-th row. An empty tab is just "(empty)", with no
// column header over it. The cursor is drawn only when `focused`, matching the
// Equip tab, whose menu takes its cursor from ftxui's own focus state.
ftxui::Element RenderStackList(const std::vector<StackableItem>& stacks,
                               int selected, bool focused,
                               ftxui::Box& cursor_box, bool highlighted,
                               std::chrono::steady_clock::duration elapsed) {
  if (stacks.empty()) {
    // No header over nothing, as on an empty Equip tab. Column names are there
    // to tell rows apart, and there are no rows to tell apart.
    return ftxui::vbox({EmptyState("empty", /*gutter=*/2), ftxui::filler()});
  }
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(stacks.size()); ++i) {
    std::string cursor = "  ";
    if (focused && i == selected) {
      cursor = "> ";
    }
    ftxui::Element row = ftxui::text(
        cursor +
        ScrollingWindow(stacks[i].name(), kItemNameWidth,
                        i == selected
                            ? elapsed
                            : std::chrono::steady_clock::duration::zero()) +
        std::to_string(stacks[i].count()));
    if (i == selected) {
      // What the frame scrolls to. These rows are plain text rather than an
      // ftxui::Menu, so nothing else marks the cursor and the list would
      // happily scroll away from it. Marked whether or not the panel holds
      // focus, so the view does not jump when focus comes back.
      //
      // Reflected as well, so the item menu knows the row to open beside.
      row = std::move(row) | ftxui::focus | ftxui::reflect(cursor_box);
    }
    rows.push_back(std::move(row));
  }
  return ftxui::vbox({
      ftxui::text("  " + PadRight("Name", 26) + "Quantity"),
      PanelSeparator(highlighted),
      // Only the rows scroll; the header and its rule stay put.
      ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::flex,
  });
}

}  // namespace

InventoryPanel::InventoryPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Equip", "Inspect", "Scroll", "Star Force", "Recover", "Close"}),
      sell_menu_({"Inspect", "Use", "Sell", "Close"}) {
}

ItemMenu& InventoryPanel::menu() {
  if (active_tab_ == kEquipTab) {
    return menu_;
  }
  return sell_menu_;
}

std::vector<int> InventoryPanel::VisibleTabs() const {
  std::vector<int> tabs = {kEquipTab, kUseTab, kEtcTab};
  // The shop is a place in the world rather than a page of the bag, and it is
  // not open to a character who has nothing to spend and nothing to spend it
  // on. Until then the bar simply ends at Etc.
  if (Unlocked(Feature::kShop, character_)) {
    tabs.push_back(kShopTab);
  }
  return tabs;
}

void InventoryPanel::StepTab(int direction) {
  std::vector<int> tabs = VisibleTabs();
  std::vector<int>::iterator at =
      std::find(tabs.begin(), tabs.end(), active_tab_);
  if (at == tabs.end()) {
    // The tab the cursor was on has been locked away under it. Nothing does
    // that today -- levels only go up -- but landing on Equip beats landing
    // on a tab that is no longer in the bar.
    active_tab_ = kEquipTab;
    return;
  }
  int next = static_cast<int>(at - tabs.begin()) + direction;
  if (next < 0 || next >= static_cast<int>(tabs.size())) {
    return;  // the ends of the bar are walls, not wrapping points
  }
  active_tab_ = tabs[next];
  selected_stack_ = 0;
  // Opened it, so it stops announcing itself. The shop is the only tab here
  // that ever does.
  if (active_tab_ == kShopTab) {
    character_.MarkTabSeen(kShopTabKey);
  }
}

bool InventoryPanel::on_stackable_tab() const {
  return active_tab_ == kUseTab || active_tab_ == kEtcTab;
}

ItemCategory InventoryPanel::active_category() const {
  if (active_tab_ == kUseTab) {
    return ITEM_CATEGORY_USE;
  }
  if (active_tab_ == kEtcTab) {
    return ITEM_CATEGORY_ETC;
  }
  return ITEM_CATEGORY_UNSPECIFIED;
}

bool InventoryPanel::on_shop_tab() const {
  return active_tab_ == kShopTab;
}

bool InventoryPanel::ActiveTabEmpty() const {
  if (active_tab_ == kEquipTab) {
    return character_.inventory().size() == 0;
  }
  if (active_tab_ == kShopTab) {
    // Nothing of the player's to descend into; Enter leaves for the shop.
    return true;
  }
  ItemCategory category =
      active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
  return character_.stackables(category).empty();
}

int InventoryPanel::ListCount() const {
  if (active_tab_ == kEquipTab) {
    return character_.inventory().size();
  }
  if (active_tab_ == kShopTab) {
    return 0;
  }
  return static_cast<int>(character_.stackables(active_category()).size());
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

// The Use/Etc {Sell, Close} menu, for whatever stack the cursor is on.
void InventoryPanel::OpenStackMenu() {
  sell_menu_.Reset();
  ItemCategory category =
      active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
  // Nothing on the Etc tab is usable, so the entry is not there at all -- Etc
  // is where drops and quest pieces sit, not where anything is drunk.
  if (category == ITEM_CATEGORY_ETC) {
    sell_menu_.Hide(kStackUse);
  }
  const std::vector<StackableItem>& stacks = character_.stackables(category);
  if (selected_stack_ >= static_cast<int>(stacks.size())) {
    sell_menu_.Disable(kStackUse);
    sell_menu_.Disable(kStackSell);
    return;
  }
  const ItemPrototype& proto = stacks[selected_stack_].prototype();
  // Disabled rather than hidden, on the tab where using things is what the
  // player came to do: a greyed row is the answer to "can I drink this?".
  if (proto.effect() == ITEM_EFFECT_UNSPECIFIED) {
    sell_menu_.Disable(kStackUse);
  }
  if (proto.sell_price() <= 0) {
    sell_menu_.Disable(kStackSell);
  }
}

// The Equip tab's menu, for the item or trace the cursor is on.
void InventoryPanel::OpenEquipMenu() {
  menu_.Reset();
  // What the player has not reached yet is not drawn at all. This comes first:
  // what is hidden here is what they cannot do to any item, and what is
  // disabled below is what this item cannot do.
  if (!Unlocked(Feature::kScrolling, character_)) {
    menu_.Hide(kMenuScroll);
  }
  if (!Unlocked(Feature::kStarForce, character_)) {
    menu_.Hide(kMenuStarForce);
  }
  if (!Unlocked(Feature::kRecovery, character_)) {
    menu_.Hide(kMenuRecover);
  }
  // An upgrade this item cannot take is not drawn either. A greyed row invites
  // the player to press it and find out why; on this menu the answer is always
  // "not to this item", which is worth no row at all.
  const EquipInstance* eq = character_.inventory().equip_instance(selected_);
  if (eq == nullptr) {
    // Traces can only be inspected or recovered.
    menu_.Disable(kMenuAction);
    menu_.Hide(kMenuScroll);
    menu_.Hide(kMenuStarForce);
    return;
  }
  menu_.Hide(kMenuRecover);  // live items cannot be recovered
  if (!character_.CanEquip(eq->prototype())) {
    menu_.Disable(kMenuAction);
  }
  // Scrolling asks the prototype, not the slot count: a weapon with every slot
  // spent still takes a Clean Slate, so only an item that refuses scrolls
  // outright loses the entry.
  if (!Supports(eq->prototype(), UPGRADE_SCROLL)) {
    menu_.Hide(kMenuScroll);
  }
  if (!eq->CanStarForce()) {
    menu_.Hide(kMenuStarForce);
  }
  // Gold on an upgrade the player has been handed but never used, which is
  // where the trail from the level-up card ends. Last, so it lands on the
  // entries as they finally stand.
  if (LeadToAction(Feature::kScrolling, character_)) {
    menu_.Highlight(kMenuScroll);
  }
  if (LeadToAction(Feature::kStarForce, character_)) {
    menu_.Highlight(kMenuStarForce);
  }
}

void InventoryPanel::OpenMenu() {
  if (active_tab_ == kEquipTab) {
    OpenEquipMenu();
  } else {
    OpenStackMenu();
  }
}

Screen InventoryPanel::OnMenuEvent(ftxui::Event event,
                                   ScrollPanel& scroll_panel) {
  if (active_tab_ != kEquipTab) {
    // Use/Etc {Sell, Close} menu.
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
    if (IsForward(event)) {
      if (sell_menu_.selected() == kStackInspect) {
        return kItemInspect;
      }
      if (sell_menu_.selected() == kStackUse) {
        character_.UseStackable(active_category(), selected_stack_);
        return kMain;
      }
      if (sell_menu_.selected() == kStackSell) {
        return kSell;
      }
      return kMain;
    }
    return kItemMenu;
  }
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
  if (IsForward(event)) {
    if (menu_.selected() == kMenuAction) {
      character_.Equip(selected_);
      return kMain;
    }
    if (menu_.selected() == kMenuInspect) {
      return kInspect;
    }
    if (menu_.selected() == kMenuScroll) {
      // Followed whether or not there is a scroll to show: they pressed the
      // entry, which is what the gold was asking them to do.
      FollowedToAction(Feature::kScrolling, character_);
      if (scroll_panel.SetFilterForPrototype(
              character_.inventory()[selected_].prototype())) {
        return kScrollSelect;
      }
    }
    if (menu_.selected() == kMenuStarForce) {
      FollowedToAction(Feature::kStarForce, character_);
      return kStarForce;
    }
    if (menu_.selected() == kMenuRecover) {
      return kTraceRecover;
    }
    return kMain;
  }
  return kItemMenu;
}

ftxui::Element InventoryPanel::RenderEquipList(ftxui::Component menu) {
  rows_.clear();
  entries_.clear();
  for (int i = 0; i < character_.inventory().size(); ++i) {
    const EquipTabItem& item = character_.inventory()[i];
    const EquipPrototype& proto = item.prototype();
    int level = proto.required_level() > 0 ? proto.required_level() : 1;
    std::string info = "Lv" + PadRight(std::to_string(level), 3) + "  " +
                       FormatJobCategories(proto);
    int scroll_pass = -1, scroll_left = -1, scroll_restore = -1;
    if (proto.upgrade_slots() > 0) {
      scroll_pass = item.equip_state().scroll_successes();
      scroll_left = item.equip_state().remaining_upgrade_slots();
      scroll_restore = proto.upgrade_slots() - scroll_pass - scroll_left;
    }
    InventoryRowState row;
    // Only the selected row's name slides; the rest sit at their heads.
    row.label = FormatItemEntry(
        item.name(), proto.equip_slot(), info, scroll_pass, scroll_left,
        scroll_restore,
        i == selected_ ? name_clock_.Elapsed()
                       : std::chrono::steady_clock::duration::zero());
    row.is_trace = character_.inventory().equip_instance(i) == nullptr;
    row.level_ok = character_.MeetsLevel(proto);
    row.job_ok = character_.MeetsJob(proto);
    entries_.push_back(row.label);
    rows_.push_back(std::move(row));
  }
  if (!entries_.empty()) {
    selected_ = std::min(selected_, character_.inventory().size() - 1);
  }
  if (entries_.empty()) {
    return ftxui::vbox({EmptyState("empty", /*gutter=*/2), ftxui::filler()});
  }
  return ftxui::vbox({
      ftxui::text(kColumnHeader),
      ftxui::text(kColumnHeader2),
      PanelSeparator(highlighted_),
      // Only the items scroll; the two header rows and the rule stay put.
      // ftxui::Menu marks its selected entry, which is what the frame scrolls
      // to, so the cursor cannot walk out of view.
      menu->Render() | ftxui::vscroll_indicator | ftxui::yframe | ftxui::flex,
  });
}

ftxui::Element InventoryPanel::RenderContent(ftxui::Component menu) {
  // A list that emptied under the cursor -- the last equip worn, the last
  // stack sold -- has no row left to stand on, so the cursor comes back up to
  // the tab bar. Left where it was it would be in a zone that cannot draw it,
  // with no highlight anywhere on the panel to say which keys go where.
  if (zone_ == kZoneList && ActiveTabEmpty()) {
    zone_ = kZoneTabs;
  }
  bool focused = panel_focus_ == kInventoryPanel;
  // The tab rides in the key beside the row, so the same row of another tab
  // counts as a different name and starts from its own head.
  name_clock_.Follow(active_tab_ * kNameClockTabStride +
                     (active_tab_ == kEquipTab ? selected_ : selected_stack_));
  ftxui::Element body;
  if (active_tab_ == kShopTab) {
    // The shop is a screen of its own, so where the other tabs list what the
    // player has, this one says how to get there. Over a filler because the
    // window is taller than this one line and the line belongs at the top.
    body =
        ftxui::vbox({CenteredRow("Hit Enter to open Shop"), ftxui::filler()});
  } else if (active_tab_ == kUseTab || active_tab_ == kEtcTab) {
    ItemCategory category =
        active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
    const std::vector<StackableItem>& stacks = character_.stackables(category);
    // Keep the cursor in range as stacks are sold off.
    selected_stack_ = std::min(
        selected_stack_, std::max(0, static_cast<int>(stacks.size()) - 1));
    // The stack cursor shows only while the list zone holds focus, so it never
    // competes with the white tab-bar highlight.
    body =
        RenderStackList(stacks, selected_stack_, focused && zone_ == kZoneList,
                        cursor_box_, highlighted_, name_clock_.Elapsed());
  } else {
    body = RenderEquipList(menu);
  }
  return AccentWindow(
      " Inventory ",
      ftxui::vbox({RenderTabBar(VisibleTabs(), active_tab_, character_.meso(),
                                focused && zone_ == kZoneTabs, character_,
                                highlighted_),
                   std::move(body) | ftxui::flex}),
      PanelAccent(highlighted_), focused);
}

ftxui::Element InventoryPanel::RenderRow(const ftxui::EntryState& state) {
  const std::string& lbl = state.label;
  // The cursor shows only while the list holds focus, so it never competes
  // with the tab-bar highlight above.
  std::string cursor = state.focused && zone_ == kZoneList ? "> " : "  ";
  int idx = state.index;
  // Records where the highlighted row landed, so the item menu can open beside
  // it. Applied to whichever row is built below, so it follows the row rather
  // than one way of drawing it.
  ftxui::Decorator mark = [](ftxui::Element e) { return e; };
  if (idx == selected_) {
    mark = ftxui::reflect(cursor_box_);
  }
  if (idx < 0 || idx >= static_cast<int>(rows_.size()) ||
      static_cast<int>(lbl.size()) < 60) {
    return ftxui::text(cursor + lbl) | mark;
  }
  const InventoryRowState& row = rows_[idx];
  if (row.level_ok && row.job_ok && !row.is_trace) {
    return ftxui::text(cursor + lbl) | mark;
  }
  // Byte offsets into the label built by RenderEquipList:
  // name(26) | slot and padding(14) | level(7) | job(13) | rest
  ftxui::Element name_elem = ftxui::text(lbl.substr(0, 26));
  if (row.is_trace) {
    name_elem = name_elem | ftxui::dim;
  }
  ftxui::Element lv_elem = ftxui::text(lbl.substr(40, 7));
  if (!row.level_ok) {
    lv_elem = lv_elem | ftxui::color(kRed);
  }
  ftxui::Element job_elem = ftxui::text(lbl.substr(47, 13));
  if (!row.job_ok) {
    job_elem = job_elem | ftxui::color(kRed);
  }
  return ftxui::hbox({ftxui::text(cursor), name_elem,
                      ftxui::text(lbl.substr(26, 14)), lv_elem, job_elem,
                      ftxui::text(lbl.substr(60))}) |
         mark;
}

bool InventoryPanel::OnTabBarEvent(const ftxui::Event& event,
                                   const std::function<void()>& on_enter) {
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
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsForward(event) && active_tab_ == kShopTab) {
    // The one tab entered from the bar itself: there is no list below it to
    // walk down into first.
    on_enter();
    return true;
  }
  // Swallow the rest, or it leaks to the hidden Equip menu and silently moves
  // its selection while the tab bar holds focus.
  return true;
}

bool InventoryPanel::OnStackListEvent(const ftxui::Event& event,
                                      const std::function<void()>& on_enter) {
  // Use/Etc: the whole ring is ours to walk, there being no ftxui::Menu under
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

ftxui::Component InventoryPanel::MakeComponent(std::function<void()> on_enter) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  // Drawn here rather than at entry-generation time because ftxui::Menu only
  // accepts std::string* entries, and this is the only hook that can produce a
  // coloured Element. It also suppresses the default inversion, so the caret
  // looks the same whether or not the item menu is open.
  opt.entries_option.transform = [this](ftxui::EntryState state) {
    return RenderRow(state);
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // Focusable whether or not the equip list has rows. Container::Tab asks its
  // active panel whether it is focusable and drops every key when the answer
  // is no, and an ftxui::Menu says no on an empty list -- which would take the
  // tab bar down with a list it has nothing to do with.
  ftxui::Component renderer = AlwaysFocusable(ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); }));
  return ftxui::CatchEvent(renderer, [this, on_enter](ftxui::Event event) {
    if (zone_ == kZoneTabs) {
      return OnTabBarEvent(event, on_enter);
    }
    if (active_tab_ != kEquipTab) {
      return OnStackListEvent(event, on_enter);
    }
    return OnEquipListEvent(event, on_enter);
  });
}

}  // namespace ms
