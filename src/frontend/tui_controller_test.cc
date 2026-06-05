#include "src/frontend/tui_controller.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"
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

    Scroll scroll;
    scroll.set_name("Test Scroll");
    scroll.set_success_rate(100);
    scroll.mutable_stats()->set_attack(5);

    std::map<std::string, EquipPrototype> equips;
    equips["Sword"] = sword_;
    std::map<std::string, Scroll> scrolls;
    scrolls["Test Scroll"] = scroll;

    state_ = std::make_unique<GameState>(std::move(equips), std::move(scrolls));
    equip_panel_ =
        std::make_unique<EquippedPanel>(state_->character, panel_focus_);
    bag_panel_ = std::make_unique<BagPanel>(state_->character, panel_focus_);
    scroll_panel_ = std::make_unique<ScrollPanel>(state_->scrolls);
    controller_ = std::make_unique<TuiController>(
        *state_, *equip_panel_, *bag_panel_, *scroll_panel_, panel_focus_);

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

  int panel_focus_ = TuiController::kEquipPanel;
  EquipPrototype sword_;
  std::unique_ptr<GameState> state_;
  std::unique_ptr<EquippedPanel> equip_panel_;
  std::unique_ptr<BagPanel> bag_panel_;
  std::unique_ptr<ScrollPanel> scroll_panel_;
  std::unique_ptr<TuiController> controller_;
  ftxui::Component equip_component_;
};

// --- Tab ---

TEST_F(TuiControllerTest, TabSwitchesToBagPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, TuiController::kBagPanel);
}

TEST_F(TuiControllerTest, TabCyclesBackToEquipPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, TuiController::kEquipPanel);
}

// --- ItemMenu navigation ---

TEST_F(TuiControllerTest, EscapeInItemMenuGoesToMain) {
  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), TuiController::kMain);
}

TEST_F(TuiControllerTest, ArrowDownInItemMenuAdvancesMenuSelection) {
  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->active_menu().selected(), 1);
}

// --- Unequip ---

TEST_F(TuiControllerTest, ReturnActionUnequipsFromEquipPanel) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), TuiController::kMain);
}

TEST_F(TuiControllerTest, UnequipSwitchesFocusToBagWhenEquipEmpty) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(panel_focus_, TuiController::kBagPanel);
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

  EXPECT_EQ(controller_->screen(), TuiController::kScrollSelect);
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

  EXPECT_EQ(controller_->screen(), TuiController::kItemMenu);
}

TEST_F(TuiControllerTest, ReturnInScrollSelectAppliesScrollAndGoesToMain) {
  state_->character.PickUp(sword_);
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // apply scroll

  EXPECT_EQ(controller_->screen(), TuiController::kMain);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .proto()
                .scroll_stats()
                .attack(),
            5);
}

// --- Equip via bag panel ---

TEST_F(TuiControllerTest, ReturnActionEquipsFromBagPanel) {
  state_->character.PickUp(sword_);
  panel_focus_ = TuiController::kBagPanel;

  controller_->OpenBagMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), TuiController::kMain);
}

}  // namespace
}  // namespace ms
