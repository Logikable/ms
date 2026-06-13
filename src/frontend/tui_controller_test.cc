#include "src/frontend/tui_controller.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/ap_alloc_panel.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

class TuiControllerTest : public testing::Test {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(3);
    sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);

    Scroll scroll;
    scroll.set_name("Test Scroll");
    scroll.set_success_rate(100);
    scroll.set_tier(SCROLL_TIER_1);
    scroll.mutable_stats()->set_attack(5);
    scroll.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);

    std::map<std::string, EquipPrototype> equips;
    equips["Sword"] = sword_;
    std::map<std::string, Scroll> scrolls;
    scrolls["Test Scroll"] = scroll;

    state_ = std::make_unique<GameState>(std::move(equips), std::move(scrolls));
    state_->character.AdvanceJob(JOB_WARRIOR);
    equip_panel_ =
        std::make_unique<EquippedPanel>(state_->character, panel_focus_);
    bag_panel_ = std::make_unique<BagPanel>(state_->character, panel_focus_);
    scroll_panel_ = std::make_unique<ScrollPanel>(state_->scrolls);
    ap_alloc_panel_ = std::make_unique<ApAllocPanel>(state_->character);
    controller_ = std::make_unique<TuiController>(
        *state_, *equip_panel_, *bag_panel_, *scroll_panel_, *ap_alloc_panel_,
        panel_focus_);

    // Build the equip component so RenderEquipPanel() can populate slots_.
    equip_component_ = equip_panel_->MakeComponent([]() {});
  }

  // Renders equip_panel_ to sync its slots_ vector with character.equipped().
  // Must be called after any change to the equipped map before using
  // selected_slot() (either directly or via controller_ OnEvent).
  void RenderEquipPanel() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                              ftxui::Dimension::Fixed(5));
    ftxui::Render(scr, equip_component_->Render());
  }

  // Picks up sword_ with all upgrade slots consumed (required for star force).
  void PickUpScrolledSword() {
    sword_.set_upgrade_slots(0);
    state_->character.PickUp(sword_);
  }

  // Replaces the scroll map with a single 0%-rate scroll and rebuilds
  // scroll_panel_ and controller_ to pick up the change.
  void UseFailScroll() {
    Scroll fail;
    fail.set_name("Fail Scroll");
    fail.set_success_rate(0);
    fail.set_tier(SCROLL_TIER_1);
    fail.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    fail.mutable_stats()->set_attack(5);
    state_->scrolls.clear();
    state_->scrolls["Fail Scroll"] = fail;
    scroll_panel_ = std::make_unique<ScrollPanel>(state_->scrolls);
    controller_ = std::make_unique<TuiController>(
        *state_, *equip_panel_, *bag_panel_, *scroll_panel_, *ap_alloc_panel_,
        panel_focus_);
  }

  int panel_focus_ = kEquipPanel;
  EquipPrototype sword_;
  std::unique_ptr<GameState> state_;
  std::unique_ptr<EquippedPanel> equip_panel_;
  std::unique_ptr<BagPanel> bag_panel_;
  std::unique_ptr<ScrollPanel> scroll_panel_;
  std::unique_ptr<ApAllocPanel> ap_alloc_panel_;
  std::unique_ptr<TuiController> controller_;
  ftxui::Component equip_component_;
};

// --- Tab ---

TEST_F(TuiControllerTest, TabSwitchesToBagPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kBagPanel);
}

TEST_F(TuiControllerTest, TabSwitchesToCharPanel) {
  state_->character.LevelUp();  // grants AP
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kCharPanel);
}

TEST_F(TuiControllerTest, TabCyclesBackToEquipPanel) {
  state_->character.LevelUp();  // grants AP
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

TEST_F(TuiControllerTest, TabSkipsCharPanelWhenNoAp) {
  state_->character.AllocateAllStat(STAT_FIELD_STR);  // drain AP
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// --- AP allocation ---

TEST_F(TuiControllerTest, OpenApAllocSetsScreenToApAlloc) {
  controller_->OpenApAlloc();
  EXPECT_EQ(controller_->screen(), kApAlloc);
}

TEST_F(TuiControllerTest, EscapeInApAllocGoesToMain) {
  controller_->OpenApAlloc();
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// --- ItemMenu navigation ---

TEST_F(TuiControllerTest, EscapeInItemMenuGoesToMain) {
  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, ArrowDownInItemMenuAdvancesMenuSelection) {
  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(equip_panel_->menu().selected(), 1);
}

TEST_F(TuiControllerTest, InspectActionGoesToInspect) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kInspect);
}

TEST_F(TuiControllerTest, EscapeInInspectGoesToMain) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
}

// --- Unequip ---

TEST_F(TuiControllerTest, ReturnActionUnequipsFromEquipPanel) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, UnequipSwitchesFocusToBagWhenEquipEmpty) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(panel_focus_, kBagPanel);
}

// --- Scroll via equip panel ---

TEST_F(TuiControllerTest, ReturnScrollFromEquipPanelGoesToScrollSelect) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollSelectGoesToItemMenu) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest,
       ReturnInScrollSelectAppliesScrollAndGoesToScrollResult) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // apply scroll

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .equip_state()
                .scroll_stats()
                .attack(),
            5);
}

TEST_F(TuiControllerTest, ScrollResultStoresOutcome) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest,
       NoSlotsRemainingGoesToScrollResultWithNoSlotsOutcome) {
  sword_.set_upgrade_slots(0);
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

TEST_F(TuiControllerTest, EnterInScrollResultGoesToScrollSelectIfSlotsRemain) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollResultGoesToScrollSelect) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, ScrollResultSlotsRemainingIsDecrementedOnSuccess) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  // sword_ has 3 upgrade slots; one was consumed.
  EXPECT_EQ(controller_->scroll_result().slots_remaining, 2);
}

TEST_F(TuiControllerTest, FailedScrollStoresFailOutcome) {
  UseFailScroll();
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // apply scroll

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollFail);
}

// --- Scroll via bag panel ---

TEST_F(TuiControllerTest, BagScrollGoesToScrollSelect) {
  state_->character.PickUp(sword_);
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, BagScrollEscapeFromScrollSelectGoesToItemMenu) {
  state_->character.PickUp(sword_);
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest, BagScrollAppliesScrollToInventory) {
  state_->character.PickUp(sword_);
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // apply scroll

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(static_cast<const EquipInstance&>(*state_->character.inventory()[0])
                .equip_state()
                .scroll_stats()
                .attack(),
            5);
}

TEST_F(TuiControllerTest, BagScrollResultStoresOutcome) {
  state_->character.PickUp(sword_);
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest,
       BagScrollNoSlotsGoesToScrollResultWithNoSlotsOutcome) {
  sword_.set_upgrade_slots(0);
  state_->character.PickUp(sword_);
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // attempt scroll

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

// --- Star Force via equip panel ---

TEST_F(TuiControllerTest, StarForceActionGoesToStarForce) {
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kStarForce);
}

TEST_F(TuiControllerTest, EscapeInStarForceGoesToMain) {
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EnterInStarForceGoesToStarForceResult) {
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  controller_->OnEvent(ftxui::Event::Return);  // attempt

  EXPECT_EQ(controller_->screen(), kStarForceResult);
}

TEST_F(TuiControllerTest, StarForceResultStoresEquipNameAndStarsBefore) {
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->star_force_result().equip_name, "Sword");
  EXPECT_EQ(controller_->star_force_result().stars_before, 0);
}

TEST_F(TuiControllerTest, EnterInStarForceResultSuccessGoesToStarForce) {
  // At 0★ the success rate is 95%, so with a seeded rng the first attempt
  // will succeed. We just verify the screen transition, not the outcome.
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);  // attempt

  // If the item was not destroyed, dismissing the result goes back to
  // kStarForce.
  if (controller_->star_force_result().outcome != kStarForceDestroy) {
    controller_->OnEvent(ftxui::Event::Return);
    EXPECT_EQ(controller_->screen(), kStarForce);
  }
}

// --- Star Force via bag panel ---

TEST_F(TuiControllerTest, BagStarForceGoesToStarForce) {
  PickUpScrolledSword();
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kStarForce);
}

TEST_F(TuiControllerTest, BagStarForceAttemptGoesToStarForceResult) {
  PickUpScrolledSword();
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  controller_->OnEvent(ftxui::Event::Return);  // attempt

  EXPECT_EQ(controller_->screen(), kStarForceResult);
  EXPECT_EQ(controller_->star_force_result().equip_name, "Sword");
}

// --- Equip via bag panel ---

TEST_F(TuiControllerTest, ReturnActionEquipsFromBagPanel) {
  state_->character.PickUp(sword_);
  panel_focus_ = kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), kMain);
}

}  // namespace
}  // namespace ms
