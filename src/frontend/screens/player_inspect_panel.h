/* PlayerInspectPanel shows a party member: the sheet they sent, drawn as they
 * see it themselves.
 *
 * It is their main screen without anything unrelated to them: their Character
 * panel on the left, what they are wearing on the right, and their exp bar at
 * the bottom. Both are the player's own panels in read-only mode (see
 * CharacterPanel::SetReadOnly), so the numbers shown for a member and the
 * numbers they see come from the same code. The columns are split as on the
 * main screen, by ComputeMainWidths.
 *
 * Tab moves between the two panels, and every other key goes to the one with
 * focus. Enter opens the same cards the player's own panels open: a worn
 * item's, a skill's, a Hyper Stat's, and the member's All Stats screen.
 *
 * The panel rebuilds the member's character from their sheet using this build's
 * catalogs, since a sheet names its items rather than describing them. A
 * different member rebuilds both panels, so the screen never mixes two people.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_PLAYER_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_PLAYER_INSPECT_PANEL_H_

#include <functional>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/multiplayer.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What Enter opens from the Inspect screen. Each is its own screen, as from the
// player's own panels, and nothing here changes anything.
struct PlayerInspectActions {
  // A worn item's card.
  std::function<void()> item;
  // The member's skill card and Hyper Stat card, from the panel's Skills and
  // Hyper tabs.
  std::function<void(const Skill&)> skill;
  std::function<void(HyperStatField)> hyper_stat;
  // The View All Stats row, which opens the member's own All Stats screen (see
  // RenderAllStats below).
  std::function<void()> all_stats;
};

class PlayerInspectPanel {
 public:
  explicit PlayerInspectPanel(GameState& state);

  // Sets what Enter opens. Call once, before the screen is first opened; the
  // panels are rebuilt with these whenever a new member is shown.
  void UseActions(PlayerInspectActions actions);

  // Shows a party member. An item this build doesn't have is dropped, as when a
  // save is loaded against changed catalogs. Called every tick, so a member
  // levelling up while being read shows it, and an unchanged player isn't
  // rebuilt.
  void SetPlayer(const PlayerInfo& player);
  // Back to a fresh screen: both panels new, the cursor on the Equipped list.
  // Called when the screen opens on someone else.
  void Reset();

  // Tab moves between the panels, and every other key goes to the focused one.
  // Returns whether the event was consumed. Escape is left to the caller, which
  // knows what is behind this screen.
  bool OnEvent(const ftxui::Event& event);
  // The screen at the terminal's size, borders and exp bar included.
  ftxui::Element Render(int rows, int columns);

  // Whether the Equipped panel fills the whole screen. Escape closes that
  // before closing the screen, as on the main view.
  bool expanded() const {
    return expanded_;
  }
  void CloseExpanded() {
    expanded_ = false;
  }

  // The member's All Stats screen. Call SyncAllStats first, so it opens on the
  // allocation their Character panel is showing.
  void SyncAllStats();
  ftxui::Element RenderAllStats() const;
  bool OnAllStatsEvent(const ftxui::Event& event);

  // The member as this build reads them, for anything that needs their stats.
  const CharacterInstance& character() const {
    return character_;
  }
  // The item under the cursor, or null when nothing is worn or the cursor is on
  // a bar.
  const EquipInstance* selected_item() const;
  // The slot it is in, which the reader's own gear is compared against.
  // Unspecified when the cursor is on a bar.
  EquipSlot selected_slot() const;
  // Which of the member's two allocations is shown.
  Activity preset() const;

 private:
  // Builds both panels and their components for character_. Called again for a
  // new member, since the panels hold a cursor and an open tab that belong only
  // to the member they were opened on.
  void BuildPanels();

  GameState& state_;
  // The member, rebuilt from their sheet. Kept rather than rebuilt every frame,
  // since a sheet only arrives when something about them changes. The panels
  // hold a reference to it, so it is declared before them and outlives them.
  CharacterInstance character_;
  // The member as the lobby last described them, so a tick with no changes
  // doesn't rebuild them.
  PlayerInfo shown_;
  PlayerInspectActions actions_;
  // Which panel has the cursor, in the main screen's terms, since the panels
  // read it. It belongs to this screen only, so moving it doesn't move the
  // cursor on the main view behind it.
  int focus_ = kEquipPanel;
  bool expanded_ = false;
  // Rebuilt for each member, so they are held by pointer. The components hold
  // references into them and are replaced together.
  std::unique_ptr<CharacterPanel> char_panel_;
  std::unique_ptr<EquippedPanel> equip_panel_;
  ftxui::Component char_component_;
  ftxui::Component equip_component_;
  // The member's own All Stats screen, for the same character.
  AllStatsPanel stats_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PLAYER_INSPECT_PANEL_H_
