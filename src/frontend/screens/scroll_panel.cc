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
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

namespace {

// The name gives up four columns and the rate two, which is what pays for the
// Cost column. A name too long for what is left slides under it rather than
// losing its tail.
constexpr int kNameWidth = 12;
constexpr int kRateWidth = 7;  // matches the "Success" header label width
constexpr int kStatsWidth = 22;
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
  ScrollTier item_tier = TierForLevel(proto.required_level());
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
  SetFilter(std::move(filtered));
  return true;
}

void ScrollPanel::SetFilter(std::vector<const Scroll*> filtered) {
  ordered_ = std::move(filtered);
  std::sort(ordered_.begin(), ordered_.end(), ByTypeAndRate);
  selected_ = 0;
  ResetComponent();
}

void ScrollPanel::ResetComponent() {
  ftxui::MenuOption opt;
  opt.entries_option.transform = [](ftxui::EntryState state) -> ftxui::Element {
    return ftxui::text((state.active ? "> " : "  ") + state.label);
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
        ThemedWindow(" Scrolls — " + FormatWithCommas(TracesHeld()) + " 📜 ",
                     ftxui::vbox(std::move(rows)));
    if (confirm_.open()) {
      // yflex lets main fill the remaining height after the confirm window
      // takes its 3 rows, matching the full-height behaviour without confirm.
      return ftxui::vbox(
          {std::move(main) | ftxui::yflex, confirm_.RenderWindow()});
    }
    return main;
  });
}

ftxui::Element ScrollPanel::Render() {
  return component_->Render();
}

bool ScrollPanel::OnEvent(ftxui::Event event) {
  if (confirm_.open()) {
    if (confirm_.OnEvent(event) == ConfirmChoice::kConfirmed) {
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

int ScrollPanel::TracesHeld() const {
  int held = 0;
  for (const StackableItem& stack : character_.stackables(ITEM_CATEGORY_ETC)) {
    if (stack.name() == kSpellTraceName) {
      held += stack.count();
    }
  }
  return held;
}

bool ScrollPanel::CanAffordSelected() const {
  if (ordered_.empty()) {
    return false;
  }
  return TracesHeld() >= selected_scroll().trace_cost();
}

ftxui::Element ScrollPanel::RenderResult(const ScrollResult& r) const {
  if (r.outcome == kScrollNoSlots) {
    std::string msg = r.scroll_category == SCROLL_CATEGORY_CLEAN_SLATE
                          ? "No lost slots to restore"
                          : "No scroll slots remaining";
    return ResultWindow(" Error ", r.equip_name, {CenteredRow(msg)});
  }
  std::string result_text;
  ftxui::Color result_color;
  if (r.outcome == kScrollSuccess &&
      r.scroll_category == SCROLL_CATEGORY_CLEAN_SLATE) {
    result_text = "Slot Restored";
    result_color = kGreen;
  } else if (r.outcome == kScrollSuccess) {
    result_text = "SUCCESS";
    result_color = kGreen;
  } else {
    result_text = "FAILED";
    result_color = kMutedYellow;
  }
  return ResultWindow(
      " Result ", r.equip_name + "  |  " + r.scroll_name,
      {
          CenteredRow(result_text) | ftxui::color(result_color),
          CenteredRow(std::to_string(r.slots_remaining) + " slots remaining"),
      });
}

std::string ScrollPanel::FormatEntry(
    const Scroll& scroll, std::chrono::steady_clock::duration elapsed) {
  std::string name = ScrollingWindow(scroll.name(), kNameWidth, elapsed);
  std::string rate =
      PadRight(std::to_string(scroll.success_rate()) + "%", kRateWidth);
  std::string stats;
  if (scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
    stats = "Restores slot";
  } else {
    const EquipStats& s = scroll.stats();
    AppendStat(stats, s.attack(), "ATT");
    AppendStat(stats, s.magic_attack(), "MATT");
    AppendStat(stats, s.str(), "STR");
    AppendStat(stats, s.dex(), "DEX");
    AppendStat(stats, s.int_(), "INT");
    AppendStat(stats, s.luk(), "LUK");
    AppendStat(stats, s.max_hp(), "HP");
    AppendStat(stats, s.def(), "DEF");
  }

  return name + "  " + rate + "  " + PadRight(stats, kStatsWidth) +
         CostCell(scroll.trace_cost());
}

}  // namespace ms
