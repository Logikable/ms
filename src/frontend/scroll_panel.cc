#include "src/frontend/scroll_panel.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
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
    if (kv.second.tier() != item_tier) {
      continue;
    }
    for (int scroll_cat : kv.second.applicable_job_categories()) {
      if (item_cats.count(scroll_cat)) {
        filtered.push_back(&kv.second);
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
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_);
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
    return ftxui::window(ftxui::text("  Scrolls "),
                         ftxui::vbox({
                             ftxui::text(kColumnHeader),
                             ftxui::separator(),
                             menu->Render(),
                         }));
  });
}

ftxui::Element ScrollPanel::Render() {
  return component_->Render();
}

bool ScrollPanel::OnEvent(ftxui::Event event) {
  return component_->OnEvent(event);
}

const Scroll& ScrollPanel::selected_scroll() const {
  return *ordered_[selected_];
}

void ScrollPanel::AppendStat(std::string& out, int val,
                             const std::string& label) {
  if (val <= 0) {
    return;
  }
  if (!out.empty()) {
    out += "  ";
  }
  out += "+" + std::to_string(val) + " " + label;
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
    rate = " " + rate;
  }

  std::string stats;
  const EquipStats& s = scroll.stats();
  AppendStat(stats, s.attack(), "ATT");
  AppendStat(stats, s.magic_attack(), "MATT");
  AppendStat(stats, s.str(), "STR");
  AppendStat(stats, s.dex(), "DEX");
  AppendStat(stats, s.int_(), "INT");
  AppendStat(stats, s.luk(), "LUK");
  AppendStat(stats, s.max_hp(), "HP");
  AppendStat(stats, s.def(), "DEF");

  return name + "  " + rate + "  " + stats;
}

}  // namespace ms
