/* Tui owns the ftxui event loop and all panel and menu state for one game
 * session. Construct with a GameState reference, then call Run() which blocks
 * until the user exits (Ctrl-C).
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include "ftxui/component/component.hpp"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"

namespace ms {

class Tui {
 public:
  explicit Tui(GameState& state);
  void Run();

 private:
  enum Screen : int { kMain, kItemMenu, kScrollSelect };
  enum Panel : int { kEquipPanel = 0, kBagPanel = 1 };
  enum MenuItem : int { kMenuAction = 0, kMenuInspect = 1, kMenuScroll = 2 };

  ftxui::Element RenderFrame();
  bool OnEvent(ftxui::Event event);

  GameState& state_;
  // Which panel has keyboard focus. Tab cycles between them in kMain and
  // determines which panel's item menu opens on Enter.
  int panel_focus_ = kEquipPanel;
  // Current screen state. kMain: normal layout. kItemMenu: item context menu
  // overlay. kScrollSelect: full-screen scroll list.
  Screen screen_ = kMain;

  // Main view: always visible in kMain and kItemMenu modes.
  CharacterPanel char_panel_;
  EquippedPanel equip_panel_;
  BagPanel bag_panel_;
  // Built in Run(); held here so RenderFrame() can call ->Render().
  ftxui::Component equip_component_;
  ftxui::Component bag_component_;

  // Item context menu (kItemMenu mode).
  ItemMenu equip_menu_;
  ItemMenu bag_menu_;
  ItemMenu* active_menu_;  // points to whichever menu was opened last

  // Scroll screen (kScrollSelect mode).
  ScrollPanel scroll_panel_;
  ftxui::Component scroll_component_;
  // Saved on entry to kScrollSelect; used on Enter to identify the target.
  EquipSlot scroll_slot_ = EQUIP_SLOT_UNSPECIFIED;  // equip panel path
  int scroll_index_ = 0;                            // bag panel path
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
