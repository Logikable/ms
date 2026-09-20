#include "src/frontend/screens/player_inspect_panel.h"

#include <memory>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "google/protobuf/util/message_differencer.h"
#include "src/character/link.h"
#include "src/frontend/main_layout.h"
#include "src/frontend/widgets/exp_bar.h"
#include "src/frontend/widgets/keys.h"

namespace ms {

PlayerInspectPanel::PlayerInspectPanel(GameState& state)
    : state_(state),
      character_(state.rng, Character()),
      // No account: this is somebody else's sheet, so the Farm/Boss row is
      // gated on the level written on it.
      stats_(character_, /*account=*/nullptr, state.skills) {
  BuildPanels();
}

void PlayerInspectPanel::UseActions(PlayerInspectActions actions) {
  actions_ = std::move(actions);
  BuildPanels();
}

void PlayerInspectPanel::BuildPanels() {
  char_panel_ = std::make_unique<CharacterPanel>(character_, state_.account,
                                                 focus_, state_.skills);
  char_panel_->SetReadOnly(true);
  equip_panel_ =
      std::make_unique<EquippedPanel>(character_, state_.account, focus_);
  equip_panel_->SetReadOnly(true);

  CharacterPanelActions char_actions;
  char_actions.menu = actions_.skill;
  char_actions.hyper_inspect = actions_.hyper_stat;
  char_actions.all_stats = actions_.all_stats;
  char_component_ = char_panel_->MakeComponent(std::move(char_actions));

  equip_component_ = equip_panel_->MakeComponent(
      [this]() {
        if (actions_.item) {
          actions_.item();
        }
      },
      [this]() { expanded_ = !expanded_; });
}

void PlayerInspectPanel::SetPlayer(const PlayerInfo& player) {
  if (google::protobuf::util::MessageDifferencer::Equals(player, shown_)) {
    return;
  }
  bool same_member = player.account_id() == shown_.account_id();
  shown_ = player;
  character_.RestoreFrom(player.sheet(), state_.equips, state_.items);
  character_.set_autoswap_presets(player.autoswap_presets());
  LinkTally tally;
  for (const std::pair<const int, int>& line : player.link_lines()) {
    tally.Record(static_cast<Job>(line.first), line.second);
  }
  character_.set_link_tally(std::move(tally));
  character_.UseEquipSets(state_.equip_sets);
  if (!same_member) {
    Reset();
    return;
  }
  // They changed under the cursor -- took off a hat, or a whole set. The
  // panels hold where the cursor was and clamp it to what is left.
}

void PlayerInspectPanel::Reset() {
  expanded_ = false;
  // On the Equipped list, which is what the reader came for. The same panel
  // the main view opens focused.
  focus_ = kEquipPanel;
  stats_.SetPreset(Activity::kFarming);
  BuildPanels();
}

Activity PlayerInspectPanel::preset() const {
  return char_panel_->SelectedActivity();
}

const EquipInstance* PlayerInspectPanel::selected_item() const {
  EquipSlot slot = selected_slot();
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return nullptr;
  }
  return character_.WornAt(equip_panel_->gear_preset(), slot);
}

EquipSlot PlayerInspectPanel::selected_slot() const {
  return equip_panel_->selected_slot();
}

bool PlayerInspectPanel::OnEvent(const ftxui::Event& event) {
  if (IsSwitchPanel(event)) {
    // Nothing to walk to while one panel is the whole screen, as on the main
    // view.
    if (!expanded_) {
      focus_ = focus_ == kCharPanel ? kEquipPanel : kCharPanel;
    }
    return true;
  }
  // The components are driven by hand rather than through a container: two
  // detached components both answer Focused(), so only the one holding the
  // cursor is handed the key.
  if (expanded_ || focus_ == kEquipPanel) {
    return equip_component_->OnEvent(event);
  }
  return char_component_->OnEvent(event);
}

void PlayerInspectPanel::SyncAllStats() {
  stats_.SetPreset(preset());
}

ftxui::Element PlayerInspectPanel::RenderAllStats() const {
  return stats_.Render() |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, AllStatsPanel::kTotalWidth);
}

bool PlayerInspectPanel::OnAllStatsEvent(const ftxui::Event& event) {
  return stats_.OnEvent(event);
}

ftxui::Element PlayerInspectPanel::Render(int rows, int columns) {
  equip_panel_->SetExpanded(expanded_);
  if (expanded_) {
    equip_panel_->SetWidth(columns);
    return equip_component_->Render();
  }
  // The main screen's own split, so a member's panels stand where the
  // reader's do. The right column is always there: this screen is the two
  // panels and nothing else.
  MainWidths widths = ComputeMainWidths(columns, /*has_right_column=*/true);
  char_panel_->SetWidth(widths.left);
  // One row goes to the exp bar. Nothing else shares the column, so the
  // Character panel keeps every extra stat a tall terminal has room for.
  char_panel_->SetMaxRows(rows - 1);
  equip_panel_->SetWidth(widths.right);
  return ftxui::vbox({
      ftxui::hbox({
          // Over a filler: an hbox hands its children the whole row, which
          // would drag the Character panel's bottom border to the foot of the
          // screen. The Equipped list grows into its column by itself.
          ftxui::vbox({char_component_->Render(), ftxui::filler()}) |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, widths.left),
          equip_component_->Render() | ftxui::flex,
      }) | ftxui::flex,
      ExpBar(character_.proto()),
  });
}

}  // namespace ms
