/* TuiController owns the screen-state machine for the TUI. It handles
 * keyboard events and drives transitions between screens. Tui holds a
 * TuiController and delegates event handling; tests can construct
 * TuiController directly without the ftxui event loop.
 *
 * panel_focus is owned by the caller and shared with the panel components
 * so Container::Tab can read it; TuiController mutates it on Tab.
 */
#ifndef MS_SRC_FRONTEND_TUI_CONTROLLER_H_
#define MS_SRC_FRONTEND_TUI_CONTROLLER_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "src/equip_instance.h"
#include "src/frontend/amount_selector.h"
#include "src/frontend/buy_panel.h"
#include "src/frontend/confirm_prompt.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/inventory_panel.h"
#include "src/frontend/item_ref.h"
#include "src/frontend/map_select_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/sell_panel.h"
#include "src/frontend/shop_panel.h"
#include "src/frontend/star_force_panel.h"
#include "src/frontend/trace_recover_panel.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

class TuiController {
 public:
  // panel_focus is a reference shared with panel components and
  // Container::Tab; the controller mutates it as focus changes.
  TuiController(GameState& state, EquippedPanel& equip_panel,
                InventoryPanel& inventory_panel, ScrollPanel& scroll_panel,
                StarForcePanel& star_force_panel,
                TraceRecoverPanel& trace_recover_panel, SellPanel& sell_panel,
                MapSelectPanel& map_select_panel, ShopPanel& shop_panel,
                BuyPanel& buy_panel, int& panel_focus);

  // Open the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  // Enter in the bag: the context menu on an item, or the shop when the Shop
  // tab is the one showing.
  void OpenInventoryMenu();
  // Float the AP-allocation amount entry over the main view, seeded to spend up
  // to all available AP on `field` (defaulting to the max).
  void OpenApAllocate(StatField field);
  // Float the skill-learning amount entry over the main view, seeded to spend
  // up to the most points `skill` can still take (its stage SP, capped at how
  // far it is below max level).
  void OpenSkillLearn(const Skill& skill);
  // Open the skill's inspect screen. Copies the skill, as the learn dialog
  // does, so nothing downstream depends on the catalog outliving the screen.
  void OpenSkillInspect(const Skill& skill);
  // Float the job-advancement confirmation over the main view. The prompt opens
  // on Cancel: the choice cannot be taken back.
  void OpenJobAdvance(Job job);
  // Open the map selection screen, on the map being farmed.
  void OpenMapSelect();

  // The stat the pending AP allocation targets, and its amount selector, for
  // the dialog Tui floats over the main view.
  StatField ap_alloc_field() const {
    return ap_field_;
  }
  const AmountSelector& ap_selector() const {
    return ap_selector_;
  }
  // The skill the pending learn targets, and its amount selector, for the
  // dialog Tui floats over the main view.
  const Skill& skill_learn_skill() const {
    return skill_learn_;
  }
  const AmountSelector& sp_selector() const {
    return sp_selector_;
  }

  // The skill being inspected while in kSkillInspect, and the level the
  // character has it at, for the screen Tui draws.
  const Skill& skill_inspect_skill() const {
    return skill_inspect_;
  }
  int skill_inspect_level() const;

  // The job the pending advancement would take, and its prompt, for the dialog
  // Tui floats over the main view.
  Job job_advance_job() const {
    return job_advance_;
  }
  const ConfirmPrompt& job_advance_prompt() const {
    return job_advance_prompt_;
  }

  // Returns true if the event was consumed.
  bool OnEvent(ftxui::Event event);

  Screen screen() const {
    return screen_;
  }
  const ScrollResult& scroll_result() const {
    return scroll_result_;
  }
  const StarForceResult& star_force_result() const {
    return star_force_result_;
  }
  const TraceRecoveryResult& trace_recovery_result() const {
    return trace_recovery_result_;
  }
  // Returns the item being scrolled while in kScrollSelect or kScrollResult,
  // or nullptr otherwise.
  const EquipInstance* scroll_item() const;
  // Returns the item being inspected while in kInspect, or nullptr otherwise.
  // May be an EquipTrace if the selected bag item was destroyed.
  const EquipTabItem* inspect_item() const;
  // Returns the item being star forced while in kStarForce, or nullptr
  // otherwise. Do not call in kStarForceResult (item may be destroyed).
  const EquipInstance* star_force_item() const;
  // Returns the trace being recovered while in kTraceRecover, or nullptr.
  const EquipTabItem* trace_recover_item() const;

 private:
  // Where the item under the cursor of the focused panel lives. The one place
  // that reads panel_focus_ to answer that question.
  ItemRef SelectedItem() const;

  bool OnItemMenuEvent(ftxui::Event event);
  bool OnInspectEvent(ftxui::Event event);
  bool OnScrollSelectEvent(ftxui::Event event);
  bool OnScrollResultEvent(ftxui::Event event);
  bool OnApAllocEvent(ftxui::Event event);
  bool OnSkillLearnEvent(ftxui::Event event);
  bool OnSkillInspectEvent(ftxui::Event event);
  bool OnJobAdvanceEvent(ftxui::Event event);
  bool OnStarForceEvent(ftxui::Event event);
  bool OnStarForceResultEvent(ftxui::Event event);
  bool OnTraceRecoverEvent(ftxui::Event event);
  bool OnTraceRecoverResultEvent(ftxui::Event event);
  bool OnSellEvent(ftxui::Event event);
  bool OnMapSelectEvent(ftxui::Event event);
  bool OnShopEvent(ftxui::Event event);
  bool OnShopBuyEvent(ftxui::Event event);

  GameState& state_;
  EquippedPanel& equip_panel_;
  InventoryPanel& inventory_panel_;
  ScrollPanel& scroll_panel_;
  StarForcePanel& star_force_panel_;
  TraceRecoverPanel& trace_recover_panel_;
  SellPanel& sell_panel_;
  MapSelectPanel& map_select_panel_;
  ShopPanel& shop_panel_;
  BuyPanel& buy_panel_;
  // Catalog key of the item the buy dialog is open on, so the purchase reads
  // the prototype rather than trusting a pointer to outlive the screen.
  std::string buy_item_;
  int& panel_focus_;
  Screen screen_ = kMain;
  // Each modal remembers the item it was opened on. Where that item lives is
  // settled once, when the player picks it, so nothing downstream has to ask
  // which panel had focus at the time.
  ItemRef scroll_ref_;
  ItemRef inspect_ref_;
  ItemRef star_force_ref_;
  // Recovery is a bag-only affair: a trace cannot be worn.
  int trace_index_ = 0;
  ItemCategory sell_category_ = ITEM_CATEGORY_UNSPECIFIED;
  int sell_index_ = 0;
  StatField ap_field_ = STAT_FIELD_UNSPECIFIED;
  AmountSelector ap_selector_;
  Skill skill_learn_;
  AmountSelector sp_selector_;
  Skill skill_inspect_;
  Job job_advance_ = JOB_UNSPECIFIED;
  ConfirmPrompt job_advance_prompt_;
  ScrollResult scroll_result_;
  StarForceResult star_force_result_;
  TraceRecoveryResult trace_recovery_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
