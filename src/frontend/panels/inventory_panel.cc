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
                            const CharacterInstance& character) {
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
      ThemedSeparator(),
  });
}

// Renders a Name/Quantity list of `stacks`, one row per stack, with a "> "
// cursor on the `selected`-th row. An empty tab is just "(empty)", with no
// column header over it. The cursor is drawn only when `focused`, matching the
// Equip tab, whose menu takes its cursor from ftxui's own focus state.
ftxui::Element RenderStackList(const std::vector<StackableItem>& stacks,
                               int selected, bool focused,
                               ftxui::Box& cursor_box) {
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
    ftxui::Element row = ftxui::text(cursor + PadRight(stacks[i].name(), 26) +
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
      ThemedSeparator(),
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

void InventoryPanel::OpenMenu() {
  if (active_tab_ != kEquipTab) {
    sell_menu_.Reset();
    // Sell is unavailable on an empty tab or an unsellable selected stack.
    ItemCategory category =
        active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
    // Nothing on the Etc tab is usable, so the entry is not there at all --
    // Etc is where drops and quest pieces sit, not where anything is drunk.
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
    // player came to do: a Use item that does nothing yet is a fact about
    // that item, and the greyed row is the answer to "can I drink this?".
    if (proto.effect() == ITEM_EFFECT_UNSPECIFIED) {
      sell_menu_.Disable(kStackUse);
    }
    if (proto.sell_price() <= 0) {
      sell_menu_.Disable(kStackSell);
    }
    return;
  }
  menu_.Reset();
  // Entries the player has not reached yet are not drawn at all. This comes
  // before the questions about the item itself: what is disabled below is
  // what this item cannot do, and what is hidden here is what the player
  // cannot do to any item.
  if (!Unlocked(Feature::kScrolling, character_)) {
    menu_.Hide(kMenuScroll);
  }
  if (!Unlocked(Feature::kStarForce, character_)) {
    menu_.Hide(kMenuStarForce);
  }
  if (!Unlocked(Feature::kRecovery, character_)) {
    menu_.Hide(kMenuRecover);
  }
  const EquipInstance* eq = character_.inventory().equip_instance(selected_);
  if (eq == nullptr) {
    // Traces can only be inspected or recovered.
    menu_.Disable(kMenuAction);
    menu_.Disable(kMenuScroll);
    menu_.Disable(kMenuStarForce);
    return;
  }
  // Live items cannot be recovered.
  menu_.Disable(kMenuRecover);
  if (!character_.CanEquip(eq->prototype())) {
    menu_.Disable(kMenuAction);
  }
  // Scrolling asks the prototype, not the slot count: a weapon with every slot
  // spent still takes a Clean Slate, so only an item that refuses scrolls
  // outright loses the entry.
  if (!Supports(eq->prototype(), UPGRADE_SCROLL)) {
    menu_.Disable(kMenuScroll);
  }
  if (!eq->CanStarForce()) {
    menu_.Disable(kMenuStarForce);
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
      if (scroll_panel.SetFilterForPrototype(
              character_.inventory()[selected_].prototype())) {
        return kScrollSelect;
      }
    }
    if (menu_.selected() == kMenuStarForce) {
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
    row.label = FormatItemEntry(item.name(), proto.equip_slot(), info,
                                scroll_pass, scroll_left, scroll_restore);
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
      ThemedSeparator(),
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
    body = RenderStackList(stacks, selected_stack_,
                           focused && zone_ == kZoneList, cursor_box_);
  } else {
    body = RenderEquipList(menu);
  }
  return AccentWindow(
      " Inventory ",
      ftxui::vbox({RenderTabBar(VisibleTabs(), active_tab_, character_.meso(),
                                focused && zone_ == kZoneTabs, character_),
                   std::move(body) | ftxui::flex}),
      PanelAccent(highlighted_), focused);
}

ftxui::Component InventoryPanel::MakeComponent(std::function<void()> on_enter) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  // Suppress the default color inversion so the caret indicator looks the same
  // whether or not the item menu is open.
  // Color is applied here rather than at entry-generation time because
  // ftxui::Menu only accepts std::string* entries; transform is the only hook
  // that can produce a colored Element.
  // label layout: name(26) + "  "(2) + slot(10) + "  "(2) + info(20) + rest
  //   info[0..6]  = "LvXXX  "  (level, 7 chars)
  //   info[7..19] = job string (13 chars, padded)
  opt.entries_option.transform =
      [this](ftxui::EntryState state) -> ftxui::Element {
    const std::string& lbl = state.label;
    // Show the cursor only while the list zone holds focus, so it never
    // competes with the white tab-bar highlight above.
    std::string cursor = state.focused && zone_ == kZoneList ? "> " : "  ";
    int idx = state.index;
    // Records where the highlighted row lands so the item menu can open beside
    // it. Applied to whichever of the rows below is built, so it follows the
    // row rather than one particular way of drawing it.
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
    // name(26) | "  "+slot(10)+"  "(14) | level(7) | job(13) | rest
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
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // rows_ and entries_ are rebuilt on every render via RenderContent so the
  // display stays in sync with inventory changes made via on_enter.
  // Focusable whether or not the equip list has rows in it. Container::Tab
  // asks its active panel whether it is focusable and drops every key when the
  // answer is no, and an ftxui::Menu says no on an empty list -- which would
  // take the tab bar down with the list it has nothing to do with.
  ftxui::Component renderer = AlwaysFocusable(ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); }));
  return ftxui::CatchEvent(renderer, [this, on_enter](ftxui::Event event) {
    if (zone_ == kZoneTabs) {
      // Tab bar: Left/Right switch tabs, Down descends into the item list.
      if (event == ftxui::Event::ArrowLeft) {
        StepTab(-1);
        return true;
      }
      if (event == ftxui::Event::ArrowRight) {
        StepTab(+1);
        return true;
      }
      if (event == ftxui::Event::ArrowDown) {
        // Descend only into a non-empty tab; an empty list is inert and would
        // leave the cursor nowhere visible.
        if (!ActiveTabEmpty()) {
          zone_ = kZoneList;
        }
        return true;
      }
      if (IsForward(event) && active_tab_ == kShopTab) {
        // The one tab the player enters from the bar itself: there is no list
        // below it to walk down into first.
        on_enter();
        return true;
      }
      // Swallow the rest (notably Up) so nothing leaks to the hidden Equip menu
      // and silently moves its selection while the tab bar holds focus.
      return true;
    }
    // List zone: Up off the top row returns to the tab bar.
    if (active_tab_ != kEquipTab) {
      // Use/Etc: walk the stack cursor with Up/Down. Swallow list navigation
      // and activation regardless so the hidden Equip menu stays put.
      ItemCategory category =
          active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
      int count = static_cast<int>(character_.stackables(category).size());
      if (event == ftxui::Event::ArrowUp) {
        if (selected_stack_ == 0) {
          zone_ = kZoneTabs;
        } else {
          --selected_stack_;
        }
        return true;
      }
      if (event == ftxui::Event::ArrowDown) {
        if (selected_stack_ + 1 < count) {
          ++selected_stack_;
        }
        return true;
      }
      if (IsForward(event)) {
        // Open the {Sell, Close} menu on a non-empty stack.
        if (count > 0) {
          on_enter();
        }
        return true;
      }
      return false;
    }
    // Equip tab: intercept Up off the top row to return to the tab bar;
    // otherwise let the ftxui Menu move its own selection.
    if (event == ftxui::Event::ArrowUp && selected_ == 0) {
      zone_ = kZoneTabs;
      return true;
    }
    if (event == ftxui::Event::Character(' ')) {
      on_enter();
      return true;
    }
    return false;
  });
}

}  // namespace ms
