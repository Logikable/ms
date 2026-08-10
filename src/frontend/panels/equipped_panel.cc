#include "src/frontend/panels/equipped_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Two leading spaces match the "  " / "> " cursor added by the entry transform.
constexpr char kColumnHeader[] =
    "  Name                      "  // 2 cursor + 26 name
    "  Equip Slot"                  // 2 sep + 10 slot
    "  Stats               "        // 2 sep + 20 info (10 atk + 10 main)
    "  Scrolls";                    // 2 sep + label

// Sub-header row: 64 spaces align "Pass/Left/Restore" under the scroll column.
constexpr char kColumnHeader2[] =
    "                              "  // 30
    "                              "  // 30
    "    "                            // 4 → 64 total
    "Pass/Left/Restore";

}  // namespace

EquippedPanel::EquippedPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Unequip", "Inspect", "Scroll", "Star Force", "Close"}) {
}

void EquippedPanel::OpenMenu() {
  // Opening the menu on the worn weapon is the trail's first step walked: the
  // player looked, and what they were being sent to look at is on screen.
  if (selected_slot() == EQUIP_SLOT_PRIMARY_WEAPON) {
    FollowedToWeapon(character_);
  }
  menu_.Reset();
  // Entries the player has not reached yet are not drawn at all, ahead of any
  // question about this particular item: a character who has just been handed
  // this panel has no bag to unequip into yet, and asking about scrolls means
  // nothing to them for a long while after that.
  if (!Unlocked(Feature::kUnequip, character_)) {
    menu_.Hide(kMenuAction);
  }
  if (!Unlocked(Feature::kScrolling, character_)) {
    menu_.Hide(kMenuScroll);
  }
  if (!Unlocked(Feature::kStarForce, character_)) {
    menu_.Hide(kMenuStarForce);
  }
  EquipSlot slot = selected_slot();
  if (slot != EQUIP_SLOT_UNSPECIFIED) {
    const EquipInstance& item = character_.equipped().at(slot);
    // Scrolling asks the prototype, not the slot count: a weapon with every
    // slot spent still takes a Clean Slate, so only an item that refuses
    // scrolls outright loses the entry.
    // Not drawn rather than drawn grey, as above: an upgrade this item cannot
    // take is worth no row, whatever the reason it cannot take it.
    if (!Supports(item.prototype(), UPGRADE_SCROLL)) {
      menu_.Hide(kMenuScroll);
    }
    if (!item.CanStarForce()) {
      menu_.Hide(kMenuStarForce);
    }
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

Screen EquippedPanel::OnMenuEvent(ftxui::Event event,
                                  ScrollPanel& scroll_panel) {
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
      character_.Unequip(selected_slot());
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
              character_.equipped().at(selected_slot()).prototype())) {
        return kScrollSelect;
      }
    }
    if (menu_.selected() == kMenuStarForce) {
      FollowedToAction(Feature::kStarForce, character_);
      return kStarForce;
    }
    return kMain;
  }
  return kItemMenu;
}

EquipSlot EquippedPanel::selected_slot() const {
  if (selected_ >= static_cast<int>(slots_.size())) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  return slots_[selected_];
}

ftxui::Element EquippedPanel::RenderRow(const ftxui::EntryState& state) {
  std::string cursor = state.focused ? "> " : "  ";
  int idx = state.index;
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

std::string EquippedPanel::RowInfo(const EquipStats& stats) const {
  // One column for the stat this job's damage is built on -- the same question
  // the character panel and the AP reset ask, so asked in the same place
  // rather than switched over jobs here.
  Job job = character_.proto().job();
  const DisplayStat* main = DisplayStatFor(PrimaryStatField(job));
  std::string main_str;
  if (main != nullptr && main->GetFrom(stats) > 0) {
    main_str = "+" + std::to_string(main->GetFrom(stats)) + " " + main->label;
  }
  // Room for one attack figure, so show the one this job swings with. A wand
  // carries both, and a magician's weapon attack never reaches the damage
  // chain.
  std::string atk_str;
  bool magic = job == JOB_MAGICIAN || job == JOB_ICE_LIGHTNING_WIZARD ||
               job == JOB_FIRE_POISON_WIZARD || job == JOB_CLERIC;
  if (!magic && stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  } else if (stats.magic_attack() > 0) {
    atk_str = "+" + std::to_string(stats.magic_attack()) + " MATT";
  } else if (stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  }
  // Attack leads: it is the number that decides a weapon, and the main stat
  // qualifies it.
  return PadRight(atk_str, 10) + PadRight(main_str, 10);
}

void EquippedPanel::RebuildRows() {
  // The menu writes selected_ behind this panel's back, so a move is noticed
  // here rather than hooked at the keypress.
  name_clock_.Follow(selected_);
  entries_.clear();
  slots_.clear();
  inactive_.clear();
  name_bytes_.clear();
  led_.clear();
  // Asked once for the whole list rather than per row: it is a fact about the
  // character, and only the worn weapon's row acts on it.
  bool lead = LeadToWeapon(character_);
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       character_.equipped()) {
    const EquipInstance& item = kv.second;
    slots_.push_back(kv.first);
    inactive_.push_back(!character_.AttackCounts(item.prototype()));
    // Only the selected row's name slides; the rest sit at their heads.
    bool selected = static_cast<int>(entries_.size()) == selected_;
    std::chrono::steady_clock::duration elapsed =
        selected ? name_clock_.Elapsed()
                 : std::chrono::steady_clock::duration::zero();
    name_bytes_.push_back(static_cast<int>(
        ItemNameCell(item.prototype().name(), elapsed).size()));
    led_.push_back(lead && kv.first == EQUIP_SLOT_PRIMARY_WEAPON);
    entries_.push_back(FormatItemEntry(item.prototype().name(), kv.first,
                                       RowInfo(item.stats()), item.prototype(),
                                       item.equip_state(), elapsed));
  }
  if (!entries_.empty()) {
    selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
  }
}

ftxui::Element EquippedPanel::RenderContent(ftxui::Component menu) {
  // Rebuilt from equipped() on every render, so the display stays in step with
  // whatever the item menu did.
  RebuildRows();
  bool focused = panel_focus_ == kEquipPanel;
  if (entries_.empty()) {
    return AccentWindow(" Equipped ", EmptyState("empty"),
                        PanelAccent(highlighted_), focused);
  }
  return AccentWindow(" Equipped ",
                      ftxui::vbox({
                          ftxui::text(kColumnHeader),
                          ftxui::text(kColumnHeader2),
                          PanelSeparator(highlighted_),
                          menu->Render(),
                      }),
                      PanelAccent(highlighted_), focused);
}

ftxui::Component EquippedPanel::MakeComponent(std::function<void()> on_enter) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  // Also suppresses the default inversion, so the caret looks the same whether
  // or not the item menu is open.
  opt.entries_option.transform = [this](ftxui::EntryState state) {
    return RenderRow(state);
  };
  // Wrapped so the list is a ring: there is no tab bar over this panel, so Up
  // off the top row has nowhere to go but the bottom one.
  ftxui::Component menu =
      WrappingList(ftxui::Menu(&entries_, &selected_, opt), selected_,
                   [this]() { return static_cast<int>(entries_.size()); });
  // Focusable whether or not anything is worn. Container::Tab asks its active
  // panel whether it is focusable and drops every key when the answer is no,
  // and an ftxui::Menu says no on an empty list -- which would leave this
  // panel deaf the moment the player strips down.
  ftxui::Component renderer = AlwaysFocusable(ftxui::Renderer(
      menu, [this, menu]() -> ftxui::Element { return RenderContent(menu); }));
  return ftxui::CatchEvent(renderer, [this, on_enter](ftxui::Event event) {
    if (event != ftxui::Event::Character(' ')) {
      return false;
    }
    // With nothing worn there is no item to act on, and the menu would open on
    // EQUIP_SLOT_UNSPECIFIED -- a slot the map has no entry for. Swallowed
    // rather than passed on, as on an empty tab of the bag.
    if (!character_.equipped().empty()) {
      on_enter();
    }
    return true;
  });
}

}  // namespace ms
