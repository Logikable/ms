#include "src/frontend/ap_alloc_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/panel_util.h"
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

// HP and MP AP allocation was removed from GMS in 2013. AllocateStat still
// supports STAT_FIELD_HP for Demon Avenger (15 HP per AP); add an entry here
// when that job is implemented.
constexpr StatEntry kStats[] = {
    {"STR", "STR", STAT_FIELD_STR},
    {"DEX", "DEX", STAT_FIELD_DEX},
    {"INT", "INT", STAT_FIELD_INT},
    {"LUK", "LUK", STAT_FIELD_LUK},
};

constexpr int kNumStats = sizeof(kStats) / sizeof(kStats[0]);

// Width of the button area "[+1] [All] " that follows the stat on the
// selected row, used to pad non-selected rows to the same total width.
constexpr int kButtonsWidth = 11;

// Longest name field in kStats, used to size the panel against the confirm
// dialog so the panel never widens when confirm appears.
constexpr int kMaxStatNameLen = 3;  // STR/DEX/INT/LUK

enum Button : int { kButtonPlus1 = 0, kButtonAll = 1 };
enum ConfirmSel : int { kSelConfirm = 0, kSelCancel = 1 };

struct StatValues {
  int base;   // from allocated AP
  int bonus;  // from gear
};

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
      return {a.mp(), e.max_mp()};
    default:
      return {0, 0};
  }
}

std::string StatRowText(const char* label, int base, int bonus) {
  int total = base + bonus;
  std::string s = std::string(label) + "  " + std::to_string(total);
  if (bonus > 0) {
    s += " (" + std::to_string(base) + "+" + std::to_string(bonus) + ")";
  }
  return s + "  ";
}

}  // namespace

ApAllocPanel::ApAllocPanel(CharacterInstance& character)
    : character_(character) {
}

void ApAllocPanel::Reset() {
  selected_ = 0;
  button_ = kButtonPlus1;
  confirming_ = false;
  confirm_sel_ = kSelConfirm;
}

ftxui::Element ApAllocPanel::RenderBelowPanel(int ap) const {
  if (!confirming_) {
    return ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 4);
  }
  std::string msg = " Assign all " + std::to_string(ap) + " AP to " +
                    kStats[selected_].name + "? ";
  ftxui::Element confirm_btn = ftxui::text("[Confirm]");
  ftxui::Element cancel_btn = ftxui::text("[Cancel]");
  if (confirm_sel_ == kSelConfirm) {
    confirm_btn = confirm_btn | ftxui::inverted;
  } else {
    cancel_btn = cancel_btn | ftxui::inverted;
  }
  return ThemedWindow("", ftxui::vbox({
                              ftxui::text(msg),
                              ftxui::hbox({
                                  ftxui::text(" "),
                                  confirm_btn,
                                  ftxui::text("  "),
                                  cancel_btn,
                                  ftxui::text(" "),
                              }),
                          }));
}

ftxui::Element ApAllocPanel::Render() const {
  int ap = character_.proto().ap();
  bool has_ap = ap > 0;

  const EquipStats& equip = character_.equip_stats();
  const AllocatedStats& alloc = character_.proto().allocated_stats();

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" AP: " + std::to_string(ap) + " "));
  rows.push_back(ThemedSeparator());

  std::vector<std::string> row_texts;
  int max_row_width = 0;
  for (int i = 0; i < kNumStats; ++i) {
    StatValues sv = StatValuesFor(alloc, equip, kStats[i].field);
    row_texts.push_back(StatRowText(kStats[i].label, sv.base, sv.bonus));
    max_row_width = std::max(max_row_width, (int)row_texts.back().size());
  }
  // Ensure the panel is wide enough that the confirm dialog never causes a
  // resize. Confirm inner: " Assign all <ap> AP to <name>? " = 21 + ap_digits
  // + kMaxStatNameLen. Row region inner: 2 (prefix) + max_row_width +
  // kButtonsWidth. Floor = confirm_inner - 2 - kButtonsWidth.
  int confirm_inner = 21 + (int)std::to_string(ap).size() + kMaxStatNameLen;
  max_row_width = std::max(max_row_width, confirm_inner - 2 - kButtonsWidth);

  for (int i = 0; i < kNumStats; ++i) {
    std::string row_text = row_texts[i];
    row_text.resize(max_row_width, ' ');

    if (i != selected_) {
      rows.push_back(
          ftxui::text("  " + row_text + std::string(kButtonsWidth, ' ')));
      continue;
    }

    ftxui::Element btn1 = ftxui::text("[+1]");
    ftxui::Element btn_all = ftxui::text("[All]");
    if (!has_ap) {
      btn1 = btn1 | ftxui::dim;
      btn_all = btn_all | ftxui::dim;
    } else if (button_ == kButtonPlus1) {
      btn1 = btn1 | ftxui::inverted;
    } else {
      btn_all = btn_all | ftxui::inverted;
    }
    rows.push_back(ftxui::hbox({
        ftxui::text("> " + row_text),
        btn1,
        ftxui::text(" "),
        btn_all,
        ftxui::text(" "),
    }));
  }

  ftxui::Element main =
      ThemedWindow(" AP Allocation ", ftxui::vbox(std::move(rows)));
  return ftxui::vbox({main, RenderBelowPanel(ap)});
}

Screen ApAllocPanel::OnConfirmEvent(ftxui::Event event) {
  if (IsBack(event)) {
    confirming_ = false;
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowLeft) {
    confirm_sel_ = kSelConfirm;
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowRight) {
    confirm_sel_ = kSelCancel;
    return kApAlloc;
  }
  if (IsForward(event)) {
    if (confirm_sel_ == kSelConfirm) {
      character_.AllocateAllStat(kStats[selected_].field);
    }
    confirming_ = false;
  }
  return kApAlloc;
}

Screen ApAllocPanel::OnEvent(ftxui::Event event) {
  if (confirming_) {
    return OnConfirmEvent(event);
  }
  if (IsBack(event)) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    if (selected_ > 0) {
      selected_--;
      button_ = kButtonPlus1;
    }
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (selected_ < kNumStats - 1) {
      selected_++;
      button_ = kButtonPlus1;
    }
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowLeft) {
    button_ = kButtonPlus1;
    return kApAlloc;
  }
  if (event == ftxui::Event::ArrowRight) {
    button_ = kButtonAll;
    return kApAlloc;
  }
  if (IsForward(event) && character_.proto().ap() > 0) {
    if (button_ == kButtonPlus1) {
      character_.AllocateStat(kStats[selected_].field);
    } else {
      confirming_ = true;
      confirm_sel_ = kSelConfirm;
    }
  }
  return kApAlloc;
}

}  // namespace ms
