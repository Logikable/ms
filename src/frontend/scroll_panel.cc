#include "src/frontend/scroll_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

namespace {

bool ByTypeAndRate(const Scroll* a, const Scroll* b) {
  if (a->scroll_type() != b->scroll_type()) {
    return a->scroll_type() < b->scroll_type();
  }
  return a->success_rate() > b->success_rate();
}

}  // namespace

ScrollPanel::ScrollPanel(const std::map<std::string, Scroll>& scrolls)
    : scrolls_(scrolls) {
  for (const std::pair<const std::string, Scroll>& kv : scrolls_) {
    ordered_.push_back(&kv.second);
  }
  std::sort(ordered_.begin(), ordered_.end(), ByTypeAndRate);
}

void ScrollPanel::SetFilter(std::vector<const Scroll*> filtered) {
  ordered_ = std::move(filtered);
  std::sort(ordered_.begin(), ordered_.end(), ByTypeAndRate);
  selected_ = 0;
}

const Scroll& ScrollPanel::selected_scroll() const {
  return *ordered_[selected_];
}

ftxui::Component ScrollPanel::MakeComponent() {
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_);
  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    for (const Scroll* scroll : ordered_) {
      entries_.push_back(FormatEntry(*scroll));
    }
    if (!entries_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    }
    return ftxui::window(ftxui::text(" Scrolls "), menu->Render());
  });
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
  if ((int)name.size() < 28) {
    name += std::string(28 - (int)name.size(), ' ');
  } else {
    name = name.substr(0, 28);
  }

  std::string rate = std::to_string(scroll.success_rate()) + "%";
  while ((int)rate.size() < 4) {
    rate = " " + rate;
  }

  std::string tier;
  switch (scroll.tier()) {
    case SCROLL_TIER_1:
      tier = "T1";
      break;
    case SCROLL_TIER_2:
      tier = "T2";
      break;
    case SCROLL_TIER_3:
      tier = "T3";
      break;
    default:
      tier = "  ";
      break;
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

  return name + "  " + rate + "  " + tier + "  " + stats;
}

}  // namespace ms
