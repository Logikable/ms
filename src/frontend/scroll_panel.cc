#include "src/frontend/scroll_panel.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

namespace {

constexpr int kNameWidth = 16;
constexpr int kRateWidth = 9;  // matches "Success %" header label width
// Two leading spaces match the "  " / "> " cursor the menu prepends to entries.
constexpr char kColumnHeader[] = "  Name              Success %  Stats";

bool ByTypeAndRate(const Scroll* a, const Scroll* b) {
  if (a->scroll_type() != b->scroll_type()) {
    return a->scroll_type() < b->scroll_type();
  }
  return a->success_rate() > b->success_rate();
}

// GMS tier cutoffs: T1 < 75, T2 75–114, T3 115+.
ScrollTier TierForLevel(int required_level) {
  if (required_level >= 115) {
    return SCROLL_TIER_3;
  }
  if (required_level >= 75) {
    return SCROLL_TIER_2;
  }
  return SCROLL_TIER_1;
}

}  // namespace

ScrollPanel::ScrollPanel(const std::map<std::string, Scroll>& scrolls)
    : scrolls_(scrolls) {
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
    if (s.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
      filtered.push_back(&s);
      continue;
    }
    if (s.tier() != item_tier) {
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
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);
  // entries_ is rebuilt from ordered_ on every render so the display stays
  // in sync with SetFilter calls.
  component_ = ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    for (const Scroll* scroll : ordered_) {
      entries_.push_back(FormatEntry(*scroll));
    }
    if (!entries_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    }
    std::vector<ftxui::Element> rows = {
        ftxui::text(kColumnHeader),
        ThemedSeparator(),
        menu->Render(),
    };
    ftxui::Element main =
        ThemedWindow("  Scrolls ", ftxui::vbox(std::move(rows)));
    if (confirming_) {
      // yflex lets main fill the remaining height after the confirm window
      // takes its 3 rows, matching the full-height behaviour without confirm.
      return ftxui::vbox(
          {std::move(main) | ftxui::yflex, ConfirmWindow(confirm_cancel_)});
    }
    return main;
  });
}

ftxui::Element ScrollPanel::Render() {
  return component_->Render();
}

bool ScrollPanel::OnEvent(ftxui::Event event) {
  if (confirming_) {
    if (IsBack(event)) {
      confirming_ = false;
      confirm_cancel_ = false;
      return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
      confirm_cancel_ = false;
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      confirm_cancel_ = true;
      return true;
    }
    if (IsForward(event)) {
      if (!confirm_cancel_) {
        confirmed_ = true;
      }
      confirming_ = false;
      confirm_cancel_ = false;
      return true;
    }
    return true;
  }
  if (IsForward(event)) {
    confirming_ = true;
    confirm_cancel_ = false;
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

ftxui::Element ScrollPanel::RenderResult(const ScrollResult& r) const {
  if (r.outcome == kScrollNoSlots) {
    std::string msg = r.scroll_category == SCROLL_CATEGORY_CLEAN_SLATE
                          ? " No lost slots to restore "
                          : " No scroll slots remaining ";
    return ThemedWindow(
        " Error ",
        ftxui::vbox({
            ftxui::text(" " + r.equip_name + " ") | ftxui::hcenter,
            ThemedSeparator(),
            ftxui::text(msg) | ftxui::hcenter,
            ftxui::text(""),
            ftxui::text("[Continue]") | ftxui::inverted | ftxui::hcenter,
        }));
  }
  std::string result_text;
  ftxui::Color result_color;
  if (r.outcome == kScrollSuccess &&
      r.scroll_category == SCROLL_CATEGORY_CLEAN_SLATE) {
    result_text = " Slot Restored ";
    result_color = kGreen;
  } else if (r.outcome == kScrollSuccess) {
    result_text = " SUCCESS ";
    result_color = kGreen;
  } else {
    result_text = " FAILED ";
    result_color = kMutedYellow;
  }
  return ThemedWindow(
      " Result ",
      ftxui::vbox({
          ftxui::text(" " + r.equip_name + "  |  " + r.scroll_name + " "),
          ThemedSeparator(),
          ftxui::text(result_text) | ftxui::hcenter |
              ftxui::color(result_color),
          ftxui::text(" " + std::to_string(r.slots_remaining) +
                      " slots remaining ") |
              ftxui::hcenter,
          ftxui::text(""),
          ftxui::text("[Continue]") | ftxui::inverted | ftxui::hcenter,
      }));
}

std::string ScrollPanel::FormatEntry(const Scroll& scroll) {
  std::string name = scroll.name();
  if ((int)name.size() < kNameWidth) {
    name += std::string(kNameWidth - (int)name.size(), ' ');
  } else {
    name = name.substr(0, kNameWidth);
  }

  std::string rate = std::to_string(scroll.success_rate()) + "%";
  while ((int)rate.size() < kRateWidth) {
    rate += ' ';
  }

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

  return name + "  " + rate + "  " + stats;
}

}  // namespace ms
