#include "src/frontend/bag_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/scroll_panel.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

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

}  // namespace

BagPanel::BagPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Equip", "Inspect", "Scroll", "Star Force", "Recover"}) {
}

void BagPanel::OpenMenu() {
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

Screen BagPanel::OnMenuEvent(ftxui::Event event, int& panel_focus,
                             ScrollPanel& scroll_panel) {
  if (event == ftxui::Event::Escape) {
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
  if (event == ftxui::Event::Return) {
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

ftxui::Component BagPanel::MakeComponent(std::function<void()> on_enter) {
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
    const ftxui::Color kRed = ftxui::Color::RGB(185, 70, 70);
    const std::string& lbl = state.label;
    std::string cursor = state.focused ? "> " : "  ";
    int idx = state.index;
    if (idx < 0 || idx >= (int)rows_.size() || (int)lbl.size() < 60) {
      return ftxui::text(cursor + lbl);
    }
    const BagRowState& row = rows_[idx];
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

  // rows_ and entries_ are rebuilt from inventory() on every render so the
  // display stays in sync with changes made via on_enter.
  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    rows_.clear();
    entries_.clear();
    for (int i = 0; i < character_.inventory().size(); ++i) {
      const EquipTabItem& item = character_.inventory()[i];
      const EquipPrototype& proto = item.prototype();
      int level = 1;
      if (proto.required_level() > 0) {
        level = proto.required_level();
      }
      std::string info = "Lv" + PadRight(std::to_string(level), 3) + "  " +
                         FormatJobCategories(proto);
      int scroll_pass = -1, scroll_left = -1, scroll_restore = -1;
      if (proto.upgrade_slots() > 0) {
        scroll_pass = item.equip_state().scroll_successes();
        scroll_left = item.equip_state().remaining_upgrade_slots();
        scroll_restore = proto.upgrade_slots() - scroll_pass - scroll_left;
      }
      BagRowState row;
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
      return ThemedWindow(" Bag ", ftxui::text("(empty)"));
    }
    return ThemedWindow(" Bag ", ftxui::vbox({
                                     ftxui::text(kColumnHeader),
                                     ftxui::text(kColumnHeader2),
                                     ThemedSeparator(),
                                     menu->Render(),
                                 }));
  });
}

}  // namespace ms
