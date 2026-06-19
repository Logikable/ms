#include "src/frontend/trace_recover_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/types.h"
#include "src/item.h"
#include "src/protos/equip.pb.h"

namespace ms {

TraceRecoverPanel::TraceRecoverPanel(const CharacterInstance& character)
    : character_(character) {
}

void TraceRecoverPanel::SetTrace(const EquipTabItem* trace) {
  trace_ = trace;
  matching_indices_.clear();
  selected_ = 0;
  if (trace_ == nullptr) {
    return;
  }
  const std::string& proto_name = trace_->prototype().name();
  for (int i = 0; i < character_.inventory().size(); ++i) {
    // equip_instance is null for traces; skip them as recovery targets.
    if (character_.inventory().equip_instance(i) != nullptr &&
        character_.inventory()[i].prototype().name() == proto_name) {
      matching_indices_.push_back(i);
    }
  }
}

EquipInstance TraceRecoverPanel::PreviewResult() const {
  Equip state = trace_->equip_state();
  state.set_stars(EquipInstance::RecoveryStars(trace_->stars()));
  return EquipInstance(trace_->prototype(), state);
}

ftxui::Element TraceRecoverPanel::RenderTabs() const {
  if (matching_indices_.empty()) {
    return ftxui::vbox({
        ftxui::text(" (no matching items) ") | ftxui::hcenter,
        ThemedSeparator(),
    });
  }
  std::vector<ftxui::Element> chips;
  for (int i = 0; i < static_cast<int>(matching_indices_.size()); ++i) {
    const EquipTabItem& item = character_.inventory()[matching_indices_[i]];
    std::string label = " " + std::to_string(item.stars()) + "★ ";
    ftxui::Element chip = ftxui::text(label) | ftxui::color(kTheme);
    if (i == selected_) {
      chip = chip | ftxui::inverted;
    }
    chips.push_back(std::move(chip));
  }
  return ftxui::vbox({
      ftxui::hbox(std::move(chips)) | ftxui::hcenter,
      ThemedSeparator(),
  });
}

ftxui::Element TraceRecoverPanel::RenderBelow() const {
  if (confirming_) {
    return ConfirmWindow(confirm_cancel_);
  }
  // Reserve the same height as ConfirmWindow so the layout doesn't shift.
  return ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 3);
}

bool TraceRecoverPanel::OnEvent(ftxui::Event event) {
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
    return true;  // Swallow all other events while confirming.
  }
  if (matching_indices_.empty()) {
    return false;
  }
  if (event == ftxui::Event::ArrowLeft && selected_ > 0) {
    --selected_;
    return true;
  }
  if (event == ftxui::Event::ArrowRight &&
      selected_ < static_cast<int>(matching_indices_.size()) - 1) {
    ++selected_;
    return true;
  }
  if (IsForward(event)) {
    confirming_ = true;
    confirm_cancel_ = false;
    return true;
  }
  return false;
}

bool TraceRecoverPanel::TakeConfirmed() {
  bool v = confirmed_;
  confirmed_ = false;
  return v;
}

int TraceRecoverPanel::selected_index() const {
  if (matching_indices_.empty()) {
    return -1;
  }
  return matching_indices_[selected_];
}

ftxui::Element TraceRecoverPanel::RenderResult(
    const TraceRecoveryResult& r) const {
  return ThemedWindow(
      " Recovery Complete ",
      ftxui::vbox({
          ftxui::text(" " + r.equip_name + " ") | ftxui::hcenter,
          ThemedSeparator(),
          ftxui::text("Recovered at " + std::to_string(r.stars_recovered) +
                      "★") |
              ftxui::hcenter,
          ftxui::text(""),
          ftxui::text("[Continue]") | ftxui::inverted | ftxui::hcenter,
      }));
}

}  // namespace ms
