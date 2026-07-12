#include "src/frontend/inventory_panel.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/scroll_panel.h"
#include "src/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

enum InventoryTab : int {
  kEquipTab = 0,
  kUseTab = 1,
  kEtcTab = 2,
  kNumInventoryTabs = 3,
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

// Renders the left-aligned Equip/Use/Etc chip row (active tab inverted) with a
// centered meso counter overlaid in the empty space, over a separator. Mirrors
// TraceRecoverPanel::RenderTabs.
ftxui::Element RenderTabBar(int active_tab, int64_t meso) {
  const char* labels[kNumInventoryTabs] = {"Equip", "Use", "Etc"};
  std::vector<ftxui::Element> chips;
  for (int i = 0; i < kNumInventoryTabs; ++i) {
    ftxui::Element chip =
        ftxui::text(std::string(" ") + labels[i] + " ") | ftxui::color(kTheme);
    if (i == active_tab) {
      chip = chip | ftxui::inverted;
    }
    chips.push_back(std::move(chip));
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
// cursor on the `selected`-th row. Shows "(empty)" when there are no stacks.
ftxui::Element RenderStackList(const std::vector<StackableItem>& stacks,
                               int selected) {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text("  " + PadRight("Name", 26) + "Quantity"));
  rows.push_back(ThemedSeparator());
  for (int i = 0; i < static_cast<int>(stacks.size()); ++i) {
    std::string cursor = i == selected ? "> " : "  ";
    rows.push_back(ftxui::text(cursor + PadRight(stacks[i].name(), 26) +
                               std::to_string(stacks[i].count())));
  }
  if (stacks.empty()) {
    rows.push_back(ftxui::text("  (empty)"));
  }
  return ftxui::vbox(std::move(rows));
}

}  // namespace

InventoryPanel::InventoryPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Equip", "Inspect", "Scroll", "Star Force", "Recover", "Close"}),
      sell_menu_({"Sell", "Close"}) {
}

ItemMenu& InventoryPanel::menu() {
  return active_tab_ == kEquipTab ? menu_ : sell_menu_;
}

void InventoryPanel::OpenMenu() {
  if (active_tab_ != kEquipTab) {
    sell_menu_.Reset();
    // Sell is unavailable on an empty tab or an unsellable selected stack.
    ItemCategory category =
        active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
    const std::vector<StackableItem>& stacks = character_.stackables(category);
    if (selected_stack_ >= static_cast<int>(stacks.size()) ||
        stacks[selected_stack_].prototype().sell_price() <= 0) {
      sell_menu_.Disable(kSellSell);
    }
    return;
  }
  menu_.Reset();
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
  if (!eq->CanStarForce()) {
    menu_.Disable(kMenuStarForce);
  }
}

Screen InventoryPanel::OnMenuEvent(ftxui::Event event, int& panel_focus,
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
      return sell_menu_.selected() == kSellSell ? kSell : kMain;
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
      if (character_.inventory().empty()) {
        panel_focus = kEquipPanel;
      }
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
    return ftxui::text("  (empty)");
  }
  return ftxui::vbox({
      ftxui::text(kColumnHeader),
      ftxui::text(kColumnHeader2),
      ThemedSeparator(),
      menu->Render(),
  });
}

ftxui::Element InventoryPanel::RenderContent(ftxui::Component menu) {
  ftxui::Element body;
  if (active_tab_ == kUseTab || active_tab_ == kEtcTab) {
    ItemCategory category =
        active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
    const std::vector<StackableItem>& stacks = character_.stackables(category);
    // Keep the cursor in range as stacks are sold off.
    selected_stack_ = std::min(
        selected_stack_, std::max(0, static_cast<int>(stacks.size()) - 1));
    body = RenderStackList(stacks, selected_stack_);
  } else {
    body = RenderEquipList(menu);
  }
  return ThemedWindow(" Inventory ",
                      ftxui::vbox({RenderTabBar(active_tab_, character_.meso()),
                                   std::move(body)}));
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
    std::string cursor = state.focused ? "> " : "  ";
    int idx = state.index;
    if (idx < 0 || idx >= (int)rows_.size() || (int)lbl.size() < 60) {
      return ftxui::text(cursor + lbl);
    }
    const InventoryRowState& row = rows_[idx];
    if (row.level_ok && row.job_ok && !row.is_trace) {
      return ftxui::text(cursor + lbl);
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
                        ftxui::text(lbl.substr(60))});
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // rows_ and entries_ are rebuilt on every render via RenderContent so the
  // display stays in sync with inventory changes made via on_enter.
  ftxui::Component renderer = ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); });
  return ftxui::CatchEvent(renderer, [this, on_enter](ftxui::Event event) {
    if (event == ftxui::Event::ArrowLeft) {
      if (active_tab_ > 0) {
        --active_tab_;
        selected_stack_ = 0;
      }
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      if (active_tab_ < kEtcTab) {
        ++active_tab_;
        selected_stack_ = 0;
      }
      return true;
    }
    if (active_tab_ != kEquipTab) {
      // Use/Etc: move the stack cursor with Up/Down. Swallow list navigation
      // and activation regardless so the hidden Equip menu stays put.
      ItemCategory category =
          active_tab_ == kUseTab ? ITEM_CATEGORY_USE : ITEM_CATEGORY_ETC;
      int count = static_cast<int>(character_.stackables(category).size());
      if (event == ftxui::Event::ArrowUp) {
        if (selected_stack_ > 0) {
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
    if (event == ftxui::Event::Character(' ')) {
      on_enter();
      return true;
    }
    return false;
  });
}

}  // namespace ms
