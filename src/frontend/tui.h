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
  enum Mode { kMain, kItemMenu, kScrollSelect };

  ftxui::Element RenderFrame();
  bool OnEvent(ftxui::Event event);

  GameState& state_;
  int panel_focus_ = 0;
  Mode mode_ = kMain;
  CharacterPanel char_panel_;
  EquippedPanel equip_panel_;
  BagPanel bag_panel_;
  ItemMenu equip_menu_;
  ItemMenu bag_menu_;
  ItemMenu* active_menu_;
  ScrollPanel scroll_panel_;
  EquipSlot scroll_slot_ = EQUIP_SLOT_UNSPECIFIED;
  ftxui::Component equip_component_;
  ftxui::Component bag_component_;
  ftxui::Component scroll_component_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
