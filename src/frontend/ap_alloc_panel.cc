#include "src/frontend/ap_alloc_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

struct StatEntry {
  const char* label;  // 3-char display label for row alignment
  const char* name;   // canonical name used in confirmation message
  StatField field;
};

constexpr StatEntry kStats[] = {
    {"STR", "STR", STAT_FIELD_STR}, {"DEX", "DEX", STAT_FIELD_DEX},
    {"INT", "INT", STAT_FIELD_INT}, {"LUK", "LUK", STAT_FIELD_LUK},
    {" HP", "HP", STAT_FIELD_HP},   {" MP", "MP", STAT_FIELD_MP},
};

constexpr int kNumStats = 6;

struct StatValues {
  int base;   // from allocated AP
  int bonus;  // from gear
};

// Returns the allocated and gear-bonus components for `field`. Update the
// bonus case for MP here when EquipStats gains a max_mp field.
StatValues StatValuesFor(const AllocatedStats& a, const EquipStats& e,
                         StatField field) {
  switch (field) {
    case STAT_FIELD_STR:
      return {a.str(), e.str()};
    case STAT_FIELD_DEX:
      return {a.dex(), e.dex()};
    case STAT_FIELD_INT:
      return {a.int_(), e.int_()};
    case STAT_FIELD_LUK:
      return {a.luk(), e.luk()};
    case STAT_FIELD_HP:
      return {a.hp(), e.max_hp()};
    case STAT_FIELD_MP:
      return {a.mp(), 0};
    default:
      return {0, 0};
  }
}

std::string StatRowText(const char* label, int base, int bonus) {
  int total = base + bonus;
  return std::string(label) + "  " + std::to_string(total) + " (" +
         std::to_string(base) + "+" + std::to_string(bonus) + ")  ";
}

}  // namespace

ApAllocPanel::ApAllocPanel(CharacterInstance& character)
    : character_(character) {
}

void ApAllocPanel::Reset() {
  selected_ = 0;
  button_ = 0;
  confirming_ = false;
  confirm_sel_ = 0;
}

ftxui::Element ApAllocPanel::Render() const {
  int ap = character_.proto().ap();
  bool has_ap = ap > 0;

  const EquipStats& equip = character_.equip_stats();
  const AllocatedStats& alloc = character_.proto().allocated_stats();

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" AP: " + std::to_string(ap) + " "));
  rows.push_back(ftxui::separator());

  for (int i = 0; i < kNumStats; ++i) {
    StatValues sv = StatValuesFor(alloc, equip, kStats[i].field);
    std::string text = StatRowText(kStats[i].label, sv.base, sv.bonus);

    if (i != selected_) {
      rows.push_back(ftxui::text("  " + text));
      continue;
    }

    ftxui::Element btn1 = ftxui::text("[+1]");
    ftxui::Element btn_all = ftxui::text("[All]");
    if (!has_ap) {
      btn1 = btn1 | ftxui::dim;
      btn_all = btn_all | ftxui::dim;
    } else if (button_ == 0) {
      btn1 = btn1 | ftxui::inverted;
    } else {
      btn_all = btn_all | ftxui::inverted;
    }
    rows.push_back(ftxui::hbox({
        ftxui::text("> " + text),
        btn1,
        ftxui::text(" "),
        btn_all,
        ftxui::text(" "),
    }));
  }

  if (confirming_) {
    rows.push_back(ftxui::separator());
    std::string msg = " Assign all " + std::to_string(ap) + " AP to " +
                      kStats[selected_].name + "? ";
    rows.push_back(ftxui::text(msg));
    ftxui::Element confirm_btn = ftxui::text("[Confirm]");
    ftxui::Element cancel_btn = ftxui::text("[Cancel]");
    if (confirm_sel_ == 0) {
      confirm_btn = confirm_btn | ftxui::inverted;
    } else {
      cancel_btn = cancel_btn | ftxui::inverted;
    }
    rows.push_back(ftxui::hbox({
        ftxui::text(" "),
        confirm_btn,
        ftxui::text("  "),
        cancel_btn,
        ftxui::text(" "),
    }));
  }

  return ftxui::window(ftxui::text(" AP Allocation "),
                       ftxui::vbox(std::move(rows)));
}

Screen ApAllocPanel::OnEvent(ftxui::Event event) {
  if (confirming_) {
    if (event == ftxui::Event::Escape) {
      confirming_ = false;
      return kApAlloc;
    }
    if (event == ftxui::Event::ArrowLeft) {
      confirm_sel_ = 0;
      return kApAlloc;
    }
    if (event == ftxui::Event::ArrowRight) {
      confirm_sel_ = 1;
      return kApAlloc;
    }
    if (event == ftxui::Event::Return) {
      if (confirm_sel_ == 0) {
        character_.AllocateAllStat(kStats[selected_].field);
      }
      confirming_ = false;
    }
    return kApAlloc;
  }

  if (event == ftxui::Event::Escape) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    if (selected_ > 0) {
      selected_--;
      button_ = 0;
    }
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (selected_ < kNumStats - 1) {
      selected_++;
      button_ = 0;
    }
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowLeft) {
    button_ = 0;
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowRight) {
    button_ = 1;
    return kApAlloc;
  }
  if (event == ftxui::Event::Return && character_.proto().ap() > 0) {
    if (button_ == 0) {
      character_.AllocateStat(kStats[selected_].field);
    } else {
      confirming_ = true;
      confirm_sel_ = 0;
    }
  }
  return kApAlloc;
}

}  // namespace ms
