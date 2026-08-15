#include "src/frontend/screens/scroll_panel.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/spell_trace_cost.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

namespace {

// The name gives up four columns and the rate two, which is what pays for the
// Cost column. A name too long for what is left slides under it rather than
// losing its tail.
constexpr int kNameWidth = 12;
constexpr int kRateWidth = 7;  // matches the "Success" header label width
// Wide enough for the longest line any scroll writes: an armour All Stats
// scroll, which pays a stat, HP and DEF at once.
constexpr int kStatsWidth = 30;
// Wide enough for a four-figure cost and the two columns 📜 occupies.
constexpr int kCostWidth = 8;
// The cost cell, right-aligned in kCostWidth columns.
//
// PadLeft counts bytes and the scroll glyph is four of them for two columns,
// so the padding is worked out from the display width by hand. Getting this
// wrong shows up as a Cost column that does not line up with its header.
std::string CostCell(int traces) {
  std::string digits = std::to_string(traces);
  int shown = static_cast<int>(digits.size()) + 3;  // space + a two-column 📜
  return std::string(std::max(0, kCostWidth - shown), ' ') + digits + " 📜";
}

// Two leading spaces match the "  " / "> " cursor the menu prepends to entries.
// Built from the widths rather than written out, so a column cannot drift from
// the heading over it.
std::string ColumnHeader() {
  return "  " + PadRight("Name", kNameWidth) + "  " +
         PadRight("Success", kRateWidth) + "  " +
         PadRight("Stats", kStatsWidth) + PadLeft("Cost", kCostWidth);
}

// What a scroll pays, in the order EquipStats lists it. Four stats that agree
// are one "All Stats" line: that is the option's name, and spelling it out
// costs the row four cells to say one thing.
std::string ScrollStats(const Scroll& scroll) {
  if (scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
    return "Restores slot";
  }
  const EquipStats& s = scroll.stats();
  std::string out;
  AppendStat(out, s.attack(), "ATT");
  AppendStat(out, s.magic_attack(), "MATT");
  if (s.str() > 0 && s.str() == s.dex() && s.str() == s.int_() &&
      s.str() == s.luk()) {
    AppendStat(out, s.str(), "All Stats");
  } else {
    AppendStat(out, s.str(), "STR");
    AppendStat(out, s.dex(), "DEX");
    AppendStat(out, s.int_(), "INT");
    AppendStat(out, s.luk(), "LUK");
  }
  AppendStat(out, s.max_hp(), "HP");
  AppendStat(out, s.def(), "DEF");
  return out;
}

bool ByTypeAndRate(const Scroll* a, const Scroll* b) {
  if (a->scroll_type() != b->scroll_type()) {
    return a->scroll_type() < b->scroll_type();
  }
  return a->success_rate() > b->success_rate();
}

}  // namespace

ScrollPanel::ScrollPanel(const CharacterInstance& character,
                         const std::map<std::string, Scroll>& scrolls)
    : character_(character), scrolls_(scrolls) {
  for (const std::pair<const std::string, Scroll>& kv : scrolls_) {
    ordered_.push_back(&kv.second);
  }
  std::sort(ordered_.begin(), ordered_.end(), ByTypeAndRate);
  ResetComponent();
}

bool ScrollPanel::SetFilterForPrototype(const EquipPrototype& proto) {
  target_name_ = proto.name();
  ScrollTier item_tier = TierForLevel(proto.required_level());
  ScrollTarget item_target = TargetForSlot(proto.equip_slot());
  std::set<int> item_cats(proto.equip_job_categories().begin(),
                          proto.equip_job_categories().end());
  std::vector<const Scroll*> filtered;
  for (const std::pair<const std::string, Scroll>& kv : scrolls_) {
    const Scroll& s = kv.second;
    if (s.tier() != item_tier) {
      continue;
    }
    // A clean slate restores a slot whoever is holding the item, so it skips
    // the job check -- but not the tier one, which is what it costs by.
    if (s.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
      filtered.push_back(&s);
      continue;
    }
    // A weapon scroll on a hat is not a thing GMS sells. Both sides have to
    // name the same kind of equipment, so an item in a slot that names none
    // is offered nothing.
    if (s.target() != item_target) {
      continue;
    }
    for (int scroll_cat : s.applicable_job_categories()) {
      if (item_cats.count(scroll_cat)) {
        filtered.push_back(&s);
        break;
      }
    }
  }
  if (filtered.empty()) {
    return false;
  }
  SetFilter(std::move(filtered), proto.required_level());
  return true;
}

void ScrollPanel::SetFilter(std::vector<const Scroll*> filtered,
                            int required_level) {
  ordered_ = std::move(filtered);
  target_level_ = required_level;
  std::sort(ordered_.begin(), ordered_.end(), ByTypeAndRate);
  selected_ = 0;
  ResetComponent();
}

void ScrollPanel::ResetComponent() {
  ftxui::MenuOption opt;
  // The cost is its own cell rather than part of the label, so a price the
  // player cannot pay can be said in red without colouring the whole row.
  opt.entries_option.transform =
      [this](ftxui::EntryState state) -> ftxui::Element {
    return ftxui::hbox({
        ftxui::text((state.active ? "> " : "  ") + state.label),
        CostCellFor(state.index),
    });
  };
  // Wrapped so the list is a ring: this screen is the list and nothing else,
  // so Up off the top row has nowhere to go but the bottom one.
  ftxui::Component menu =
      WrappingList(ftxui::Menu(&entries_, &selected_, opt), selected_,
                   [this]() { return static_cast<int>(entries_.size()); });
  // entries_ is rebuilt from ordered_ on every render so the display stays
  // in sync with SetFilter calls.
  component_ = ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    if (!ordered_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(ordered_.size()) - 1);
    }
    // Followed before the rows are built, so the selected one is the only row
    // asking for a slide and it asks from the moment it was selected.
    clock_.Follow(selected_);
    entries_.clear();
    for (int i = 0; i < static_cast<int>(ordered_.size()); ++i) {
      entries_.push_back(FormatEntry(
          *ordered_[i], i == selected_
                            ? clock_.Elapsed()
                            : std::chrono::steady_clock::duration()));
    }
    std::vector<ftxui::Element> rows = {
        ftxui::text(ColumnHeader()),
        ThemedSeparator(),
        menu->Render(),
    };
    // The balance rides in the title: it is the number every row's Cost is
    // read against, and up there it never scrolls away with the list.
    ftxui::Element main =
        ThemedWindow(" Scrolls — " + FormatWithCommas(TracesOwned()) + " 📜 ",
                     ftxui::vbox(std::move(rows)));
    if (confirm_.open()) {
      // Over the list rather than under it: the question is about the row the
      // cursor is on, and a window that pushed the list around while asking
      // would move that row out from under it.
      return ftxui::dbox({std::move(main),
                          ftxui::center(RenderConfirm() | ftxui::clear_under)});
    }
    return main;
  });
}

ftxui::Element ScrollPanel::Render() {
  return component_->Render();
}

bool ScrollPanel::OnEvent(ftxui::Event event) {
  if (confirm_.open()) {
    // Answered yes only counts when the player can pay: the window greys
    // Confirm out, and this is what makes the key agree with the picture.
    if (confirm_.OnEvent(event) == ConfirmChoice::kConfirmed &&
        CanAffordSelected()) {
      confirmed_ = true;
    }
    return true;
  }
  if (IsForward(event)) {
    confirm_.Open();
    return true;
  }
  return component_->OnEvent(event);
}

bool ScrollPanel::TakeConfirmed() {
  bool v = confirmed_;
  confirmed_ = false;
  return v;
}

const Scroll& ScrollPanel::selected_scroll() const {
  return *ordered_[selected_];
}

int ScrollPanel::TracesOwned() const {
  int owned = 0;
  for (const StackableItem& stack : character_.stackables(ITEM_CATEGORY_ETC)) {
    if (stack.name() == kSpellTraceName) {
      owned += stack.count();
    }
  }
  return owned;
}

bool ScrollPanel::CanAffordSelected() const {
  if (ordered_.empty()) {
    return false;
  }
  return TracesOwned() >= CostOfSelected();
}

ftxui::Element ScrollPanel::CostCellFor(int index) const {
  if (index < 0 || index >= static_cast<int>(ordered_.size())) {
    return ftxui::text(std::string(kCostWidth, ' '));
  }
  int cost = TraceCost(*ordered_[index], target_level_);
  ftxui::Element cell = ftxui::text(CostCell(cost));
  if (cost > TracesOwned()) {
    // The same red the confirm window says it in, so the list answers "what
    // can I afford" without opening every row to find out.
    cell = std::move(cell) | ftxui::color(kRed);
  }
  return cell;
}

int ScrollPanel::CostOfSelected() const {
  if (ordered_.empty()) {
    return 0;
  }
  return TraceCost(selected_scroll(), target_level_);
}

ftxui::Element ScrollPanel::RenderConfirm() const {
  const Scroll& scroll = selected_scroll();
  int cost = CostOfSelected();
  bool affordable = CanAffordSelected();

  std::string what = scroll.name();
  if (!target_name_.empty()) {
    what += "  ->  " + target_name_;
  }

  // What the scroll does, said the same way the list says it.
  std::string effect = scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE
                           ? "Restores one lost slot"
                           : ScrollStats(scroll);

  // The price alone. What the player owns is in the list's title behind this
  // window, and doing the subtraction for them here only crowded the one
  // number they are deciding on. Red says they cannot pay it, and the greyed
  // Confirm below says the same -- neither needs the words for it.
  ftxui::Element money_row =
      CenteredRow("Cost " + FormatWithCommas(cost) + " \U0001F4DC");
  if (!affordable) {
    money_row = std::move(money_row) | ftxui::color(kRed);
  }

  // Three blocks with a rule between each: what is going on what, what it does
  // and what it costs, and the answer. The middle block is the only one the
  // player reads twice.
  return ThemedWindow(
      " Confirm ",
      ftxui::vbox({
          CenteredRow(what),
          ThemedSeparator(),
          CenteredRow(effect),
          std::move(money_row),
          ThemedSeparator(),
          ConfirmButtons(confirm_.focus(), affordable) | ftxui::hcenter,
      }));
}

ftxui::Element ScrollPanel::RenderResult(const ScrollResult& r) const {
  if (r.outcome == kScrollNoSlots) {
    std::string msg = r.scroll_category == SCROLL_CATEGORY_CLEAN_SLATE
                          ? "No lost slots to restore"
                          : "No scroll slots remaining";
    return ResultWindow(" Error ", r.equip_name, {CenteredRow(msg)});
  }
  // A scroll that landed goes gold all the way through: border, rules and the
  // word itself. It is the moment the traces were spent for. A failure keeps
  // the steel-blue frame -- nothing happened worth colouring the room for.
  std::string result_text = "FAILED";
  ftxui::Color text_color = kMutedYellow;
  ftxui::Color accent = kTheme;
  if (r.outcome == kScrollSuccess) {
    result_text = r.scroll_category == SCROLL_CATEGORY_CLEAN_SLATE
                      ? "Slot Restored"
                      : "SUCCESS";
    text_color = kYellow;
    accent = kYellow;
  }
  return ResultWindow(
      " Result ", r.equip_name + "  |  " + r.scroll_name,
      {
          CenteredRow(result_text) | ftxui::color(text_color),
          CenteredRow(std::to_string(r.slots_remaining) + " slots remaining"),
      },
      accent);
}

std::string ScrollPanel::FormatEntry(
    const Scroll& scroll, std::chrono::steady_clock::duration elapsed) {
  std::string name = ScrollingWindow(scroll.name(), kNameWidth, elapsed);
  std::string rate =
      PadRight(std::to_string(scroll.success_rate()) + "%", kRateWidth);
  return name + "  " + rate + "  " + PadRight(ScrollStats(scroll), kStatsWidth);
}

}  // namespace ms
