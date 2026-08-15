#include "src/frontend/screens/trace_recover_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// What the star bar may use. It stands over the Inspect card on this screen,
// so it is held to the card's width rather than allowed to push it wider --
// a player holding a dozen copies of one item has a dozen chips.
constexpr int kBarWidth = 44;

}  // namespace

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
        CenteredRow(EmptyState("no matching items", /*gutter=*/0)),
        ThemedSeparator(),
    });
  }
  // The chip row is the only thing on this screen the keys reach, so it is
  // always the focused bar.
  std::vector<TabSpec> tabs;
  for (int index : matching_indices_) {
    const EquipTabItem& item = character_.inventory()[index];
    tabs.push_back({std::to_string(item.stars()) + "★"});
  }
  return ftxui::vbox({
      CenteredRow(TabBar(tabs, selected_, /*row_focused=*/true, kBarWidth)),
      ThemedSeparator(),
  });
}

ftxui::Element TraceRecoverPanel::RenderBelow() const {
  if (confirm_.open()) {
    return confirm_.RenderWindow();
  }
  // Reserve the prompt's height so the layout doesn't shift when it opens.
  return ftxui::text("") |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, ConfirmPrompt::kWindowHeight);
}

bool TraceRecoverPanel::OnEvent(ftxui::Event event) {
  if (confirm_.open()) {
    if (confirm_.OnEvent(event) == ConfirmChoice::kConfirmed) {
      confirmed_ = true;
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
    confirm_.Open();
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
  return ResultWindow(
      " Recovery Complete ", r.equip_name,
      {CenteredRow("Recovered at " + std::to_string(r.stars_recovered) + "★")});
}

}  // namespace ms
