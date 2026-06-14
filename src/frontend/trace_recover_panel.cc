#include "src/frontend/trace_recover_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/frontend/types.h"
#include "src/item.h"

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
    if (character_.inventory().equip_instance(i) != nullptr &&
        character_.inventory()[i].prototype().name() == proto_name) {
      matching_indices_.push_back(i);
    }
  }
}

ftxui::Element TraceRecoverPanel::Render() const {
  if (trace_ == nullptr) {
    return ftxui::window(ftxui::text(" Trace Recovery "),
                         ftxui::text(" (no trace) "));
  }

  int recovery_stars = EquipInstance::RecoveryStars(trace_->stars());

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(trace_->name()) | ftxui::hcenter);
  rows.push_back(ftxui::separator());
  rows.push_back(
      ftxui::text("Recovers at " + std::to_string(recovery_stars) + "★") |
      ftxui::hcenter);
  rows.push_back(ftxui::separator());

  if (matching_indices_.empty()) {
    rows.push_back(ftxui::text(" No matching item in inventory ") |
                   ftxui::hcenter);
    rows.push_back(ftxui::separator());
    rows.push_back(ftxui::text(" Esc to cancel "));
    return ftxui::window(ftxui::text(" Trace Recovery "),
                         ftxui::vbox(std::move(rows)));
  }

  for (int i = 0; i < static_cast<int>(matching_indices_.size()); ++i) {
    int inv_idx = matching_indices_[i];
    const EquipTabItem& item = character_.inventory()[inv_idx];
    std::string label = (i == selected_ ? "> " : "  ") + item.name();
    rows.push_back(ftxui::text(label));
  }
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::text(" Enter: recover  Esc: cancel "));
  return ftxui::window(ftxui::text(" Trace Recovery "),
                       ftxui::vbox(std::move(rows)));
}

bool TraceRecoverPanel::OnEvent(ftxui::Event event) {
  if (matching_indices_.empty()) {
    return false;
  }
  if (event == ftxui::Event::ArrowUp && selected_ > 0) {
    --selected_;
    return true;
  }
  if (event == ftxui::Event::ArrowDown &&
      selected_ < static_cast<int>(matching_indices_.size()) - 1) {
    ++selected_;
    return true;
  }
  return false;
}

int TraceRecoverPanel::selected_index() const {
  if (matching_indices_.empty()) {
    return -1;
  }
  return matching_indices_[selected_];
}

ftxui::Element TraceRecoverPanel::RenderResult(
    const TraceRecoveryResult& r) const {
  return ftxui::window(
      ftxui::text(" Recovery Complete "),
      ftxui::vbox({
          ftxui::text(" " + r.equip_name + " ") | ftxui::hcenter,
          ftxui::separator(),
          ftxui::text("Recovered at " + std::to_string(r.stars_recovered) +
                      "★") |
              ftxui::hcenter,
          ftxui::text(""),
          ftxui::text(" Press Enter to continue "),
      }));
}

}  // namespace ms
