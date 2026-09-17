/* PlayerInspectPanel reads a party member: the sheet they sent, drawn the way
 * they see it themselves.
 *
 * Their main screen, less everything that is not about them: their Character
 * panel on the left, what they are wearing on the right, their exp bar across
 * the foot. Both are the PLAYER'S OWN panels in read-only -- see
 * CharacterPanel::SetReadOnly -- so the numbers a member is shown and the
 * numbers they see cannot come from two places. The columns split the way the
 * main screen's do, by ComputeMainWidths.
 *
 * Tab moves between the two panels and everything else belongs to whichever
 * has it. Enter reaches the cards the player's own panels reach: a worn item's,
 * a skill's, a Hyper Stat's, and the member's All Stats screen.
 *
 * The panel rebuilds the member's character from their sheet against this
 * build's own catalogs -- a sheet names its items rather than describing them.
 * A DIFFERENT member builds both panels again, so a screen is never half one
 * person and half another.
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

// What Enter opens from the Inspect screen. Each is a screen of its own, the
// way it is from the player's own panels -- nothing here changes anything.
struct PlayerInspectActions {
  // A worn item's card.
  std::function<void()> item;
  // The member's skill card and Hyper Stat card, from the panel's Skills and
  // Hyper tabs.
  std::function<void(const Skill&)> skill;
  std::function<void(HyperStatField)> hyper_stat;
  // The View All Stats row, which opens the member's own All Stats screen --
  // RenderAllStats below.
  std::function<void()> all_stats;
};

class PlayerInspectPanel {
 public:
  explicit PlayerInspectPanel(GameState& state);

  // What Enter reaches. Call once, before the screen is first opened; the
  // panels are rebuilt with these whenever a new member arrives.
  void UseActions(PlayerInspectActions actions);

  // Points the panel at a party member; an item this build does not have is
  // dropped, as a save loaded against changed catalogs is. Called every tick,
  // so a member levelling under the reader shows it, and an unchanged player
  // is not rebuilt.
  void SetPlayer(const PlayerInfo& player);
  // Back to a screen nobody has touched: both panels new, the cursor on the
  // Equipped list. Called when the screen opens on somebody else.
  void Reset();

  // Tab moves between the panels; everything else goes to the focused one.
  // Returns whether the event was taken. Escape is the caller's: only they
  // know what is behind this screen.
  bool OnEvent(const ftxui::Event& event);
  // The screen at the terminal's size, borders and exp bar included.
  ftxui::Element Render(int rows, int columns);

  // Whether the Equipped panel is drawn over the whole screen. Escape closes
  // that before it closes the screen, as it does on the main view.
  bool expanded() const {
    return expanded_;
  }
  void CloseExpanded() {
    expanded_ = false;
  }

  // The member's All Stats screen. SyncAllStats first, so it opens on the
  // allocation their Character panel is reading.
  void SyncAllStats();
  ftxui::Element RenderAllStats() const;
  bool OnAllStatsEvent(const ftxui::Event& event);

  // The member as this build reads them, for whoever needs their stats.
  const CharacterInstance& character() const {
    return character_;
  }
  // The item the cursor is on, or null with nothing worn or the cursor on a
  // bar.
  const EquipInstance* selected_item() const;
  // Which of the member's two allocations is being read.
  Activity preset() const;

 private:
  // Builds both panels and their components over character_. Called again for
  // a new member: the panels hold a cursor and an open tab, and neither
  // belongs to anybody but the member it was opened on.
  void BuildPanels();

  GameState& state_;
  // The member, rebuilt from their sheet. Held rather than rebuilt per frame:
  // a sheet arrives when something about them changes, not every tick. The
  // panels hold a reference to it, so it outlives them by declaration order.
  CharacterInstance character_;
  // The member as the lobby last described them, so a tick that changed
  // nothing does not rebuild them.
  PlayerInfo shown_;
  PlayerInspectActions actions_;
  // Which panel has the cursor, in the main screen's own terms -- the panels
  // read it. This screen's alone: walking it must not move the cursor on the
  // main view behind it.
  int focus_ = kEquipPanel;
  bool expanded_ = false;
  // Rebuilt per member, so they are held by pointer; the components capture
  // references into them and are replaced together.
  std::unique_ptr<CharacterPanel> char_panel_;
  std::unique_ptr<EquippedPanel> equip_panel_;
  ftxui::Component char_component_;
  ftxui::Component equip_component_;
  // The member's own All Stats screen, over the same character.
  AllStatsPanel stats_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PLAYER_INSPECT_PANEL_H_
