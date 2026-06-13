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

}  // namespace

BagPanel::BagPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Equip", "Inspect", "Scroll", "Star Force"}) {
}

void BagPanel::OpenMenu() {
  menu_.Reset();
  const EquipTabItem& item = *character_.inventory()[selected_];
  const EquipInstance* eq = dynamic_cast<const EquipInstance*>(&item);
  if (eq == nullptr) {
    // Traces can only be inspected.
    menu_.Disable(kMenuAction);
    menu_.Disable(kMenuScroll);
    menu_.Disable(kMenuStarForce);
    return;
  }
  if (!character_.CanEquip(item.prototype())) {
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
              character_.inventory()[selected_]->prototype())) {
        return kScrollSelect;
      }
    }
    if (menu_.selected() == kMenuStarForce) {
      return kStarForce;
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
  opt.entries_option.transform = [](ftxui::EntryState state) -> ftxui::Element {
    return ftxui::text((state.focused ? "> " : "  ") + state.label);
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);

  // entries_ is rebuilt from inventory() on every render so the display stays
  // in sync with changes made via on_enter.
  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    for (const std::unique_ptr<EquipTabItem>& item_ptr :
         character_.inventory()) {
      const EquipPrototype& proto = item_ptr->prototype();
      int level = 1;
      if (proto.required_level() > 0) {
        level = proto.required_level();
      }
      std::string info = "Lv" + PadRight(std::to_string(level), 3) + "  " +
                         FormatJobCategories(proto);
      int slots = 0;
      if (const EquipInstance* eq =
              dynamic_cast<const EquipInstance*>(item_ptr.get())) {
        slots = eq->equip_state().remaining_upgrade_slots();
      }
      entries_.push_back(
          FormatItemEntry(item_ptr->name(), proto.equip_slot(), info, slots));
    }
    if (!entries_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    }
    if (entries_.empty()) {
      return ftxui::window(ftxui::text(" Bag "), ftxui::text("(empty)"));
    }
    return ftxui::window(ftxui::text(" Bag "), ftxui::vbox({
                                                   ftxui::text(kColumnHeader),
                                                   ftxui::separator(),
                                                   menu->Render(),
                                               }));
  });
}

}  // namespace ms
