#include "src/frontend/tui_controller.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/progression.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

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
    // A stand-in under the catalog key a Magician's advancement asks for, so
    // the advancement tests see gear arrive without loading data/equip.
    EquipPrototype wand;
    wand.set_name("Wooden Wand");
    wand.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    equips["wooden_wand"] = wand;
    // Something for the shop to stock. ShopPanel fixes its list at
    // construction, so this has to exist before the panel does.
    EquipPrototype machete;
    machete.set_name("Machete");
    machete.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    machete.set_required_level(20);
    machete.set_shop_price(10000);
    equips["machete"] = machete;
    std::map<std::string, Scroll> scrolls;
    scrolls["Test Scroll"] = scroll;

    state_ = std::make_unique<GameState>(std::move(equips), std::move(scrolls),
                                         std::map<std::string, ItemPrototype>{},
                                         std::map<std::string, Mob>{},
                                         std::map<std::string, MapData>{});
    state_->character.AdvanceJob(JOB_SWORDMAN);
    // The starting character stands at its advancement with no SP yet; these
    // tests spend SP, so they level far enough to have some of their own.
    while (state_->character.sp(1) < 60) {
      state_->character.LevelUp();
    }
    equip_panel_ =
        std::make_unique<EquippedPanel>(state_->character, panel_focus_);
    inventory_panel_ =
        std::make_unique<InventoryPanel>(state_->character, panel_focus_);
    scroll_panel_ = std::make_unique<ScrollPanel>(state_->scrolls);
    star_force_panel_ = std::make_unique<StarForcePanel>();
    trace_recover_panel_ =
        std::make_unique<TraceRecoverPanel>(state_->character);
    sell_panel_ = std::make_unique<SellPanel>();
    map_select_panel_ = std::make_unique<MapSelectPanel>(*state_);
    shop_panel_ =
        std::make_unique<ShopPanel>(state_->character, state_->equips);
    buy_panel_ = std::make_unique<BuyPanel>();
    controller_ = std::make_unique<TuiController>(
        *state_, *equip_panel_, *inventory_panel_, *scroll_panel_,
        *star_force_panel_, *trace_recover_panel_, *sell_panel_,
        *map_select_panel_, *shop_panel_, *buy_panel_, panel_focus_);

    // Build the equip component so RenderEquipPanel() can populate slots_.
    equip_component_ = equip_panel_->MakeComponent([]() {});
    // The inventory component drives tab switching and opens the context menu.
    inventory_component_ = inventory_panel_->MakeComponent(
        [this]() { controller_->OpenInventoryMenu(); });
  }

  // Levels the character to `level`. The item menu's entries are level-gated
  // (see progression.h), so a test that means to exercise one has to have
  // reached it -- star force is the late one, at 60.
  void LevelTo(int level) {
    while (state_->character.proto().level() < level) {
      state_->character.LevelUp();
    }
  }

  // Adds a sellable Etc stack and navigates the inventory to the Etc tab.
  void EnterEtcTabWithStack(int count, int sell_price) {
    ItemPrototype shell;
    shell.set_name("Green Snail Shell");
    shell.set_category(ITEM_CATEGORY_ETC);
    shell.set_sell_price(sell_price);
    state_->character.AddStackable(shell, count);
    panel_focus_ = kInventoryPanel;
    inventory_component_->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
    inventory_component_->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
    inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> stack
  }

  // Opens the stack context menu and walks to Sell, leaving the sell dialog
  // up. Inspect leads that menu, so Return alone lands on the wrong screen --
  // and quietly enough that a test asserting nothing was sold would pass.
  void OpenStackSell() {
    inventory_component_->OnEvent(ftxui::Event::Return);  // the stack menu
    controller_->OnEvent(ftxui::Event::ArrowDown);        // Inspect -> Sell
    controller_->OnEvent(ftxui::Event::Return);
  }

  // Walks the tab bar to the Shop tab and opens it, leaving the shop screen up
  // with the cursor on the first item.
  void OpenShop() {
    // The Shop tab is gated at 20. The fixture already levels past that
    // buying SP, but that is a coincidence of the SP arithmetic and not
    // something the shop tests should be resting on.
    LevelTo(20);
    panel_focus_ = kInventoryPanel;
    for (int i = 0; i < 3; ++i) {
      inventory_component_->OnEvent(ftxui::Event::ArrowRight);
    }
    inventory_component_->OnEvent(ftxui::Event::Return);
  }

  // Opens the shop, then the menu on the first item, then its Buy entry,
  // leaving the buy dialog up with the quantity field focused.
  void OpenBuyDialog() {
    OpenShop();
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopMenu, on Inspect
    controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Buy
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopBuy
  }

  // MapSelectPanel fixes its display order at construction, so the maps must
  // exist before it does -- rebuild both after touching state_->maps.
  void RebuildMapSelect() {
    map_select_panel_ = std::make_unique<MapSelectPanel>(*state_);
    controller_ = std::make_unique<TuiController>(
        *state_, *equip_panel_, *inventory_panel_, *scroll_panel_,
        *star_force_panel_, *trace_recover_panel_, *sell_panel_,
        *map_select_panel_, *shop_panel_, *buy_panel_, panel_focus_);
  }

  // Adds a map on the second level band, so paging has somewhere to go. The
  // two from LoadTwoMaps both sit on the first.
  void AddMapOnTheSecondBand() {
    Mob golem;
    golem.set_name("Stone Golem");
    golem.set_level(15);
    state_->mobs["golem"] = golem;

    MapData temple;
    temple.set_name("Temple");
    MapData::Spawn* golems = temple.add_spawns();
    golems->set_mob("golem");
    golems->set_count(3);
    state_->maps["temple"] = temple;

    RebuildMapSelect();
  }

  // Loads two maps, both on the first level band. Field, holding the level 1
  // snail, sorts ahead of Cave, holding the level 8 mushroom.
  void LoadTwoMaps() {
    Mob snail;
    snail.set_name("Snail");
    snail.set_level(1);
    Mob mushroom;
    mushroom.set_name("Horny Mushroom");
    mushroom.set_level(8);
    state_->mobs["snail"] = snail;
    state_->mobs["mushroom"] = mushroom;

    MapData field;
    field.set_name("Field");
    MapData::Spawn* snails = field.add_spawns();
    snails->set_mob("snail");
    snails->set_count(1);
    MapData cave;
    cave.set_name("Cave");
    MapData::Spawn* mushrooms = cave.add_spawns();
    mushrooms->set_mob("mushroom");
    mushrooms->set_count(1);
    state_->maps["field"] = field;
    state_->maps["cave"] = cave;

    RebuildMapSelect();
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
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
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
        *state_, *equip_panel_, *inventory_panel_, *scroll_panel_,
        *star_force_panel_, *trace_recover_panel_, *sell_panel_,
        *map_select_panel_, *shop_panel_, *buy_panel_, panel_focus_);
  }

  int panel_focus_ = kEquipPanel;
  EquipPrototype sword_;
  std::unique_ptr<GameState> state_;
  std::unique_ptr<EquippedPanel> equip_panel_;
  std::unique_ptr<InventoryPanel> inventory_panel_;
  std::unique_ptr<ScrollPanel> scroll_panel_;
  std::unique_ptr<StarForcePanel> star_force_panel_;
  std::unique_ptr<TraceRecoverPanel> trace_recover_panel_;
  std::unique_ptr<SellPanel> sell_panel_;
  std::unique_ptr<MapSelectPanel> map_select_panel_;
  std::unique_ptr<ShopPanel> shop_panel_;
  std::unique_ptr<BuyPanel> buy_panel_;
  std::unique_ptr<TuiController> controller_;
  ftxui::Component equip_component_;
  ftxui::Component inventory_component_;
};

// --- Tab ---

// Focus starts on the equipped panel and Tab runs clockwise through every
// panel: equipped -> inventory -> combat -> character -> back to equipped. The
// character panel is always focusable, so no panel is ever skipped.

TEST_F(TuiControllerTest, TabSwitchesToInventoryPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kInventoryPanel);
}

TEST_F(TuiControllerTest, TabSwitchesToCombatPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kCombatPanel);
}

TEST_F(TuiControllerTest, TabSwitchesToCharPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kCharPanel);
}

TEST_F(TuiControllerTest, TabCyclesBackToEquipPanel) {
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// --- AP allocation ---

TEST_F(TuiControllerTest, OpenApAllocateSetsScreenToApAlloc) {
  controller_->OpenApAllocate(STAT_FIELD_STR);
  EXPECT_EQ(controller_->screen(), kApAlloc);
}

TEST_F(TuiControllerTest, ConfirmAllocatesAllAvailableApByDefault) {
  state_->character.LevelUp();  // grants AP to spend
  int ap = state_->character.proto().ap();
  ASSERT_GT(ap, 0);
  int str_before = state_->character.proto().allocated_stats().str();
  controller_->OpenApAllocate(STAT_FIELD_STR);    // amount defaults to max
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().allocated_stats().str(), str_before + ap);
  EXPECT_EQ(state_->character.proto().ap(), 0);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, ConfirmAllocatesTheChosenAmount) {
  state_->character.LevelUp();
  int ap = state_->character.proto().ap();
  ASSERT_GT(ap, 1);
  int str_before = state_->character.proto().allocated_stats().str();
  controller_->OpenApAllocate(STAT_FIELD_STR);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  controller_->OnEvent(ftxui::Event::Return);     // amount 1
  controller_->OnEvent(ftxui::Event::ArrowDown);  // [1] -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().allocated_stats().str(), str_before + 1);
  EXPECT_EQ(state_->character.proto().ap(), ap - 1);
}

TEST_F(TuiControllerTest, CancelInApAllocDoesNotAllocate) {
  state_->character.LevelUp();
  int str_before = state_->character.proto().allocated_stats().str();
  controller_->OpenApAllocate(STAT_FIELD_STR);
  controller_->OnEvent(ftxui::Event::ArrowDown);   // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::ArrowRight);  // [Confirm] -> [Cancel]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().allocated_stats().str(), str_before);
  EXPECT_EQ(controller_->screen(), kMain);
}

Skill SlashBlast() {
  Skill skill;
  skill.set_name("Slash Blast");
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  return skill;
}

TEST_F(TuiControllerTest, OpenSkillLearnSetsScreenToSkillLearn) {
  controller_->OpenSkillLearn(SlashBlast());
  EXPECT_EQ(controller_->screen(), kSkillLearn);
}

TEST_F(TuiControllerTest, ConfirmLearnsTheChosenPoints) {
  Skill skill = SlashBlast();
  int sp_before = state_->character.sp(1);
  ASSERT_GT(sp_before, 1);
  controller_->OpenSkillLearn(skill);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  controller_->OnEvent(ftxui::Event::Return);     // amount 1
  controller_->OnEvent(ftxui::Event::ArrowDown);  // [1] -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.skill_level(skill), 1);
  EXPECT_EQ(state_->character.sp(1), sp_before - 1);
  EXPECT_EQ(controller_->screen(), kMain);
}

// --- Skill inspect ---

TEST_F(TuiControllerTest, OpenSkillInspectSetsScreenToSkillInspect) {
  controller_->OpenSkillInspect(SlashBlast());
  EXPECT_EQ(controller_->screen(), kSkillInspect);
  EXPECT_EQ(controller_->skill_inspect_skill().name(), "Slash Blast");
}

TEST_F(TuiControllerTest, SkillInspectReportsTheLearnedLevel) {
  Skill skill = SlashBlast();
  ASSERT_TRUE(state_->character.LearnSkill(skill, 3));
  controller_->OpenSkillInspect(skill);
  EXPECT_EQ(controller_->skill_inspect_level(), 3);
}

TEST_F(TuiControllerTest, SkillInspectReportsZeroForAnUnlearnedSkill) {
  controller_->OpenSkillInspect(SlashBlast());
  EXPECT_EQ(controller_->skill_inspect_level(), 0);
}

// The level is read live, not captured when the screen opened, so a point
// spent between two looks shows up on the second.
TEST_F(TuiControllerTest, SkillInspectFollowsAPointSpentWhileItIsOpen) {
  Skill skill = SlashBlast();
  controller_->OpenSkillInspect(skill);
  ASSERT_EQ(controller_->skill_inspect_level(), 0);
  ASSERT_TRUE(state_->character.LearnSkill(skill, 2));
  EXPECT_EQ(controller_->skill_inspect_level(), 2);
}

TEST_F(TuiControllerTest, EscapeLeavesTheSkillInspectScreen) {
  controller_->OpenSkillInspect(SlashBlast());
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Reading is all there is to do, so Enter leaves too rather than sitting there
// doing nothing.
TEST_F(TuiControllerTest, EnterAlsoLeavesTheSkillInspectScreen) {
  controller_->OpenSkillInspect(SlashBlast());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// --- Job advancement ---

TEST_F(TuiControllerTest, OpenJobAdvanceFloatsTheConfirmation) {
  controller_->OpenJobAdvance(JOB_ROGUE);
  EXPECT_EQ(controller_->screen(), kJobAdvance);
  EXPECT_EQ(controller_->job_advance_job(), JOB_ROGUE);
}

// The prompt opens on Cancel, so a stray Enter backs out rather than picking a
// job the player cannot un-pick.
TEST_F(TuiControllerTest, EnterAloneDoesNotAdvance) {
  Job before = state_->character.proto().job();
  controller_->OpenJobAdvance(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().job(), before);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, ConfirmingAdvancesAndHandsOverTheGear) {
  int bag_before = state_->character.inventory().size();
  controller_->OpenJobAdvance(JOB_MAGICIAN);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // [Cancel] -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().job(), JOB_MAGICIAN);
  EXPECT_EQ(state_->character.proto().allocated_stats().int_(), 25);
  EXPECT_GT(state_->character.inventory().size(), bag_before);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EscapeLeavesTheJobAlone) {
  Job before = state_->character.proto().job();
  controller_->OpenJobAdvance(JOB_ARCHER);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(state_->character.proto().job(), before);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, ConfirmStopsAtTheSkillsMaxLevelByDefault) {
  Skill skill = SlashBlast();
  int sp_before = state_->character.sp(1);
  ASSERT_GT(sp_before, skill.max_level());  // the skill's cap is what binds
  controller_->OpenSkillLearn(skill);       // amount defaults to max
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.skill_level(skill), skill.max_level());
  EXPECT_EQ(state_->character.sp(1), sp_before - skill.max_level());
}

TEST_F(TuiControllerTest, ConfirmSpendsEveryPointWhenSpIsWhatBinds) {
  Skill skill = SlashBlast();
  skill.set_max_level(1000);  // out of reach, so the SP pool is the limit
  int sp_before = state_->character.sp(1);
  ASSERT_GT(sp_before, 0);
  controller_->OpenSkillLearn(skill);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.skill_level(skill), sp_before);
  EXPECT_EQ(state_->character.sp(1), 0);
}

TEST_F(TuiControllerTest, EscapeInSkillLearnGoesToMainWithoutLearning) {
  Skill skill = SlashBlast();
  controller_->OpenSkillLearn(skill);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(state_->character.skill_level(skill), 0);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EscapeInApAllocGoesToMainWithoutAllocating) {
  state_->character.LevelUp();
  int str_before = state_->character.proto().allocated_stats().str();
  controller_->OpenApAllocate(STAT_FIELD_STR);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(state_->character.proto().allocated_stats().str(), str_before);
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
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kInspect);
}

TEST_F(TuiControllerTest, EscapeInInspectGoesToMain) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
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
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), kMain);
}

// Both panels used to shove focus at the other one when their own list ran
// out, because an empty list left the panel unable to receive a key. Neither
// does now, and the player keeps the cursor they were holding.
TEST_F(TuiControllerTest, UnequippingTheLastItemLeavesFocusWhereItWas) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  ASSERT_TRUE(state_->character.equipped().empty());
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// --- Scroll via equip panel ---

TEST_F(TuiControllerTest, ReturnScrollFromEquipPanelGoesToScrollSelect) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollSelectGoesToItemMenu) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
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
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .equip_state()
                .scroll_stats()
                .attack(),
            5);
}

TEST_F(TuiControllerTest, ScrollResultStoresOutcome) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest,
       NoSlotsRemainingGoesToScrollResultWithNoSlotsOutcome) {
  sword_.set_upgrade_slots(0);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // bypass confirm (0 slots)

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

TEST_F(TuiControllerTest,
       ReturnToItemMenuAfterScrollingEnablesStarForceWhenSlotsDepleted) {
  LevelTo(60);  // star force is gated there
  sword_.set_upgrade_slots(1);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  // Open menu while item still has 1 slot — Star Force should be disabled.
  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open confirm bar
  controller_->OnEvent(
      ftxui::Event::Return);  // confirm → kScrollResult (0 slots)
  controller_->OnEvent(ftxui::Event::Escape);  // → kScrollSelect
  controller_->OnEvent(ftxui::Event::Escape);  // → kItemMenu (re-opens menu)

  EXPECT_EQ(controller_->screen(), kItemMenu);
  // Navigate to Star Force position; it must be reachable (not skipped).
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  EXPECT_EQ(equip_panel_->menu().selected(), kMenuStarForce);
}

TEST_F(TuiControllerTest, EnterInScrollResultGoesToScrollSelectIfSlotsRemain) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm
  controller_->OnEvent(ftxui::Event::Return);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollResultGoesToScrollSelect) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm
  controller_->OnEvent(ftxui::Event::Escape);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, ScrollResultSlotsRemainingIsDecrementedOnSuccess) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  // sword_ has 3 upgrade slots; one was consumed.
  EXPECT_EQ(controller_->scroll_result().slots_remaining, 2);
}

TEST_F(TuiControllerTest, FailedScrollStoresFailOutcome) {
  UseFailScroll();
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);     // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollFail);
}

// --- Scroll via bag panel ---

TEST_F(TuiControllerTest, BagScrollGoesToScrollSelect) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, BagScrollEscapeFromScrollSelectGoesToItemMenu) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest, BagScrollAppliesScrollToInventory) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->screen(), kScrollResult);
  const EquipInstance* item = state_->character.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->equip_state().scroll_stats().attack(), 5);
}

TEST_F(TuiControllerTest, BagScrollResultStoresOutcome) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest,
       BagScrollNoSlotsGoesToScrollResultWithNoSlotsOutcome) {
  sword_.set_upgrade_slots(0);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // bypass confirm (0 slots)

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

// --- Star Force via equip panel ---

TEST_F(TuiControllerTest, StarForceActionGoesToStarForce) {
  LevelTo(60);  // star force is gated there
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
  LevelTo(60);  // star force is gated there
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->screen(), kStarForceResult);
}

TEST_F(TuiControllerTest, StarForceResultStoresEquipNameAndStarsBefore) {
  LevelTo(60);  // star force is gated there
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->star_force_result().equip_name, "Sword");
  EXPECT_EQ(controller_->star_force_result().stars_before, 0);
}

TEST_F(TuiControllerTest, EnterInStarForceResultSuccessGoesToStarForce) {
  LevelTo(60);  // star force is gated there
  // At 0★ the success rate is 95%, so with a seeded rng the first attempt
  // will succeed. We just verify the screen transition, not the outcome.
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  // If the item was not destroyed, dismissing the result goes back to
  // kStarForce.
  if (controller_->star_force_result().outcome != kStarForceDestroy) {
    controller_->OnEvent(ftxui::Event::Return);
    EXPECT_EQ(controller_->screen(), kStarForce);
  }
}

// --- Star Force via bag panel ---

TEST_F(TuiControllerTest, BagStarForceGoesToStarForce) {
  LevelTo(60);  // star force is gated there
  PickUpScrolledSword();
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kStarForce);
}

TEST_F(TuiControllerTest, BagStarForceAttemptGoesToStarForceResult) {
  LevelTo(60);  // star force is gated there
  PickUpScrolledSword();
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  controller_->OnEvent(ftxui::Event::Return);  // open confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->screen(), kStarForceResult);
  EXPECT_EQ(controller_->star_force_result().equip_name, "Sword");
}

// --- inspect_item accessor ---

TEST_F(TuiControllerTest, InspectItemReturnsNullptrWhenNotInspecting) {
  EXPECT_EQ(controller_->inspect_item(), nullptr);
}

TEST_F(TuiControllerTest, BagInspectGoesToInspect) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kInspect);
  EXPECT_NE(controller_->inspect_item(), nullptr);
}

// The accessors resolve whichever half the player opened the modal from. The
// controller settles that when the item is picked, so these cover both halves
// of that decision reaching the right item back.

TEST_F(TuiControllerTest, EquipInspectResolvesTheWornItem) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  ASSERT_NE(controller_->inspect_item(), nullptr);
  EXPECT_EQ(controller_->inspect_item(),
            &state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

TEST_F(TuiControllerTest, BagInspectResolvesTheBagItem) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  ASSERT_NE(controller_->inspect_item(), nullptr);
  EXPECT_EQ(controller_->inspect_item(), &state_->character.inventory()[0]);
}

TEST_F(TuiControllerTest, EquipScrollResolvesTheWornItem) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->scroll_item(),
            &state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

// Taking focus elsewhere after opening the modal must not change which item it
// is about -- the old code re-read panel_focus_ every time and would have
// followed it.
TEST_F(TuiControllerTest, MovingFocusDoesNotRepointAnOpenModal) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  panel_focus_ = kInventoryPanel;

  EXPECT_EQ(controller_->inspect_item(),
            &state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

// --- Sell via Etc tab ---

// --- shop ---

// The Shop tab has no list to walk down into, so Enter opens it straight from
// the tab bar.
TEST_F(TuiControllerTest, EnterOnTheShopTabOpensTheShop) {
  panel_focus_ = kInventoryPanel;
  for (int i = 0; i < 3; ++i) {
    inventory_component_->OnEvent(ftxui::Event::ArrowRight);
  }
  inventory_component_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kShop);
}

// Enter on the other tabs still opens the item menu, not the shop.
TEST_F(TuiControllerTest, EnterOnTheEtcTabStillOpensTheItemMenu) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  inventory_component_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest, ShopEscapeReturnsToTheBag) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EnterOnAShopItemOpensTheMenu) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kShopMenu);
}

TEST_F(TuiControllerTest, ShopMenuInspectOpensTheInspectScreen) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);  // -> kShopMenu, on Inspect
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kShopInspect);
}

// Inspecting is how a player decides whether to buy, so leaving the inspect
// screen puts them back at the list rather than out in the bag.
TEST_F(TuiControllerTest, LeavingShopInspectReturnsToTheShop) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);  // Inspect
  ASSERT_EQ(controller_->screen(), kShopInspect);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kShop);
}

TEST_F(TuiControllerTest, ShopMenuBuyOpensTheBuyDialog) {
  OpenBuyDialog();
  EXPECT_EQ(controller_->screen(), kShopBuy);
}

TEST_F(TuiControllerTest, ShopMenuCloseReturnsToTheList) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);     // -> kShopMenu
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Buy
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Close
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kShop);
}

TEST_F(TuiControllerTest, EscapeFromTheShopMenuReturnsToTheList) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kShop);
}

// Escape out of the menu leaves the list where it was, so the next Escape is
// the one that leaves the shop.
TEST_F(TuiControllerTest, EscapeFromTheShopMenuDoesNotLeaveTheShop) {
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(controller_->screen(), kShop);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, BuyingTakesTheMesoAndFillsTheBag) {
  state_->character.AddMeso(25000);
  OpenBuyDialog();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);     // Confirm

  EXPECT_EQ(state_->character.meso(), 15000);
  EXPECT_EQ(state_->character.inventory().size(), 1);
  // Back to the shop rather than the bag: buying one thing usually means two.
  EXPECT_EQ(controller_->screen(), kShop);
}

TEST_F(TuiControllerTest, BuyingTheTypedQuantity) {
  state_->character.AddMeso(25000);
  OpenBuyDialog();
  controller_->OnEvent(ftxui::Event::Backspace);       // clear the 1
  controller_->OnEvent(ftxui::Event::Character('2'));  // two of them
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.meso(), 5000);
  EXPECT_EQ(state_->character.inventory().size(), 2);
}

TEST_F(TuiControllerTest, CancellingABuyReturnsToTheShopUnchanged) {
  state_->character.AddMeso(25000);
  OpenBuyDialog();
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kShop);
  EXPECT_EQ(state_->character.meso(), 25000);
  EXPECT_EQ(state_->character.inventory().size(), 0);
}

// The dialog opens on an unaffordable item rather than refusing to, and says
// so by leaving Confirm inert.
TEST_F(TuiControllerTest, ConfirmingWhatCannotBeAffordedBuysNothing) {
  state_->character.AddMeso(500);
  OpenBuyDialog();
  EXPECT_EQ(controller_->screen(), kShopBuy);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kShopBuy) << "Confirm should be inert";
  EXPECT_EQ(state_->character.meso(), 500);
  EXPECT_EQ(state_->character.inventory().size(), 0);
}

TEST_F(TuiControllerTest, SellMenuSellGoesToSellScreen) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  OpenStackSell();
  EXPECT_EQ(controller_->screen(), kSell);
}

TEST_F(TuiControllerTest, SellConfirmSellsWholeStackAndCreditsMeso) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  OpenStackSell();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);     // Confirm (qty = 10)

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(state_->character.meso(), 20);  // 10 * 2
}

TEST_F(TuiControllerTest, SellEscapeCancelsWithoutSelling) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  OpenStackSell();
  controller_->OnEvent(ftxui::Event::Escape);  // cancel

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.stackables(ITEM_CATEGORY_ETC)[0].count(), 10);
  EXPECT_EQ(state_->character.meso(), 0);
}

TEST_F(TuiControllerTest, SellConfirmSellsTypedQuantity) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  OpenStackSell();
  // Digits append, so empty the field before typing the quantity to sell.
  controller_->OnEvent(ftxui::Event::Backspace);       // 10 -> 1
  controller_->OnEvent(ftxui::Event::Backspace);       // 1 -> 0
  controller_->OnEvent(ftxui::Event::Character('3'));  // quantity 3
  controller_->OnEvent(ftxui::Event::ArrowDown);       // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);          // Confirm

  EXPECT_EQ(state_->character.stackables(ITEM_CATEGORY_ETC)[0].count(), 7);
  EXPECT_EQ(state_->character.meso(), 6);  // 3 * 2
}

// --- Equip via bag panel ---

TEST_F(TuiControllerTest, ReturnActionEquipsFromInventoryPanel) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EquippingTheLastItemLeavesFocusWhereItWas) {
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::Return);

  ASSERT_TRUE(state_->character.inventory().empty());
  EXPECT_EQ(panel_focus_, kInventoryPanel);
}

// --- Map select ---

TEST_F(TuiControllerTest, OpenMapSelectStartsOnTheMapBeingFarmed) {
  LoadTwoMaps();
  state_->current_map = "cave";

  controller_->OpenMapSelect();

  EXPECT_EQ(controller_->screen(), kMapSelect);
  EXPECT_EQ(map_select_panel_->selected_map(), "cave");
}

TEST_F(TuiControllerTest, EnterInMapSelectTravelsToTheHighlightedMap) {
  LoadTwoMaps();
  state_->current_map = "cave";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Cave -> Field
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->current_map, "field");
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, LeftAndRightInMapSelectChangeTheLevelBand) {
  LoadTwoMaps();
  AddMapOnTheSecondBand();
  state_->current_map = "field";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowRight);  // 1-10 -> 11-30
  EXPECT_EQ(map_select_panel_->selected_map(), "temple");

  controller_->OnEvent(ftxui::Event::ArrowLeft);  // back down
  EXPECT_EQ(map_select_panel_->selected_map(), "field");
}

TEST_F(TuiControllerTest, EnterTravelsToAMapOnAnotherBand) {
  LoadTwoMaps();
  AddMapOnTheSecondBand();
  state_->current_map = "field";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->current_map, "temple");
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EscapeInMapSelectLeavesTheMapAlone) {
  LoadTwoMaps();
  state_->current_map = "cave";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Cave -> Field
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(state_->current_map, "cave");
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, MapSelectSwallowsKeysThatWouldActOnTheMainScreen) {
  LoadTwoMaps();

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::Tab);  // would cycle panel focus in kMain

  EXPECT_EQ(controller_->screen(), kMapSelect);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// --- panels a character has not unlocked ---

// The two panels down the right are handed over as the player levels. Until
// then they are not drawn and Tab does not stop on them.
TEST_F(TuiControllerTest, TheRightHandPanelsArriveWithTheirLevels) {
  // The fixture levels past both buying SP, so this walks back down by
  // building a character at each level rather than levelling up to it.
  GameState fresh({}, {}, {}, {}, {});
  ASSERT_EQ(fresh.character.proto().level(), 1);

  EquippedPanel equip(fresh.character, panel_focus_);
  InventoryPanel bag(fresh.character, panel_focus_);
  ScrollPanel scroll({});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  MapSelectPanel maps(fresh);
  ShopPanel shop(fresh.character, fresh.equips);
  BuyPanel buy;
  int focus = kCharPanel;
  TuiController controller(fresh, equip, bag, scroll, star, trace, sell, maps,
                           shop, buy, focus);

  EXPECT_TRUE(controller.PanelVisible(kCharPanel));
  EXPECT_TRUE(controller.PanelVisible(kCombatPanel));
  EXPECT_FALSE(controller.PanelVisible(kEquipPanel));
  EXPECT_FALSE(controller.PanelVisible(kInventoryPanel));

  while (fresh.character.proto().level() < UnlockLevel(Feature::kEquipped)) {
    fresh.character.LevelUp();
  }
  EXPECT_TRUE(controller.PanelVisible(kEquipPanel));
  EXPECT_FALSE(controller.PanelVisible(kInventoryPanel))
      << "the bag comes a level later";

  fresh.character.LevelUp();
  EXPECT_TRUE(controller.PanelVisible(kInventoryPanel));
}

// Tab rounds the panels that exist. At level 1 that is two of them, so it
// cannot leave the player pressing Tab at a panel that is not on screen.
TEST_F(TuiControllerTest, TabSkipsThePanelsThatAreNotThereYet) {
  GameState fresh({}, {}, {}, {}, {});
  EquippedPanel equip(fresh.character, panel_focus_);
  InventoryPanel bag(fresh.character, panel_focus_);
  ScrollPanel scroll({});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  MapSelectPanel maps(fresh);
  ShopPanel shop(fresh.character, fresh.equips);
  BuyPanel buy;
  int focus = kCharPanel;
  TuiController controller(fresh, equip, bag, scroll, star, trace, sell, maps,
                           shop, buy, focus);

  controller.OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(focus, kCombatPanel) << "past both locked panels";
  controller.OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(focus, kCharPanel) << "and back round";
}

// The game opens focused on the equipped panel, which a level 1 character
// does not have. Focus has to leave before a key is dispatched, or it lands
// on a panel the player cannot see.
TEST_F(TuiControllerTest, FocusLeavesAPanelThatIsNotOnScreen) {
  GameState fresh({}, {}, {}, {}, {});
  EquippedPanel equip(fresh.character, panel_focus_);
  InventoryPanel bag(fresh.character, panel_focus_);
  ScrollPanel scroll({});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  MapSelectPanel maps(fresh);
  ShopPanel shop(fresh.character, fresh.equips);
  BuyPanel buy;
  int focus = kEquipPanel;  // where the game starts
  TuiController controller(fresh, equip, bag, scroll, star, trace, sell, maps,
                           shop, buy, focus);

  controller.OnEvent(ftxui::Event::Custom);  // any key at all
  EXPECT_TRUE(controller.PanelVisible(focus));
}

// Inspect leads the stack menu and opens the same screen the equip lists use,
// showing what the item is rather than what it is worth.
TEST_F(TuiControllerTest, StackInspectShowsTheItemsDescription) {
  ItemPrototype shell;
  shell.set_name("Green Snail Shell");
  shell.set_category(ITEM_CATEGORY_ETC);
  shell.set_description("A shell shed by a snail.");
  state_->character.AddStackable(shell, 3);
  panel_focus_ = kInventoryPanel;
  inventory_component_->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  inventory_component_->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);   // into the list
  inventory_component_->OnEvent(ftxui::Event::Return);      // the stack menu
  controller_->OnEvent(ftxui::Event::Return);               // Inspect

  EXPECT_EQ(controller_->screen(), kItemInspect);
  ASSERT_NE(controller_->item_inspect_item(), nullptr);
  EXPECT_EQ(controller_->item_inspect_item()->description(),
            "A shell shed by a snail.");

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(controller_->item_inspect_item(), nullptr);
}

}  // namespace
}  // namespace ms
