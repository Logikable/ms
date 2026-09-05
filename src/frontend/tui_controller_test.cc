#include "src/frontend/tui_controller.h"

#include <gtest/gtest.h>

#include <ctime>
#include <map>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/consumables.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/combat/boss_run.h"
#include "src/combat/offline.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/cube_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/party_inspect_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/pot_info_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_equip_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
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
    MakeState();
    SeedCharacter();
    MakePanels();
  }

  // The catalogs these tests run on: small enough to state here rather than
  // load out of data/, and every entry is here because some test below needs
  // it to exist before a panel is built.
  void MakeState() {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(3);
    sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);

    Scroll scroll;
    scroll.set_name("Test Scroll");
    scroll.set_success_rate(100);
    scroll.set_tier(SCROLL_TIER_1);
    scroll.set_trace_cost(5);
    scroll.mutable_stats()->set_attack(5);
    scroll.set_target(SCROLL_TARGET_WEAPON);
    scroll.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);

    std::map<std::string, EquipPrototype> equips;
    equips["Sword"] = sword_;
    // A stand-in under the catalog key a Magician's advancement asks for, so
    // the advancement tests see gear arrive without loading data/equip.
    EquipPrototype staff;
    staff.set_name("Wooden Staff");
    staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    equips["wooden_staff"] = staff;
    // Something for the shop to stock. ShopPanel fixes its list at
    // construction, so this has to exist before the panel does.
    EquipPrototype machete;
    machete.set_name("Machete");
    machete.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    machete.set_required_level(20);
    machete.set_shop_price(10000);
    equips["machete"] = machete;
    // The token shelf's own stock, and the token that buys it.
    EquipPrototype frozen;
    frozen.set_name("Frozen Sword");
    frozen.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    frozen.set_required_level(120);
    frozen.set_token_item("weapon_token");
    frozen.set_token_price(1);
    equips["frozen_sword"] = frozen;
    token_.set_name("Weapon Token");
    token_.set_category(ITEM_CATEGORY_ETC);
    token_.set_currency_mark("●");
    token_.set_currency_color(CURRENCY_COLOR_THEME);
    // Something plain for a boss to drop, so the clear card has a row that is
    // not a currency.
    shard_.set_name("Zakum's Soul Shard");
    shard_.set_category(ITEM_CATEGORY_ETC);
    shard_.set_max_stack(100);
    std::map<std::string, ItemPrototype> items;
    items["weapon_token"] = token_;
    items["zakums_soul_shard"] = shard_;
    std::map<std::string, Scroll> scrolls;
    scrolls["Test Scroll"] = scroll;

    state_ = std::make_unique<GameState>(
        std::move(equips), std::move(scrolls), std::move(items),
        std::map<std::string, Mob>{}, std::map<std::string, MapData>{});
    // Normal Zakum, so the boss screen has a fight on it. Seeded here rather
    // than per test: BossSelectPanel fixes its list at construction, and the
    // panels are built once for the fixture.
    Mob arm;
    arm.set_name("Zakum's Arm");
    arm.set_level(110);
    // A single point of HP: these tests are about the screen the fight is on,
    // not about how long one takes.
    arm.set_max_hp(1);
    arm.set_boss(true);
    state_->mobs["zakum_arm"] = arm;
    Boss boss;
    boss.set_name("Zakum");
    BossDifficulty* normal = boss.add_difficulties();
    normal->set_name("Normal");
    normal->set_reset(RESET_PERIOD_DAILY);
    normal->set_time_limit_seconds(300);
    Spawn* spawn = normal->add_phases()->add_spawns();
    spawn->set_mob("zakum_arm");
    spawn->set_count(2);
    state_->bosses["zakum"] = boss;
  }

  // A character holding nothing is refused the fight, so every boss test that
  // means to get past the list arms itself first.
  void HoldASword() {
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
    state_->character.Equip(state_->character.inventory().size() - 1);
  }

  // A sword on the character and the equip panel drawn over it, which is what
  // fills the panel's row list. The starting point for every test that opens
  // the equip menu.
  void WearASwordAndDraw() {
    HoldASword();
    RenderEquipPanel();
  }

  // Puts the bag's cursor on the panel and down on its first row, which is
  // where every test that opens an item menu means to be standing: focus
  // arrives on the tab bar, where Enter opens the tab's own menu instead.
  void DescendIntoBag() {
    panel_focus_ = kInventoryPanel;
    RenderInventoryPanel();  // the row has to exist before the cursor reaches
                             // it
    inventory_component_->OnEvent(ftxui::Event::ArrowDown);
  }

  // A sword in the bag instead, with the cursor down on its row.
  void BagASword() {
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
    DescendIntoBag();
  }

  // Runs the fight in progress until it is over, however it ends. The run
  // outlives the fight -- it is held behind whatever panel ended it -- so this
  // waits on the screen rather than on in_boss_fight().
  void RunFightToEnd() {
    for (int i = 0; i < 20000 && controller_->screen() == kBossFight; ++i) {
      controller_->AdvanceBossRun(0.1);
    }
  }

  // Takes the one fight in the catalog, so a test starts inside it.
  void EnterFight() {
    HoldASword();
    controller_->OpenMenuEntry(MenuEntry::kBoss);
    controller_->OnEvent(ftxui::Event::Return);
    controller_->OnEvent(ftxui::Event::Return);
  }

  // A Swordman with enough SP to spend and standing at scrolling's gate.
  void SeedCharacter() {
    state_->character.AdvanceJob(JOB_SWORDMAN);
    // The starting character stands at its advancement with no SP yet; these
    // tests spend SP, so they level far enough to have some of their own.
    while (state_->character.sp(1) < 60) {
      state_->character.LevelUp();
    }
    // Many tests below reach the Scroll entry by counting rows down the item
    // menu, and a gated entry is not drawn at all -- so the character has to
    // stand at scrolling's gate or the count lands somewhere else. The SP loop
    // above happens to end there today (stage-1 SP starts at 11 and pays 3 a
    // level, so 60 of it is exactly level 30). Stated rather than relied on:
    // either number can move without the other.
    LevelTo(UnlockLevel(Feature::kScrolling));
  }

  // Every panel the controller drives, and the controller over them.
  void MakePanels() {
    char_panel_ = std::make_unique<CharacterPanel>(
        state_->character, state_->account, panel_focus_, state_->skills);
    equip_panel_ = std::make_unique<EquippedPanel>(
        state_->character, state_->account, panel_focus_);
    inventory_panel_ = std::make_unique<InventoryPanel>(
        state_->character, state_->account, panel_focus_);
    scroll_panel_ =
        std::make_unique<ScrollPanel>(state_->character, state_->scrolls);
    star_force_panel_ = std::make_unique<StarForcePanel>();
    trace_recover_panel_ =
        std::make_unique<TraceRecoverPanel>(state_->character);
    sell_panel_ = std::make_unique<SellPanel>();
    sell_equip_panel_ = std::make_unique<SellEquipPanel>();
    multi_sell_panel_ =
        std::make_unique<MultiSellPanel>(state_->character, state_->account);
    map_select_panel_ = std::make_unique<MapSelectPanel>(*state_);
    mob_inspect_panel_ = std::make_unique<MobInspectPanel>(*state_);
    boss_select_panel_ = std::make_unique<BossSelectPanel>(*state_);
    party_inspect_panel_ = std::make_unique<PartyInspectPanel>(*state_);
    shop_panel_ = std::make_unique<ShopPanel>(state_->character, state_->equips,
                                              state_->items);
    buy_panel_ = std::make_unique<BuyPanel>();
    job_inspect_panel_ = std::make_unique<JobInspectPanel>(state_->skills);
    menu_panel_ = std::make_unique<MenuPanel>(*state_, analysis_, panel_focus_);
    keys_ = std::make_unique<KeyMap>(state_->account.mutable_keybinds());
    keybinds_panel_ = std::make_unique<KeybindsPanel>(*keys_);
    options_panel_ = std::make_unique<OptionsPanel>(state_->account);
    controller_ = std::make_unique<TuiController>(
        *state_,
        Screens{
            *char_panel_,        *equip_panel_,         *inventory_panel_,
            *scroll_panel_,      inspect_panel_,        preview_inspect_panel_,
            *star_force_panel_,  cube_panel_,           *trace_recover_panel_,
            *sell_panel_,        *sell_equip_panel_,    *multi_sell_panel_,
            *map_select_panel_,  *mob_inspect_panel_,   *boss_select_panel_,
            party_select_panel_, *party_inspect_panel_, *shop_panel_,
            *buy_panel_,         *job_inspect_panel_,   skill_inspect_panel_,
            pot_info_panel_,     *menu_panel_,          *keybinds_panel_,
            *options_panel_},
        analysis_, *keys_, panel_focus_);

    // Build the equip component so RenderEquipPanel() can populate slots_.
    equip_component_ = equip_panel_->MakeComponent([]() {});
    // The inventory component drives tab switching and opens the context menu.
    inventory_component_ = inventory_panel_->MakeComponent(
        [this]() { controller_->OpenInventoryMenu(); },
        [this]() { controller_->ToggleExpanded(kInventoryPanel); });
  }

  // Opens the Keybinds screen the way a player does, from the Settings box.
  // Up walks the Settings box from the bottom, so Keybinds is the second stop
  // and Options the first.
  void OpenKeybinds() {
    controller_->OpenMenuEntry(MenuEntry::kSettings);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::Return);
  }

  void OpenOptions() {
    controller_->OpenMenuEntry(MenuEntry::kSettings);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::Return);
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
    LevelTo(UnlockLevel(Feature::kShop));
    panel_focus_ = kInventoryPanel;
    for (int i = 0; i < 3; ++i) {
      inventory_component_->OnEvent(ftxui::Event::ArrowRight);
    }
    inventory_component_->OnEvent(ftxui::Event::Return);
  }

  // Opens the shop on the Buy-Back shelf with the cursor on its first row.
  // Up twice puts the cursor on the tab bar, past the pay row under it; Down
  // brings it back into the list, which the Buy-Back shelf has no pay row to
  // stop at on the way.
  void OpenBuyBackShelf() {
    OpenShop();
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    for (int i = 0; i < kShopBuyBackTab; ++i) {
      controller_->OnEvent(ftxui::Event::ArrowRight);
    }
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }

  // As OpenBuyDialog, from the shelf.
  void OpenBuyBackDialog() {
    OpenBuyBackShelf();
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopMenu, on Inspect
    controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Buy
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopBuy
  }

  // Opens the shop, then the menu on the first item, then its Buy entry,
  // leaving the buy dialog up with the quantity field focused.
  void OpenBuyDialog() {
    OpenShop();
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopMenu, on Inspect
    controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Buy
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopBuy
  }

  // As above, on the Token half of the weapon shelf.
  void OpenTokenBuyDialog() {
    OpenShop();
    controller_->OnEvent(ftxui::Event::ArrowUp);     // list -> the pay row
    controller_->OnEvent(ftxui::Event::ArrowRight);  // Meso -> Token
    controller_->OnEvent(ftxui::Event::ArrowDown);   // -> the first item
    controller_->OnEvent(ftxui::Event::Return);      // -> kShopMenu
    controller_->OnEvent(ftxui::Event::ArrowDown);   // -> Buy
    controller_->OnEvent(ftxui::Event::Return);      // -> kShopBuy
  }

  // The pot menu's entries, read the same way.
  std::string RenderPotMenu() {
    ftxui::Element menu = controller_->pot_menu().Render(0, 0);
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(menu));
    ftxui::Render(screen, menu);
    return ScreenText(screen);
  }

  // The buy dialog's text, read off the screen cell by cell rather than from
  // Screen::ToString, which threads colour escapes through the rows.
  std::string RenderBuyDialog() {
    ftxui::Element dialog = buy_panel_->Render();
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(dialog));
    ftxui::Render(screen, dialog);
    return ScreenText(screen);
  }

  // MapSelectPanel fixes its display order at construction, so the maps must
  // exist before it does -- rebuild both after touching state_->maps.
  void RebuildMapSelect() {
    map_select_panel_ = std::make_unique<MapSelectPanel>(*state_);
    mob_inspect_panel_ = std::make_unique<MobInspectPanel>(*state_);
    boss_select_panel_ = std::make_unique<BossSelectPanel>(*state_);
    party_inspect_panel_ = std::make_unique<PartyInspectPanel>(*state_);
    controller_ = std::make_unique<TuiController>(
        *state_,
        Screens{
            *char_panel_,        *equip_panel_,         *inventory_panel_,
            *scroll_panel_,      inspect_panel_,        preview_inspect_panel_,
            *star_force_panel_,  cube_panel_,           *trace_recover_panel_,
            *sell_panel_,        *sell_equip_panel_,    *multi_sell_panel_,
            *map_select_panel_,  *mob_inspect_panel_,   *boss_select_panel_,
            party_select_panel_, *party_inspect_panel_, *shop_panel_,
            *buy_panel_,         *job_inspect_panel_,   skill_inspect_panel_,
            pot_info_panel_,     *menu_panel_,          *keybinds_panel_,
            *options_panel_},
        analysis_, *keys_, panel_focus_);
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
    Spawn* golems = temple.add_spawns();
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
    Spawn* snails = field.add_spawns();
    snails->set_mob("snail");
    snails->set_count(1);
    MapData cave;
    cave.set_name("Cave");
    Spawn* mushrooms = cave.add_spawns();
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

  // Renders inventory_component_ to build the Equip tab's row list, which is
  // filled during the render rather than up front. The list has to exist
  // before the cursor can be walked down it.
  void RenderInventoryPanel() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                              ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, inventory_component_->Render());
  }

  // The skill card as text, for asserting which rows of it are on screen.
  std::string RenderSkillCard() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                              ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, skill_inspect_panel_.Render());
    return scr.ToString();
  }

  // The colour the open cube question is drawn in, which is its border's.
  // Gold on a reroll that ranked the potential up, steel blue otherwise.
  ftxui::Color CubeQuestionColor() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                              ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, cube_panel_.RenderConfirm());
    return scr.PixelAt(0, 0).foreground_color;
  }

  // The equip sell dialog as text, for asserting what it tells the player.
  std::string RenderSellDialog() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(44),
                                              ftxui::Dimension::Fixed(12));
    ftxui::Render(scr, sell_equip_panel_->Render());
    return scr.ToString();
  }

  // Picks up sword_ with all upgrade slots consumed (required for star force).
  // Walks the open gear menu down to `entry`. Counting keypresses would only
  // count the entries this item happens to be offered.
  void WalkGearMenuTo(int entry) {
    for (int i = 0; i < 10 && equip_panel_->menu().selected() != entry; ++i) {
      controller_->OnEvent(ftxui::Event::ArrowDown);
    }
    ASSERT_EQ(equip_panel_->menu().selected(), entry);
  }

  void PickUpScrolledSword() {
    sword_.set_upgrade_slots(0);
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  }

  // Replaces the scroll map with a single 0%-rate scroll and rebuilds
  // scroll_panel_ and controller_ to pick up the change.
  void UseFailScroll() {
    Scroll fail;
    fail.set_name("Fail Scroll");
    // GMS sells no 0% scroll, so no band prices it and the figure written here
    // is what the player pays. That is what makes a guaranteed failure
    // affordable to test: the real rates all sometimes land.
    fail.set_success_rate(0);
    fail.set_tier(SCROLL_TIER_1);
    fail.set_trace_cost(5);
    fail.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    fail.set_target(SCROLL_TARGET_WEAPON);
    fail.mutable_stats()->set_attack(5);
    state_->scrolls.clear();
    state_->scrolls["Fail Scroll"] = fail;
    scroll_panel_ =
        std::make_unique<ScrollPanel>(state_->character, state_->scrolls);
    controller_ = std::make_unique<TuiController>(
        *state_,
        Screens{
            *char_panel_,        *equip_panel_,         *inventory_panel_,
            *scroll_panel_,      inspect_panel_,        preview_inspect_panel_,
            *star_force_panel_,  cube_panel_,           *trace_recover_panel_,
            *sell_panel_,        *sell_equip_panel_,    *multi_sell_panel_,
            *map_select_panel_,  *mob_inspect_panel_,   *boss_select_panel_,
            party_select_panel_, *party_inspect_panel_, *shop_panel_,
            *buy_panel_,         *job_inspect_panel_,   skill_inspect_panel_,
            pot_info_panel_,     *menu_panel_,          *keybinds_panel_,
            *options_panel_},
        analysis_, *keys_, panel_focus_);
  }

  int panel_focus_ = kEquipPanel;
  // Scrolling is paid for in spell traces, so a test that scrolls stocks some
  // first. Not done in SetUp: it would put a stack at the head of the Etc tab
  // and shift every index the sell tests count on.
  void GiveTraces(int count) {
    ItemPrototype trace;
    trace.set_name(kSpellTraceName);
    trace.set_category(ITEM_CATEGORY_ETC);
    trace.set_max_stack(30000);
    state_->character.AddStackable(trace, count);
  }

  EquipPrototype sword_;
  ItemPrototype token_;
  ItemPrototype shard_;
  std::unique_ptr<GameState> state_;
  std::unique_ptr<CharacterPanel> char_panel_;
  std::unique_ptr<EquippedPanel> equip_panel_;
  std::unique_ptr<InventoryPanel> inventory_panel_;
  std::unique_ptr<ScrollPanel> scroll_panel_;
  std::unique_ptr<StarForcePanel> star_force_panel_;
  CubePanel cube_panel_;
  std::unique_ptr<TraceRecoverPanel> trace_recover_panel_;
  std::unique_ptr<SellPanel> sell_panel_;
  std::unique_ptr<SellEquipPanel> sell_equip_panel_;
  std::unique_ptr<MultiSellPanel> multi_sell_panel_;
  std::unique_ptr<MapSelectPanel> map_select_panel_;
  std::unique_ptr<MobInspectPanel> mob_inspect_panel_;
  std::unique_ptr<BossSelectPanel> boss_select_panel_;
  // Not a pointer: it takes nothing to build, and there is no connection in
  // these tests for it to draw.
  PartySelectPanel party_select_panel_;
  std::unique_ptr<PartyInspectPanel> party_inspect_panel_;
  std::unique_ptr<ShopPanel> shop_panel_;
  std::unique_ptr<BuyPanel> buy_panel_;
  std::unique_ptr<JobInspectPanel> job_inspect_panel_;
  SkillInspectPanel skill_inspect_panel_;
  PotInfoPanel pot_info_panel_;
  InspectPanel inspect_panel_;
  InspectPanel preview_inspect_panel_;
  BattleAnalysis analysis_;
  std::unique_ptr<MenuPanel> menu_panel_;
  std::unique_ptr<KeyMap> keys_;
  std::unique_ptr<KeybindsPanel> keybinds_panel_;
  std::unique_ptr<OptionsPanel> options_panel_;
  std::unique_ptr<TuiController> controller_;
  ftxui::Component equip_component_;
  ftxui::Component inventory_component_;
};

// --- Tab ---

// Focus starts on the equipped panel and Tab runs clockwise through every
// panel: equipped -> inventory -> menu -> combat -> character -> back to
// equipped. The character panel is always focusable, so no panel is ever
// skipped.

TEST_F(TuiControllerTest, TabWalksThePanelRing) {
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kInventoryPanel);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kMenuPanel);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kCombatPanel);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kCharPanel);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// The same ring anticlockwise, rounding the same way.
TEST_F(TuiControllerTest, ShiftTabWalksTheRingBackwards) {
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(panel_focus_, kCharPanel);
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(panel_focus_, kCombatPanel);
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(panel_focus_, kMenuPanel);
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(panel_focus_, kInventoryPanel);
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// A gold tab says "there is something new in here". Tabbing to the panel it is
// already open on is reading it, so the gold has to come off -- otherwise it
// could only be cleared by arrowing away from the tab and back onto it.
TEST_F(TuiControllerTest, ArrivingOnAPanelReadsTheTabLeftOpenOnIt) {
  int stage = state_->character.proto().job_stage();
  ASSERT_FALSE(state_->account.Seen(EquipGiftTabKey(stage)));

  controller_->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(panel_focus_, kInventoryPanel);
  EXPECT_TRUE(state_->account.Seen(EquipGiftTabKey(stage)));
}

// Which is what it is for: undoing a Tab that went one panel too far.
TEST_F(TuiControllerTest, ShiftTabUndoesATab) {
  controller_->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(panel_focus_, kInventoryPanel);
  controller_->OnEvent(ftxui::Event::TabReverse);
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

// --- Skill menu ---

// The Bishop's toggle, on the Swordman's book so the fixture's character can
// hold it.
Skill RighteouslyIndignant() {
  Skill skill;
  skill.set_name("Righteously Indignant");
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.set_toggle(true);
  return skill;
}

TEST_F(TuiControllerTest, EnterOnASkillOpensItsMenuOnInspect) {
  controller_->OpenSkillMenu(SlashBlast());
  EXPECT_EQ(controller_->screen(), kSkillMenu);
  EXPECT_EQ(controller_->skill_menu_skill().name(), "Slash Blast");
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kSkillInspect);
  EXPECT_EQ(controller_->skill_inspect_skill().name(), "Slash Blast");
}

// Nothing to switch on an ordinary skill, so Down off Inspect lands on Close
// rather than on a row that does nothing.
TEST_F(TuiControllerTest, AnOrdinarySkillIsOfferedNoSwitch) {
  controller_->OpenSkillMenu(SlashBlast());
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->skill_menu().selected(), kSkillMenuClose);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, TheSwitchThrowsAndTheMenuCloses) {
  Skill toggle = RighteouslyIndignant();
  ASSERT_TRUE(state_->character.LearnSkill(toggle, 1));
  controller_->OpenSkillMenu(toggle);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(controller_->skill_menu().selected(), kSkillMenuToggle);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.SkillToggledOn("Righteously Indignant"));

  // And the verb reads the other way round the second time.
  controller_->OpenSkillMenu(toggle);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(state_->character.SkillToggledOn("Righteously Indignant"));
}

// A toggle nobody has bought still lists its switch -- greyed, because its
// absence would be the surprise -- and the cursor steps past it.
TEST_F(TuiControllerTest, AnUnboughtToggleCannotBeSwitchedOn) {
  controller_->OpenSkillMenu(RighteouslyIndignant());
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->skill_menu().selected(), kSkillMenuClose);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(state_->character.SkillToggledOn("Righteously Indignant"));
}

TEST_F(TuiControllerTest, EscapeLeavesTheSkillMenu) {
  controller_->OpenSkillMenu(SlashBlast());
  controller_->OnEvent(ftxui::Event::Escape);
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

TEST_F(TuiControllerTest, ReportsZeroForAnUnlearnedSkill) {
  controller_->OpenSkillInspect(SlashBlast());
  EXPECT_EQ(controller_->skill_inspect_level(), 0);
}

// The level is read live, not captured when the screen opened, so a point
// spent between two looks shows up on the second.
TEST_F(TuiControllerTest, SkillInspectFollowsAPointSpent) {
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

// The arrows scroll the card rather than leaving the screen, and every card
// opens at its head however far down the last one was read.
TEST_F(TuiControllerTest, AFreshSkillCardStartsAtTheTop) {
  Skill skill = SlashBlast();
  ASSERT_TRUE(state_->character.LearnSkill(skill, 3));
  controller_->OpenSkillInspect(skill);
  skill_inspect_panel_.SetSkill(&skill, 3, 0);
  // Small enough that there is somewhere to scroll to.
  skill_inspect_panel_.SetMaxRows(6);

  std::string head = RenderSkillCard();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->screen(), kSkillInspect) << "scrolled, not closed";
  EXPECT_NE(RenderSkillCard(), head);

  controller_->OpenSkillInspect(skill);
  EXPECT_EQ(RenderSkillCard(), head);
}

// Reading is all there is to do, so Enter leaves too rather than sitting there
// doing nothing.
TEST_F(TuiControllerTest, EnterAlsoLeavesTheSkillInspectScreen) {
  controller_->OpenSkillInspect(SlashBlast());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// --- Job advancement ---

// --- the Pots tab's menu, card and question ---

// The switch is the menu's first entry, named for the state it would leave
// the pot in, and pressing it asks nothing: nothing is spent until it procs.
TEST_F(TuiControllerTest, TheFirstEntryThrowsTheSwitchAndSaysWhichWay) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_NE(RenderPotMenu().find("Enable"), std::string::npos);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.ConsumableActive(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));

  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_NE(RenderPotMenu().find("Disable"), std::string::npos);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(state_->character.ConsumableActive(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
}

TEST_F(TuiControllerTest, ThePotMenuOpensOnInspectAndReadsTheCard) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_EQ(controller_->screen(), kPotMenu);
  EXPECT_EQ(controller_->pot_type(), CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Enable -> Inspect
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kPotInfo);
  // Read-only, and Back returns to the menu it was opened from.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kPotInfo);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kPotMenu);
}

TEST_F(TuiControllerTest, BuyPermBuysThePotOutright) {
  LevelTo(kConsumableUnlockLevel);
  state_->character.AddMeso(200'000'000);
  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Enable -> Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Buy Perm
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kPotBuy);
  EXPECT_EQ(controller_->pot_buy_price(), 100'000'000);
  EXPECT_TRUE(controller_->pot_buy_affordable());

  // Opens on Cancel, so buying is Left then Enter.
  const int64_t before = state_->character.proto().meso();
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.ConsumableOwned(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(state_->character.proto().meso(), before - 100'000'000);
}

// The question still opens on a purse that cannot pay -- the player gets to
// read what it would have cost -- but [Confirm] answers nothing.
TEST_F(TuiControllerTest, AShortPurseOpensTheQuestionAndRefusesTheAnswer) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenPotBuy(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  ASSERT_EQ(controller_->screen(), kPotBuy);
  EXPECT_FALSE(controller_->pot_buy_affordable());
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kPotBuy);
  EXPECT_FALSE(state_->character.ConsumableOwned(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// A pot already bought keeps its entry, greyed, and the cursor steps over it.
TEST_F(TuiControllerTest, AnOwnedPotHasNothingLeftToBuy) {
  LevelTo(kConsumableUnlockLevel);
  state_->character.AddMeso(200'000'000);
  ASSERT_TRUE(state_->character.BuyConsumable(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Enable -> Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Close, stepping
  controller_->OnEvent(ftxui::Event::Return);     // over the greyed Buy Perm
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, CloseAndEscapeBothLeaveThePotMenu) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Enable -> Close, wrapping
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);

  controller_->OpenPotMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Enter on a job asks what to do with it rather than going straight to the
// confirmation: what a job is should be readable before it is chosen.
TEST_F(TuiControllerTest, OpenJobMenuFloatsTheMenuAndNotTheConfirmation) {
  controller_->OpenJobMenu(JOB_ROGUE);
  EXPECT_EQ(controller_->screen(), kJobMenu);
  EXPECT_EQ(controller_->job_advance_job(), JOB_ROGUE);
}

TEST_F(TuiControllerTest, TheJobMenuOpensOnInspect) {
  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kJobInspect);
}

TEST_F(TuiControllerTest, TheJobMenusSecondEntryTakesTheAdvancement) {
  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Advance
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kJobAdvance);
  EXPECT_EQ(controller_->job_advance_job(), JOB_ROGUE);
  // Still the confirmation it always was, still opening on Cancel.
  EXPECT_EQ(state_->character.proto().job(), JOB_SWORDMAN);
}

TEST_F(TuiControllerTest, CloseAndEscapeBothLeaveTheJobMenu) {
  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Advance -> Close
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);

  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.proto().job(), JOB_SWORDMAN);
}

// The menu is reopened at the top each time, so the entry the cursor lands on
// is never the one the last visit left it on.
TEST_F(TuiControllerTest, TheJobMenuOpensAtInspectEveryTime) {
  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OpenJobMenu(JOB_ARCHER);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kJobInspect);
}

// Read-only: the arrows walk the book and Back returns to the menu the screen
// was opened from, where the advancement is one keypress away.
TEST_F(TuiControllerTest, TheJobInspectScreenReadsAndGoesBackToTheMenu) {
  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kJobInspect);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // takes nothing
  EXPECT_EQ(controller_->screen(), kJobInspect);
  EXPECT_EQ(state_->character.proto().job(), JOB_SWORDMAN);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kJobMenu);
}

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
  int int_before = state_->character.proto().allocated_stats().int_();
  controller_->OpenJobAdvance(JOB_MAGICIAN);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // [Cancel] -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().job(), JOB_MAGICIAN);
  EXPECT_GT(state_->character.inventory().size(), bag_before);
  // The fixture already took a first advancement, so this is a second one: it
  // hands the gear over and leaves the stats alone. A reset would seat the new
  // primary at 25.
  EXPECT_EQ(state_->character.proto().allocated_stats().int_(), int_before);
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

// The learn screen counts out the pool the skill is actually bought from. It
// read the job stage's, so a Hyper Skill opened it with nothing to spend and
// the point could never be handed over.
TEST_F(TuiControllerTest, ConfirmSpendsTheHyperPoolOnAHyperSkill) {
  while (state_->character.proto().level() < 140) {
    state_->character.LevelUp();
  }
  ASSERT_EQ(state_->character.hyper_sp(), 1);
  Skill skill = SlashBlast();
  skill.set_name("Dark Thirst");
  skill.set_hyper(true);
  skill.set_required_level(140);
  skill.set_max_level(1);

  controller_->OpenSkillLearn(skill);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.skill_level(skill), 1);
  EXPECT_EQ(state_->character.hyper_sp(), 0);
}

TEST_F(TuiControllerTest, EscapeInSkillLearnLearnsNothing) {
  Skill skill = SlashBlast();
  controller_->OpenSkillLearn(skill);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(state_->character.skill_level(skill), 0);
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, EscapeInApAllocAllocatesNothing) {
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

TEST_F(TuiControllerTest, EscapeInInspectGoesToMain) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
}

// The arrows read the card rather than closing it. Only Confirm and Cancel
// leave, which is what the screen has always promised.
TEST_F(TuiControllerTest, ArrowsInInspectScrollRatherThanLeave) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::Tab);

  EXPECT_EQ(controller_->screen(), kInspect);
}

// A screen opens with the left half holding the arrows, whatever the last one
// was left reading.
TEST_F(TuiControllerTest, AnInspectScreenOpensOnItsLeftHalf) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);
  inspect_panel_.SetItem(controller_->scroll_item());
  inspect_panel_.SetMaxRows(4);
  inspect_panel_.RenderItemOnly();
  controller_->OnEvent(ftxui::Event::Tab);
  ASSERT_TRUE(controller_->right_card_focused());

  controller_->OnEvent(ftxui::Event::Escape);     // back to the item menu
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  EXPECT_FALSE(controller_->right_card_focused());
}

// The scroll list keeps the arrows until Tab hands them over, and hands them
// back the same way.
TEST_F(TuiControllerTest, TabOnTheScrollScreenMovesToTheCard) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kScrollSelect);
  ASSERT_FALSE(controller_->right_card_focused());

  // Cut short enough that the card has something to scroll, and drawn once so
  // it knows. Tui does this each frame; nothing renders in a controller test.
  inspect_panel_.SetItem(controller_->scroll_item());
  inspect_panel_.SetMaxRows(4);
  inspect_panel_.RenderItemOnly();
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(controller_->right_card_focused());
  EXPECT_EQ(controller_->screen(), kScrollSelect);

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_FALSE(controller_->right_card_focused());
}

// A card with room to spare is still a stop, and Shift+Tab walks the same
// ring of two the other way.
TEST_F(TuiControllerTest, ShiftTabOnTheScrollScreenCyclesThroughACardThatFits) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);
  inspect_panel_.SetItem(controller_->scroll_item());
  inspect_panel_.SetMaxRows(40);
  inspect_panel_.RenderItemOnly();

  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_TRUE(controller_->right_card_focused());
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_FALSE(controller_->right_card_focused());
}

// --- Unequip ---

// Both panels used to shove focus at the other one when their own list ran
// out, because an empty list left the panel unable to receive a key. Neither
// does now, and the player keeps the cursor they were holding.
TEST_F(TuiControllerTest, ReturnActionUnequipsAndKeepsFocus) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(state_->character.equipped().empty());
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// --- Scroll via equip panel ---

TEST_F(TuiControllerTest, ScrollFromTheEquipPanelOpensSelect) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollSelectGoesToItemMenu) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest, ScrollSelectAppliesAndShowsTheResult) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
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
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest, NoSlotsShowsTheNoSlotsOutcome) {
  sword_.set_upgrade_slots(0);
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll, which has no slot

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

TEST_F(TuiControllerTest, StarForceOpensOnceTheSlotsAreSpent) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  sword_.set_upgrade_slots(1);
  WearASwordAndDraw();
  GiveTraces(100);

  // Open menu while item still has 1 slot — Star Force should be disabled.
  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);     // pick Scroll -> confirm
  controller_->OnEvent(
      ftxui::Event::Return);  // confirm → kScrollResult (0 slots)
  controller_->OnEvent(ftxui::Event::Escape);  // → kScrollSelect
  controller_->OnEvent(ftxui::Event::Escape);  // → kItemMenu (re-opens menu)

  EXPECT_EQ(controller_->screen(), kItemMenu);
  // Navigate to Star Force position; it must be reachable (not skipped).
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  EXPECT_EQ(equip_panel_->menu().selected(), kGearMenuStarForce);
}

TEST_F(TuiControllerTest, EnterOnAResultReturnsToSelect) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm
  controller_->OnEvent(ftxui::Event::Return);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollResultGoesToScrollSelect) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm
  controller_->OnEvent(ftxui::Event::Escape);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, ASuccessSpendsAnUpgradeSlot) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  // sword_ has 3 upgrade slots; one was consumed.
  EXPECT_EQ(controller_->scroll_result().slots_remaining, 2);
}

// A scroll is bought, not merely chosen: the traces have to leave the bag.
// The price is the sword's, not the scroll's -- a level 60 weapon at 100% is
// 5 traces in GMS's table.
TEST_F(TuiControllerTest, ScrollingSpendsItsTraces) {
  LevelTo(60);
  sword_.set_required_level(60);
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(
      state_->character.CountStackable(ITEM_CATEGORY_ETC, kSpellTraceName), 95);
}

// A failed roll is still a scroll spent -- the trace pays for the attempt,
// not for the result.
// Pin is the second entry of the row's menu, and the pin it sets belongs to
// the character rather than the screen -- so it is still there next time.
TEST_F(TuiControllerTest, PinningFromTheMenuMarksTheCharacter) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open the row's menu
  controller_->OnEvent(ftxui::Event::ArrowDown);  // onto Pin
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(scroll_panel_->SelectedIsPinned());
  EXPECT_TRUE(
      state_->character.ScrollPinned(scroll_panel_->PinKeyOfSelected()));
  EXPECT_EQ(controller_->screen(), kScrollSelect) << "still on the list";

  // And the same entry takes it back off.
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(scroll_panel_->SelectedIsPinned());
}

// Close is the way out of the menu that changes nothing.
TEST_F(TuiControllerTest, CloseLeavesTheMenuWithoutScrolling) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open the row's menu
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Pin
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Close
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(scroll_panel_->IsMenuOpen());
  EXPECT_FALSE(scroll_panel_->IsConfirming());
  EXPECT_FALSE(scroll_panel_->SelectedIsPinned());
  EXPECT_EQ(controller_->screen(), kScrollSelect);
  EXPECT_EQ(
      state_->character.CountStackable(ITEM_CATEGORY_ETC, kSpellTraceName),
      100);
}

// Escape closes the menu rather than the screen behind it. Without this the
// one key would back out of both at once.
TEST_F(TuiControllerTest, EscapeClosesTheRowMenuAndStaysOnTheList) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(scroll_panel_->IsMenuOpen());

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(scroll_panel_->IsMenuOpen());
  EXPECT_EQ(controller_->screen(), kScrollSelect);

  // And a second Escape does leave, now that there is nothing over the list.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest, AFailedScrollStillCosts) {
  UseFailScroll();
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollFail);
  EXPECT_EQ(
      state_->character.CountStackable(ITEM_CATEGORY_ETC, kSpellTraceName), 95);
}

// Too few traces and Enter on the confirm window does nothing: no scroll, no
// spend, and the window stays up rather than dropping the player somewhere.
TEST_F(TuiControllerTest, ScrollingWithoutTheTracesIsRefused) {
  LevelTo(60);
  sword_.set_required_level(60);
  WearASwordAndDraw();
  GiveTraces(4);  // one short of the 5 a level 60 weapon costs at 100%

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
  EXPECT_EQ(
      state_->character.CountStackable(ITEM_CATEGORY_ETC, kSpellTraceName), 4);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .equip_state()
                .scroll_stats()
                .attack(),
            0);
}

TEST_F(TuiControllerTest, FailedScrollStoresFailOutcome) {
  UseFailScroll();
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);     // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);     // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollFail);
}

// --- Scroll via bag panel ---

TEST_F(TuiControllerTest, BagScrollGoesToScrollSelect) {
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, BagScrollEscapeReturnsToTheMenu) {
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kItemMenu);
}

TEST_F(TuiControllerTest, BagScrollAppliesScrollToInventory) {
  BagASword();
  GiveTraces(100);

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->screen(), kScrollResult);
  const EquipInstance* item = state_->character.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->equip_state().scroll_stats().attack(), 5);
}

TEST_F(TuiControllerTest, BagScrollResultStoresOutcome) {
  BagASword();
  GiveTraces(100);

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);  // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
  controller_->OnEvent(ftxui::Event::Return);  // confirm

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest, BagScrollWithNoSlotsShowsThatOutcome) {
  sword_.set_upgrade_slots(0);
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
  controller_->OnEvent(ftxui::Event::Return);     // open the row's menu
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll, which has no slot

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

// --- the Golden Hammer ---

// The whole trip: the entry opens the question, the answer takes the price,
// and the slot it buys is there afterwards.
TEST_F(TuiControllerTest, HammerBuysASlotOffTheEquipMenu) {
  LevelTo(UnlockLevel(Feature::kHammer));
  state_->character.AddMeso(kGoldenHammerCost);
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Hammer
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kHammer);

  controller_->OnEvent(ftxui::Event::Return);  // [Confirm]
  EXPECT_EQ(controller_->screen(), kMain);
  const EquipInstance& worn =
      state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_EQ(worn.equip_state().hammers(), 1);
  EXPECT_EQ(worn.equip_state().remaining_upgrade_slots(),
            sword_.upgrade_slots() + 1);
  EXPECT_EQ(state_->character.meso(), 0);
}

// A purse that cannot cover it gets a red price and a greyed button, and the
// dialog holds rather than closing as though something happened.
TEST_F(TuiControllerTest, AnUnaffordableHammerChangesNothing) {
  LevelTo(UnlockLevel(Feature::kHammer));
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kHammer);
  EXPECT_FALSE(controller_->hammer_panel().affordable());

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kHammer) << "it closed on a refusal";
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .equip_state()
                .hammers(),
            0);
}

// The third press on one item. The entry stands rather than vanishing, and
// says why nothing happened.
TEST_F(TuiControllerTest, AFullyHammeredItemSaysSo) {
  LevelTo(UnlockLevel(Feature::kHammer));
  state_->character.AddMeso(9 * kGoldenHammerCost);
  Equip state;
  state.set_equip_name(sword_.name());
  state.set_remaining_upgrade_slots(sword_.upgrade_slots());
  state.set_hammers(kMaxHammers);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_, state));
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kHammerNotice);
  ASSERT_EQ(controller_->notice_lines().size(), 1u);
  EXPECT_EQ(controller_->notice_lines()[0], "This item is fully Hammered.");
  EXPECT_TRUE(controller_->notice_is_refusal()) << "it is drawn in red";
  EXPECT_EQ(state_->character.meso(), 9 * kGoldenHammerCost);

  // And it leaves the player where they pressed, on the item's menu.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kItemMenu);
}

// The bag's copy of the same trip, which is the other half of ItemRef.
TEST_F(TuiControllerTest, HammerBuysASlotOffTheBagMenu) {
  LevelTo(UnlockLevel(Feature::kHammer));
  state_->character.AddMeso(kGoldenHammerCost);
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Hammer
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kHammer);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.inventory()[0].equip_state().hammers(), 1);
}

// A hammer opens a slot, and stars wait for an item with nothing left to
// scroll. So the entry the player was using goes dim until they spend it.
TEST_F(TuiControllerTest, AHammerHoldsTheStarsUntilItsSlotIsSpent) {
  LevelTo(UnlockLevel(Feature::kHammer));
  state_->character.AddMeso(kGoldenHammerCost);
  // Every slot spent, so the stars are open: PickUpScrolledSword's weapon has
  // no slots at all, which is a weapon with no shelf for a hammer to widen.
  Equip spent;
  spent.set_equip_name(sword_.name());
  spent.set_scroll_successes(sword_.upgrade_slots());
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_, spent));
  state_->character.Equip(0);
  RenderEquipPanel();
  const EquipInstance& worn =
      state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  ASSERT_TRUE(worn.CanStarForce());

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(state_->character.equipped()
                   .at(EQUIP_SLOT_PRIMARY_WEAPON)
                   .CanStarForce());
}

// --- Star Force via equip panel ---

TEST_F(TuiControllerTest, StarForceActionGoesToStarForce) {
  LevelTo(UnlockLevel(Feature::kStarForce));
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
  LevelTo(UnlockLevel(Feature::kStarForce));
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

// The way out is a button as well as a key: the screen's own [Cancel] closes
// it, so a player who never learned Escape is not stuck on it.
TEST_F(TuiControllerTest, CancelInStarForceGoesToMain) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);      // enter kStarForce
  controller_->OnEvent(ftxui::Event::ArrowRight);  // onto [Cancel]
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, ConfirmingAStarForceOpensTheResultNamingTheEquip) {
  LevelTo(UnlockLevel(Feature::kStarForce));
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
  EXPECT_EQ(controller_->star_force_result().equip_name, "Sword");
  EXPECT_EQ(controller_->star_force_result().stars_before, 0);
}

TEST_F(TuiControllerTest, EnterOnASuccessReturnsToStarForce) {
  LevelTo(UnlockLevel(Feature::kStarForce));
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

// Two cards, one either side of the panel: Tab picks which the arrows scroll,
// and Left and Right stay the button row's.
TEST_F(TuiControllerTest, TabOnStarForceMovesBetweenTheCards) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  PickUpScrolledSword();
  DescendIntoBag();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  ASSERT_EQ(controller_->screen(), kStarForce);
  EXPECT_FALSE(controller_->right_card_focused());

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(controller_->right_card_focused());
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_FALSE(controller_->right_card_focused()) << "and back round";
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(controller_->screen(), kStarForce)
      << "neither Tab nor the scrolling arrows leave";
}

// An item at its last star has no after to draw, so there is no second card
// for Tab to reach and no attempt for Enter to open. The screen is reached by
// starring an item up to its limit: the bag greys the entry for one already
// there.
TEST_F(TuiControllerTest, MaxStarsStarForceHasOneCard) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  state_->character.AddMeso(200'000'000);
  PickUpScrolledSword();
  DescendIntoBag();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);  // enter kStarForce
  ASSERT_EQ(controller_->screen(), kStarForce);

  // Nothing is destroyed below 15 stars, and this sword stops at 5, so the
  // attempts run out of stars to gain rather than out of item.
  const EquipInstance* item = controller_->star_force_item();
  ASSERT_NE(item, nullptr);
  for (int i = 0; i < 200 && item->stars() < item->max_stars(); ++i) {
    controller_->OnEvent(ftxui::Event::Return);  // open the confirm bar
    controller_->OnEvent(ftxui::Event::Return);  // confirm
    controller_->OnEvent(ftxui::Event::Return);  // dismiss the result
  }
  ASSERT_EQ(item->stars(), item->max_stars());
  ASSERT_EQ(controller_->screen(), kStarForce);

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_FALSE(controller_->right_card_focused());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kStarForce);
}

// --- Cubing, from the worn menu ---

TEST_F(TuiControllerTest, CubingActionGoesToTheCubingScreen) {
  LevelTo(UnlockLevel(Feature::kPotential));
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();

  controller_->OpenEquipMenu();
  WalkGearMenuTo(kGearMenuCube);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kCubing);
  EXPECT_EQ(controller_->cube_item(),
            &state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Confirm buys a roll and stays where it is: the player presses again over
// the lines they were just handed.
TEST_F(TuiControllerTest, ConfirmRerollsWithoutLeavingTheScreen) {
  LevelTo(UnlockLevel(Feature::kPotential));
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();
  state_->character.AddMeso(2 * kCubeCost);

  controller_->OpenEquipMenu();
  WalkGearMenuTo(kGearMenuCube);
  controller_->OnEvent(ftxui::Event::Return);  // the screen
  controller_->OnEvent(ftxui::Event::Return);  // the question
  controller_->OnEvent(ftxui::Event::Return);  // the roll

  EXPECT_EQ(controller_->screen(), kCubing);
  EXPECT_EQ(state_->character.proto().meso(), kCubeCost);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .potential()
                .rank(),
            POTENTIAL_RANK_RARE);
}

// A cube that ranks the potential up lights the question gold, and the next
// key puts it out. Rerolled until one lands: Rare ranks up one cube in seven,
// so the loop is a formality.
TEST_F(TuiControllerTest, ARankUpLightsTheCubeQuestionUntilTheNextKey) {
  LevelTo(UnlockLevel(Feature::kPotential));
  PickUpScrolledSword();
  state_->character.Equip(0);
  RenderEquipPanel();
  state_->character.AddMeso(500 * kCubeCost);

  controller_->OpenEquipMenu();
  WalkGearMenuTo(kGearMenuCube);
  controller_->OnEvent(ftxui::Event::Return);  // the screen
  controller_->OnEvent(ftxui::Event::Return);  // the question
  controller_->OnEvent(ftxui::Event::Return);  // the grant, always Rare
  EXPECT_EQ(CubeQuestionColor(), kTheme) << "a grant is not a rank up";

  const EquipInstance& worn =
      state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  for (int i = 0; i < 200 && worn.potential().rank() == POTENTIAL_RANK_RARE;
       ++i) {
    controller_->OnEvent(ftxui::Event::Return);
  }
  ASSERT_GT(worn.potential().rank(), POTENTIAL_RANK_RARE);
  EXPECT_EQ(CubeQuestionColor(), kYellow);

  controller_->OnEvent(ftxui::Event::Custom);
  EXPECT_EQ(CubeQuestionColor(), kYellow) << "a redraw is nobody doing"
                                             " anything";
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(CubeQuestionColor(), kTheme);
}

// --- Star Force via bag panel ---

TEST_F(TuiControllerTest, BagStarForceGoesToStarForce) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  PickUpScrolledSword();
  DescendIntoBag();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kStarForce);
}

TEST_F(TuiControllerTest, BagStarForceAttemptGoesToStarForceResult) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  PickUpScrolledSword();
  DescendIntoBag();

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

TEST_F(TuiControllerTest, NoInspectItemWhenNotInspecting) {
  EXPECT_EQ(controller_->inspect_item(), nullptr);
}

// The accessors resolve whichever half the player opened the modal from. The
// controller settles that when the item is picked, so these cover both halves
// of that decision reaching the right item back.
TEST_F(TuiControllerTest, EquipInspectGoesToInspectOnTheWornItem) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kInspect);
  ASSERT_NE(controller_->inspect_item(), nullptr);
  EXPECT_EQ(controller_->inspect_item(),
            &state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

TEST_F(TuiControllerTest, BagInspectGoesToInspectOnTheBagItem) {
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kInspect);
  ASSERT_NE(controller_->inspect_item(), nullptr);
  EXPECT_EQ(controller_->inspect_item(), &state_->character.inventory()[0]);
}

TEST_F(TuiControllerTest, EquipScrollResolvesTheWornItem) {
  WearASwordAndDraw();

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
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  panel_focus_ = kInventoryPanel;

  EXPECT_EQ(controller_->inspect_item(),
            &state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

// --- Sell via Etc tab ---

// --- Sell via bag panel ---

// Sell sits third from last -- above Multi-Sell and Close -- so three steps
// up the menu's ring reach it from wherever the cursor opened. Counting
// downwards would land somewhere else the moment a gated entry is missing.
void StepToSell(TuiController& controller) {
  for (int i = 0; i < 3; ++i) {
    controller.OnEvent(ftxui::Event::ArrowUp);
  }
}

// Multi-Sell sits directly under Sell, so it is one step nearer the bottom.
void StepToMultiSell(TuiController& controller) {
  controller.OnEvent(ftxui::Event::ArrowUp);
  controller.OnEvent(ftxui::Event::ArrowUp);
}

// The whole of trace recovery from the bag, which nothing else covers: the
// menu entry, the confirm, and the [Continue] that closes the result.
TEST_F(TuiControllerTest, RecoveringATraceEndsOnAResultThatCloses) {
  Equip destroyed;
  destroyed.set_equip_name(sword_.name());
  destroyed.set_scroll_successes(2);
  destroyed.mutable_scroll_stats()->set_attack(5);
  state_->character.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  BagASword();

  // A trace offers Inspect and Recover and nothing else, so the entry is
  // walked to rather than counted to.
  controller_->OpenInventoryMenu();
  for (int i = 0; i < 8 && inventory_panel_->menu().selected() != kMenuRecover;
       ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(inventory_panel_->menu().selected(), kMenuRecover);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kTraceRecover);

  controller_->OnEvent(ftxui::Event::Return);  // open the confirm bar
  controller_->OnEvent(ftxui::Event::Return);  // confirm
  ASSERT_EQ(controller_->screen(), kTraceRecoverResult);
  EXPECT_TRUE(controller_->notice_prompt().open());

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(controller_->notice_prompt().open());
}

// Two cards and a chip row: Tab picks which card the arrows scroll, and the
// chips keep Left and Right.
TEST_F(TuiControllerTest, TabOnTheRecoverScreenMovesBetweenTheCards) {
  Equip destroyed;
  destroyed.set_equip_name(sword_.name());
  destroyed.set_scroll_successes(2);
  state_->character.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  BagASword();

  controller_->OpenInventoryMenu();
  for (int i = 0; i < 8 && inventory_panel_->menu().selected() != kMenuRecover;
       ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kTraceRecover);
  EXPECT_FALSE(controller_->right_card_focused());

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(controller_->right_card_focused());
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_FALSE(controller_->right_card_focused()) << "and back round";
  EXPECT_EQ(controller_->screen(), kTraceRecover) << "Tab does not leave";
}

TEST_F(TuiControllerTest, BagSellOpensTheSellDialog) {
  BagASword();

  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kSellEquip);
}

// Stepping off [Confirm] backs out. The dialog opens on the way through, the
// shop's shelf being what a mis-sale is undone by.
TEST_F(TuiControllerTest, BagSellCanBeSteppedAwayFrom) {
  sword_.set_sell_price(900);
  BagASword();

  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);      // open the dialog
  controller_->OnEvent(ftxui::Event::ArrowRight);  // Confirm -> Cancel
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.meso(), 0);
}

TEST_F(TuiControllerTest, BagSellConfirmedTakesTheItemAndPays) {
  sword_.set_sell_price(900);
  BagASword();

  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);  // open the dialog
  controller_->OnEvent(ftxui::Event::Return);  // lands on Confirm

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 0);
  EXPECT_EQ(state_->character.meso(), 900);
}

TEST_F(TuiControllerTest, BagSellEscapeKeepsTheItem) {
  sword_.set_sell_price(900);
  BagASword();

  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.meso(), 0);
}

// A trace pays nothing however dear the item behind it was, and still goes.
// This is the whole of how a trace leaves the bag.
TEST_F(TuiControllerTest, BagSellThrowsATraceAwayForNothing) {
  sword_.set_sell_price(900);
  state_->character.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  DescendIntoBag();

  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);
  // The dialog has to say so too. What it prints and what the sale pays are
  // worked out separately, so one can go wrong while the other is right.
  EXPECT_NE(RenderSellDialog().find("Sell for \U0001FA99 0"),
            std::string::npos);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.inventory().size(), 0);
  EXPECT_EQ(state_->character.meso(), 0);
}

// The dialog is opened on the row the cursor is on, not on the first row.
TEST_F(TuiControllerTest, BagSellTakesTheSelectedRow) {
  EquipPrototype dagger;
  dagger.set_name("Dagger");
  dagger.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  dagger.set_sell_price(40);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.PickUp(std::make_unique<EquipInstance>(dagger));
  panel_focus_ = kInventoryPanel;
  RenderInventoryPanel();

  inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // into the list
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // onto the dagger
  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  ASSERT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.inventory()[0].name(), "Sword");
  EXPECT_EQ(state_->character.meso(), 40);
}

// --- multi-sell ---

TEST_F(TuiControllerTest, MultiSellOpensOnTheRowItWasCalledFrom) {
  EquipPrototype dagger;
  dagger.set_name("Dagger");
  dagger.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  dagger.set_sell_price(40);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.PickUp(std::make_unique<EquipInstance>(dagger));
  panel_focus_ = kInventoryPanel;
  RenderInventoryPanel();

  inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // into the list
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // onto the dagger
  controller_->OpenInventoryMenu();
  StepToMultiSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMultiSell);
  EXPECT_EQ(controller_->multi_sell_basket().equips, std::set<int>({1}));
}

TEST_F(TuiControllerTest, MultiSellConfirmedSellsEverythingMarked) {
  sword_.set_sell_price(900);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  BagASword();
  RenderInventoryPanel();

  controller_->OpenInventoryMenu();
  StepToMultiSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // onto the second row
  controller_->OnEvent(ftxui::Event::Return);     // mark it too
  controller_->OnEvent(ftxui::Event::ArrowDown);  // onto the buttons
  controller_->OnEvent(ftxui::Event::Return);     // open the dialog
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // it opens on Cancel
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 0);
  EXPECT_EQ(state_->character.meso(), 1800);
}

TEST_F(TuiControllerTest, MultiSellEscapeKeepsEverything) {
  sword_.set_sell_price(900);
  BagASword();

  controller_->OpenInventoryMenu();
  StepToMultiSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.meso(), 0);
}

// The Use tab's menu leads to the same screen, opened on that tab.
TEST_F(TuiControllerTest, MultiSellOpensFromAStackToo) {
  ItemPrototype potion;
  potion.set_name("Red Potion");
  potion.set_category(ITEM_CATEGORY_USE);
  potion.set_sell_price(50);
  state_->character.AddStackable(potion, 4);
  panel_focus_ = kInventoryPanel;
  RenderInventoryPanel();

  inventory_component_->OnEvent(ftxui::Event::ArrowRight);  // onto Use
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);   // onto the stack
  controller_->OpenInventoryMenu();
  // The stack menu is one entry shorter: Inspect, Use, Sell, Multi-Sell,
  // Close, with Use hidden on Etc alone.
  StepToMultiSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMultiSell);
  EXPECT_EQ(controller_->multi_sell_basket().use, std::set<int>({0}));
}

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

// Escape out of the menu leaves the list where it was, so the next Escape is
// the one that leaves the shop.
TEST_F(TuiControllerTest, EscapeFromTheShopMenuReturnsToTheList) {
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

// The dialog is seeded from the character when it opens, so buying one and
// coming back has to say two. A panel test cannot see this: BuyPanel is told a
// number, and would look right either way if the controller kept passing zero.
TEST_F(TuiControllerTest, TheBuyDialogCountsWhatIsOwned) {
  state_->character.AddMeso(25000);
  OpenBuyDialog();
  ASSERT_NE(RenderBuyDialog().find("Owned: 0"), std::string::npos);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);     // bought one
  ASSERT_EQ(state_->character.inventory().size(), 1);

  controller_->OnEvent(ftxui::Event::Return);     // -> kShopMenu, on Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Buy
  controller_->OnEvent(ftxui::Event::Return);     // -> kShopBuy again
  EXPECT_NE(RenderBuyDialog().find("Owned: 1"), std::string::npos);
}

// The token shelf charges in tokens and leaves the meso alone -- the same
// dialog, counting a different balance.
TEST_F(TuiControllerTest, BuyingWithATokenSpendsTheTokenAndNotTheMeso) {
  state_->character.AddMeso(25000);
  state_->character.AddStackable(token_, 2);
  OpenTokenBuyDialog();
  std::string dialog = RenderBuyDialog();
  ASSERT_NE(dialog.find("Frozen Sword"), std::string::npos);
  EXPECT_NE(dialog.find("● 1"), std::string::npos) << "priced in its mark";

  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.meso(), 25000);
  EXPECT_EQ(state_->character.CountStackable(token_), 1);
  ASSERT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.inventory()[0].name(), "Frozen Sword");
}

// The tokens held are a ceiling on the field, as the meso is: a number past
// them cannot be typed, so the shop is never offered an order it would refuse.
TEST_F(TuiControllerTest, TheTokenDialogCapsTheOrderAtTheTokensHeld) {
  state_->character.AddStackable(token_, 1);
  OpenTokenBuyDialog();
  controller_->OnEvent(ftxui::Event::Backspace);
  controller_->OnEvent(ftxui::Event::Character('2'));
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.CountStackable(token_), 0);
  EXPECT_EQ(state_->character.inventory().size(), 1)
      << "one, not the two typed";
}

// Meso is no help on this shelf: with no token the dialog opens on a quantity
// of zero and Confirm does nothing.
TEST_F(TuiControllerTest, TheTokenDialogIsInertWithoutAToken) {
  state_->character.AddMeso(25000);
  OpenTokenBuyDialog();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.meso(), 25000);
  EXPECT_EQ(state_->character.inventory().size(), 0);
  EXPECT_EQ(controller_->screen(), kShopBuy) << "Confirm should be inert";
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

// --- the buy-back shelf ---

TEST_F(TuiControllerTest, BuyingBackAnEquipReturnsTheItemThatLeft) {
  sword_.set_sell_price(900);
  Equip starred;
  starred.set_equip_name("Sword");
  starred.set_stars(11);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_, starred));
  state_->character.SellEquip(0);
  int64_t meso = state_->character.meso();

  OpenBuyBackDialog();
  ASSERT_EQ(controller_->screen(), kShopBuy);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // the field -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kShop);
  EXPECT_EQ(state_->character.meso(), meso - 900);
  ASSERT_EQ(state_->character.inventory().size(), 1);
  const EquipInstance* item = state_->character.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->stars(), 11) << "came back stripped";
  EXPECT_TRUE(state_->character.buy_backs().empty());
}

TEST_F(TuiControllerTest, BuyingBackPartOfAStackLeavesTheRest) {
  ItemPrototype shell;
  shell.set_name("Green Snail Shell");
  shell.set_category(ITEM_CATEGORY_ETC);
  shell.set_sell_price(7);
  state_->items["green_snail_shell"] = shell;
  state_->character.AddStackable(shell, 50);
  state_->character.SellStackable(ITEM_CATEGORY_ETC, 0, 50);
  int64_t meso = state_->character.meso();

  OpenBuyBackDialog();
  ASSERT_EQ(controller_->screen(), kShopBuy);
  // The dialog opens on the whole row; [1] takes one copy of it.
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.meso(), meso - 7);
  EXPECT_EQ(state_->character.CountStackable(shell), 1);
  ASSERT_EQ(state_->character.buy_backs().size(), 1);
  EXPECT_EQ(state_->character.buy_backs().Get(0).stack().count(), 49);
}

TEST_F(TuiControllerTest, CancellingABuyBackChangesNothing) {
  sword_.set_sell_price(900);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state_->character.SellEquip(0);
  int64_t meso = state_->character.meso();

  OpenBuyBackDialog();
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kShop);
  EXPECT_EQ(state_->character.meso(), meso);
  EXPECT_EQ(state_->character.buy_backs().size(), 1);
  EXPECT_EQ(state_->character.inventory().size(), 0);
}

// Inspect shows the item the sale kept, not a fresh one off the shelf: the
// stars are the whole reason the row is worth buying back.
TEST_F(TuiControllerTest, InspectingAShelfRowShowsTheItemThatLeft) {
  sword_.set_sell_price(900);
  Equip starred;
  starred.set_equip_name("Sword");
  starred.set_stars(11);
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_, starred));
  state_->character.SellEquip(0);

  OpenBuyBackShelf();
  controller_->OnEvent(ftxui::Event::Return);  // -> kShopMenu, on Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kShopInspect);
}

// --- Equip via bag panel ---

TEST_F(TuiControllerTest, ReturnActionEquipsAndLeavesFocusWhereItWas) {
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(state_->character.equipped().empty());
  EXPECT_TRUE(state_->character.inventory().empty());
  EXPECT_EQ(controller_->screen(), kMain);
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

// Enter opens the map's menu, which lands on Move, so travelling is Enter
// twice.
TEST_F(TuiControllerTest, MoveInTheMapMenuTravelsThere) {
  LoadTwoMaps();
  state_->current_map = "cave";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Cave -> Field
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMapMenu);
  EXPECT_EQ(state_->current_map, "cave");  // nothing moves until Move is taken

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->current_map, "field");
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, InspectInTheMapMenuOpensTheMobsOfThatMap) {
  LoadTwoMaps();
  state_->current_map = "cave";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Cave -> Field
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Move -> Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMobInspect);
  // Read, not travelled to.
  EXPECT_EQ(state_->current_map, "cave");
  EXPECT_FALSE(mob_inspect_panel_->selected_mob().empty());

  // Escape comes back to the list it was opened from, not to the game.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMapSelect);
  EXPECT_EQ(map_select_panel_->selected_map(), "field");
}

TEST_F(TuiControllerTest, CloseAndEscapeLeaveTheMapMenuAlone) {
  LoadTwoMaps();
  state_->current_map = "cave";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMapSelect);

  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Move -> Close, the ring
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMapSelect);
  EXPECT_EQ(state_->current_map, "cave");
}

TEST_F(TuiControllerTest, LeftAndRightInMapSelectChangeTheLevelBand) {
  LoadTwoMaps();
  AddMapOnTheSecondBand();
  state_->current_map = "field";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowUp);     // field is row 0 -> the bar
  controller_->OnEvent(ftxui::Event::ArrowRight);  // 1-10 -> 11-30
  EXPECT_EQ(map_select_panel_->selected_map(), "temple");

  controller_->OnEvent(ftxui::Event::ArrowLeft);  // back down
  EXPECT_EQ(map_select_panel_->selected_map(), "field");
}

// The bar owns Left and Right, as it does in the bag and the shop. In the list
// they are a key that would change the list under the cursor.
TEST_F(TuiControllerTest, LeftAndRightInTheMapListDoNothing) {
  LoadTwoMaps();
  AddMapOnTheSecondBand();
  state_->current_map = "field";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(map_select_panel_->selected_map(), "field");
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(map_select_panel_->selected_map(), "field");
}

// Enter belongs to the list. On the bar it does nothing -- the bar stands on
// no map, so a menu there would be about nothing.
TEST_F(TuiControllerTest, EnterTravelsToAMapOnAnotherBand) {
  LoadTwoMaps();
  AddMapOnTheSecondBand();
  state_->current_map = "field";

  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::ArrowUp);  // onto the chip bar
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMapSelect);

  controller_->OnEvent(ftxui::Event::ArrowDown);  // into the band's list
  controller_->OnEvent(ftxui::Event::Return);
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

TEST_F(TuiControllerTest, MapSelectSwallowsMainScreenKeys) {
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

  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, {});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PartyInspectPanel party_inspect(fresh);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  PotInfoPanel pots;
  InspectPanel item_card;
  InspectPanel trace_card;
  CubePanel cube;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,         bag,  scroll,   item_card,
                     trace_card, star,          cube, trace,    sell,
                     sell_equip, multi_sell,    maps, mobs,     bosses,
                     party,      party_inspect, shop, buy,      jobs,
                     skill_card, pots,          menu, keybinds, options},
      analysis, keys, focus);

  EXPECT_TRUE(controller.PanelVisible(kCharPanel));
  EXPECT_TRUE(controller.PanelVisible(kCombatPanel));
  EXPECT_FALSE(controller.PanelVisible(kEquipPanel));
  EXPECT_FALSE(controller.PanelVisible(kInventoryPanel));
  EXPECT_FALSE(controller.PanelVisible(kMenuPanel));

  while (fresh.character.proto().level() < UnlockLevel(Feature::kEquipped)) {
    fresh.character.LevelUp();
  }
  EXPECT_TRUE(controller.PanelVisible(kEquipPanel));
  EXPECT_FALSE(controller.PanelVisible(kInventoryPanel))
      << "the bag comes a level later";

  fresh.character.LevelUp();
  EXPECT_TRUE(controller.PanelVisible(kInventoryPanel));
  EXPECT_FALSE(controller.PanelVisible(kMenuPanel))
      << "the corner menu waits for the hotkeys tip to retire";

  fresh.character.LevelUp();
  EXPECT_TRUE(controller.PanelVisible(kMenuPanel));
  EXPECT_FALSE(HotkeysTipVisible(fresh.character, fresh.account))
      << "the corner holds one or the other, never both";
}

// Tab rounds the panels that exist. At level 1 that is two of them, so it
// cannot leave the player pressing Tab at a panel that is not on screen.
TEST_F(TuiControllerTest, TabSkipsThePanelsThatAreNotThereYet) {
  GameState fresh({}, {}, {}, {}, {});
  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, {});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PartyInspectPanel party_inspect(fresh);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  PotInfoPanel pots;
  InspectPanel item_card;
  InspectPanel trace_card;
  CubePanel cube;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,         bag,  scroll,   item_card,
                     trace_card, star,          cube, trace,    sell,
                     sell_equip, multi_sell,    maps, mobs,     bosses,
                     party,      party_inspect, shop, buy,      jobs,
                     skill_card, pots,          menu, keybinds, options},
      analysis, keys, focus);

  controller.OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(focus, kCombatPanel) << "past both locked panels";
  controller.OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(focus, kCharPanel) << "and back round";
}

// And backwards over the same gap. Going the other way walks into the two
// locked panels from the far side, which is where a step of -1 would have run
// the modulo negative.
TEST_F(TuiControllerTest, ShiftTabSkipsThePanelsThatAreNotThereYet) {
  GameState fresh({}, {}, {}, {}, {});
  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, {});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PartyInspectPanel party_inspect(fresh);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  PotInfoPanel pots;
  InspectPanel item_card;
  InspectPanel trace_card;
  CubePanel cube;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,         bag,  scroll,   item_card,
                     trace_card, star,          cube, trace,    sell,
                     sell_equip, multi_sell,    maps, mobs,     bosses,
                     party,      party_inspect, shop, buy,      jobs,
                     skill_card, pots,          menu, keybinds, options},
      analysis, keys, focus);

  controller.OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(focus, kCombatPanel) << "back past both locked panels";
  controller.OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(focus, kCharPanel) << "and back round";
}

// The game opens focused on the equipped panel, which a level 1 character
// does not have. Focus has to leave before a key is dispatched, or it lands
// on a panel the player cannot see.
TEST_F(TuiControllerTest, FocusLeavesAPanelThatIsNotOnScreen) {
  GameState fresh({}, {}, {}, {}, {});
  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, {});
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PartyInspectPanel party_inspect(fresh);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  PotInfoPanel pots;
  InspectPanel item_card;
  InspectPanel trace_card;
  CubePanel cube;
  int focus = kEquipPanel;  // where the game starts
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,         bag,  scroll,   item_card,
                     trace_card, star,          cube, trace,    sell,
                     sell_equip, multi_sell,    maps, mobs,     bosses,
                     party,      party_inspect, shop, buy,      jobs,
                     skill_card, pots,          menu, keybinds, options},
      analysis, keys, focus);

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

// --- the welcome-back card ---

namespace {

// An hour away with something to show for it.
OfflineReport PaidReport() {
  OfflineReport report;
  report.farmed = true;
  report.absence = 3600.0;
  report.seconds = 3600.0;
  report.kills = 100;
  return report;
}

}  // namespace

TEST_F(TuiControllerTest, TheOfflineCardStandsOverTheMainViewUntilDismissed) {
  controller_->OpenOfflineReport(PaidReport());
  ASSERT_EQ(controller_->screen(), kOffline);
  EXPECT_EQ(controller_->offline_report().kills, 100);

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// A player who restarted the game a minute after closing it is shown nothing.
TEST_F(TuiControllerTest, TooShortAnAbsenceRaisesNoCard) {
  OfflineReport report = PaidReport();
  report.absence = kOfflineNoticeSeconds - 1.0;

  controller_->OpenOfflineReport(report);

  EXPECT_EQ(controller_->screen(), kMain);
}

// Nor is one raised for a player who logged off in town: there is nothing to
// tell them.
TEST_F(TuiControllerTest, NothingFarmedRaisesNoCard) {
  OfflineReport report = PaidReport();
  report.farmed = false;

  controller_->OpenOfflineReport(report);

  EXPECT_EQ(controller_->screen(), kMain);
}

// --- the expanded bag ---

// The expanded bag is the main view, so Escape closes it rather than opening
// the quit prompt, and the game is only asked about once it is shut.
TEST_F(TuiControllerTest, EscapeClosesTheExpandedPanelBeforeTheGame) {
  controller_->ToggleExpanded(kInventoryPanel);
  ASSERT_EQ(controller_->expanded_panel(), kInventoryPanel);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->expanded_panel(), kNoPanel);
  EXPECT_EQ(controller_->screen(), kMain);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kQuit);
}

// The button closes the panel it is on and moves the expansion off any other,
// so the two panels that carry one cannot both be open.
TEST_F(TuiControllerTest, OnlyOnePanelIsExpandedAtATime) {
  controller_->ToggleExpanded(kInventoryPanel);
  controller_->ToggleExpanded(kEquipPanel);
  EXPECT_EQ(controller_->expanded_panel(), kEquipPanel);

  controller_->ToggleExpanded(kEquipPanel);
  EXPECT_EQ(controller_->expanded_panel(), kNoPanel);
}

// Nothing to walk to: the other panels are not drawn behind it.
TEST_F(TuiControllerTest, TabDoesNotLeaveTheExpandedPanel) {
  panel_focus_ = kInventoryPanel;
  controller_->ToggleExpanded(kInventoryPanel);

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kInventoryPanel);
}

// --- quitting ---

TEST_F(TuiControllerTest, EscapeOnTheMainScreenAsksBeforeQuitting) {
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kQuit);
  EXPECT_FALSE(controller_->quit_requested()) << "asked, not acted on";
}

// The one keystroke that must not be able to end the game on its own. Nothing
// is saved, so the prompt opens on Cancel and a second Enter answers "no".
TEST_F(TuiControllerTest, TheQuitPromptOpensOnCancel) {
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(controller_->quit_requested());
}

TEST_F(TuiControllerTest, ConfirmingTheQuitPromptRequestsTheQuit) {
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // Cancel -> Confirm
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(controller_->quit_requested());
}

TEST_F(TuiControllerTest, EscapeBacksOutOfTheQuitPrompt) {
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(controller_->quit_requested());
}

// Escape means "leave what is open" everywhere else, and only means "leave the
// game" once there is nothing else open. The quit branch sits below every
// screen branch in OnEvent to get this, so it is worth pinning.
TEST_F(TuiControllerTest, EscapeLeavesTheScreenNotTheGame) {
  controller_->OpenMapSelect();
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(controller_->quit_requested());
}

// --- the boss screen ---

TEST_F(TuiControllerTest, TheBossEntryOpensTheBossScreenAndClearsItsGold) {
  ASSERT_FALSE(state_->account.Seen(MenuPanel::boss_seen_key()));
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_TRUE(state_->account.Seen(MenuPanel::boss_seen_key()));
}

// --- settings and keybinds ---

TEST_F(TuiControllerTest, SettingsOpensItsBoxOverTheCorner) {
  controller_->OpenMenuEntry(MenuEntry::kSettings);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
  // The cursor is still on the menu row until the player walks up into it.
  EXPECT_EQ(menu_panel_->box_cursor(), -1);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(menu_panel_->box_cursor(), 1);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(menu_panel_->box_cursor(), -1);
  // Escape puts the box away.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(menu_panel_->box_open());
}

// The box hangs off the Settings entry, so walking the menu row off it takes
// the box away too.
TEST_F(TuiControllerTest, WalkingOffSettingsClosesItsBox) {
  LevelTo(UnlockLevel(Feature::kBoss));
  // Boss and Party arrive to the left of the rest and take the cursor with
  // them.
  menu_panel_->MoveCursor(3);
  ASSERT_EQ(menu_panel_->selected(), MenuEntry::kSettings);
  controller_->OpenMenuEntry(MenuEntry::kSettings);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_FALSE(menu_panel_->box_open());
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(menu_panel_->selected(), MenuEntry::kAnalysis);
}

// Inside the box the row below is not what the keys are moving on.
TEST_F(TuiControllerTest, LeftInsideTheBoxDoesNothing) {
  controller_->OpenMenuEntry(MenuEntry::kSettings);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_TRUE(menu_panel_->box_open());
  EXPECT_EQ(menu_panel_->box_cursor(), 1);
}

TEST_F(TuiControllerTest, KeybindsOpensFromTheBoxAndComesBackToIt) {
  OpenKeybinds();
  EXPECT_EQ(controller_->screen(), kKeybinds);
  // Escape on a slot with nothing in it goes back to the box, which is still
  // standing where it was left.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
}

TEST_F(TuiControllerTest, EnterOnASlotTakesTheNextKeyPressed) {
  OpenKeybinds();
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(controller_->capturing_key());
  // The ticker's redraw is not somebody pressing a key.
  controller_->OnEvent(ftxui::Event::Custom);
  EXPECT_TRUE(controller_->capturing_key());
  controller_->OnEvent(ftxui::Event::w);
  EXPECT_FALSE(controller_->capturing_key());
  EXPECT_EQ(keys_->Label(KEY_ACTION_UP, 1), "W");
  EXPECT_EQ(keys_->Translate(ftxui::Event::w), ftxui::Event::ArrowUp);
}

// The name field takes letters, so while it is open a key has to arrive as
// the player pressed it rather than as whatever action it is bound to.
TEST_F(TuiControllerTest, AnOpenNameFieldWantsTheRawKeys) {
  ftxui::Component chars = char_panel_->MakeComponent();
  panel_focus_ = kCharPanel;
  EXPECT_FALSE(controller_->capturing_key());
  chars->OnEvent(ftxui::Event::ArrowUp);  // onto the username row
  chars->OnEvent(ftxui::Event::Return);   // open the field
  EXPECT_TRUE(controller_->capturing_key());
  chars->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(controller_->capturing_key());
}

// Escape on the main view asks whether to quit, so an open field has to keep
// it: backing out of a field is not backing out of the game.
TEST_F(TuiControllerTest, EscapeLeavesTheNameFieldRatherThanTheGame) {
  ftxui::Component chars = char_panel_->MakeComponent();
  panel_focus_ = kCharPanel;
  chars->OnEvent(ftxui::Event::ArrowUp);
  chars->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(controller_->capturing_key());

  EXPECT_FALSE(controller_->OnEvent(ftxui::Event::Escape));
  EXPECT_EQ(controller_->screen(), kMain) << "no quit prompt";
  chars->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(controller_->capturing_key());
}

// Tab would carry focus off a panel mid-edit, leaving a field open that no
// key can reach -- and every rebound key dead behind it.
TEST_F(TuiControllerTest, TabDoesNotLeaveAnOpenNameFieldBehind) {
  ftxui::Component chars = char_panel_->MakeComponent();
  panel_focus_ = kCharPanel;
  chars->OnEvent(ftxui::Event::ArrowUp);
  chars->OnEvent(ftxui::Event::Return);

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kCharPanel);
}

TEST_F(TuiControllerTest, EscapeClearsASlotAndThenLeaves) {
  OpenKeybinds();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::w);
  ASSERT_EQ(keys_->Label(KEY_ACTION_UP, 1), "W");
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(keys_->Label(KEY_ACTION_UP, 1), "");
  EXPECT_EQ(controller_->screen(), kKeybinds);
  // Nothing left to clear, so the same key is the way out.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
}

TEST_F(TuiControllerTest, AReservedKeyIsRefused) {
  OpenKeybinds();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(controller_->capturing_key());
  EXPECT_EQ(keys_->Label(KEY_ACTION_UP, 1), "");
  // The screen stayed put: the refusal is a message, not a way out.
  EXPECT_EQ(controller_->screen(), kKeybinds);
}

TEST_F(TuiControllerTest, TheCloseButtonLeavesTheScreen) {
  OpenKeybinds();
  controller_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_TRUE(keybinds_panel_->on_close());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMenuBox);
}

TEST_F(TuiControllerTest, OptionsOpensFromTheBoxAndComesBackToIt) {
  OpenOptions();
  EXPECT_EQ(controller_->screen(), kOptions);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
}

// The switch takes effect where it is thrown: there is no confirmation and
// nothing to close before the panels are drawing the other way.
TEST_F(TuiControllerTest, EnterOnAnOptionThrowsItsSwitch) {
  OpenOptions();
  ASSERT_FALSE(state_->account.panel_title_blink());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(state_->account.panel_title_blink());
  EXPECT_EQ(controller_->screen(), kOptions) << "the screen stays up";
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(state_->account.panel_title_blink());
}

TEST_F(TuiControllerTest, TheOptionsCloseButtonLeavesTheScreen) {
  OpenOptions();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_TRUE(options_panel_->on_close());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  // Close threw no switch on its way out.
  EXPECT_FALSE(state_->account.panel_title_blink());
}

// --- battle analysis ---

// The first row of the box works the tool, and the box stays up: the row the
// player pressed has just become the other one.
TEST_F(TuiControllerTest, TheAnalysisBoxStartsAndStopsTheTool) {
  controller_->OpenMenuEntry(MenuEntry::kAnalysis);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_EQ(menu_panel_->box_entry(), MenuEntry::kAnalysis);

  // The box stands above the menu row, so Up reaches its bottom entry first
  // and Start, listed on top, is the second stop.
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  ASSERT_EQ(menu_panel_->selected_analysis_entry(), AnalysisEntry::kStartStop);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(analysis_.state(), AnalysisState::kWaitingToStart);
  EXPECT_EQ(controller_->screen(), kMenuBox);

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(analysis_.state(), AnalysisState::kStopped);
}

// A stop pressed by mistake is taken back by pressing the same row again.
TEST_F(TuiControllerTest, TheAnalysisEntryTakesBackAPendingStop) {
  AnalysisSample beat;
  beat.respawned = true;
  analysis_.Start();
  analysis_.Advance(beat);
  ASSERT_EQ(analysis_.state(), AnalysisState::kRunning);

  controller_->OpenMenuEntry(MenuEntry::kAnalysis);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(analysis_.state(), AnalysisState::kWaitingToStop);

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(analysis_.state(), AnalysisState::kRunning);
}

TEST_F(TuiControllerTest, ViewOpensTheAnalysisOverlayAndBackClosesIt) {
  controller_->OpenMenuEntry(MenuEntry::kAnalysis);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  ASSERT_EQ(menu_panel_->selected_analysis_entry(), AnalysisEntry::kView);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kAnalysis);

  // Escape goes back to the box, which is still standing where it was left.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
}

TEST_F(TuiControllerTest, EnterOnAFightAsksBeforeTakingIt) {
  HoldASword();
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossConfirm);
  EXPECT_EQ(controller_->boss_prompt_title(), "Normal Zakum");

  // The prompt opens on Confirm, and Escape backs out to the list.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBossSelect);
}

// A boss already cleared this reset says when he comes back rather than asking
// a question whose answer is no. Named without a difficulty, since a clear of
// any of them closes the rest.
TEST_F(TuiControllerTest, AClearedFightSaysWhenItComesBack) {
  HoldASword();
  state_->character.RecordBossClear("zakum", "Normal",
                                    static_cast<int64_t>(std::time(nullptr)));
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_FALSE(controller_->notice_is_refusal()) << "the reset, not the player";
  EXPECT_EQ(controller_->notice_lines()[0], "Zakum");
  EXPECT_EQ(controller_->notice_lines()[1], "has already been killed today.");
  EXPECT_TRUE(controller_->notice_prompt().open());

  // The notice holds the screen until it is dismissed.
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_FALSE(controller_->notice_prompt().open());
}

// The rung beside the one that was taken is closed too, and says so under the
// boss's own name rather than the difficulty the cursor happens to be on.
TEST_F(TuiControllerTest, AClearOfOneDifficultyClosesTheOthers) {
  HoldASword();
  BossDifficulty* chaos = state_->bosses["zakum"].add_difficulties();
  chaos->set_name("Chaos");
  chaos->set_reset(RESET_PERIOD_DAILY);
  chaos->set_time_limit_seconds(300);
  *chaos->mutable_phases() = state_->bosses["zakum"].difficulties(0).phases();
  state_->character.RecordBossClear("zakum", "Normal",
                                    static_cast<int64_t>(std::time(nullptr)));
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_EQ(controller_->notice_lines()[0], "Zakum");
  EXPECT_EQ(controller_->notice_lines()[1], "has already been killed today.");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";
}

// A fight the character has not levelled up to says the level it wants. The
// difficulty is dim on the list, and this is what Enter on it answers.
TEST_F(TuiControllerTest, ALockedFightNamesTheLevelItOpensAt) {
  HoldASword();
  state_->bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_TRUE(controller_->notice_is_refusal()) << "drawn in red";
  EXPECT_EQ(controller_->notice_lines()[0], "Normal Zakum");
  EXPECT_EQ(controller_->notice_lines()[1], "unlocks at level 130.");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";
}

// A fight that is not built yet answers with that and nothing else -- not the
// weapon, not the level, not the reset, none of which is the reason.
TEST_F(TuiControllerTest, AComingSoonFightSaysSoAndStartsNothing) {
  BossDifficulty* chaos = state_->bosses["zakum"].add_difficulties();
  chaos->set_name("Chaos");
  chaos->set_coming_soon(true);
  Spawn* spawn = chaos->add_phases()->add_spawns();
  spawn->set_mob("zakum");
  spawn->set_count(1);
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_TRUE(controller_->notice_is_refusal()) << "drawn in red";
  EXPECT_EQ(controller_->notice_lines()[0], "Chaos Zakum");
  EXPECT_EQ(controller_->notice_lines()[1], "is coming soon!");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";
}

// A character holding nothing cannot fight: the fight used to start and then
// give up on its own, which read as the screen closing for no reason.
TEST_F(TuiControllerTest, AFightRefusesACharacterWithNoWeapon) {
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_TRUE(controller_->notice_is_refusal()) << "drawn in red";
  ASSERT_EQ(controller_->notice_lines().size(), 1u);
  EXPECT_EQ(controller_->notice_lines()[0], "You have no weapon equipped!");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_EQ(controller_->boss_run(), nullptr);
  EXPECT_FALSE(controller_->in_boss_fight());
}

TEST_F(TuiControllerTest, EscapeLeavesTheBossScreen) {
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Tab);  // would cycle focus in kMain
  EXPECT_EQ(controller_->screen(), kBossSelect);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// The Extreme Green Potion is drunk on the way in, whatever the fight does
// next. Charged once per entry rather than per phase or per clear.
TEST_F(TuiControllerTest, TheGreenPotionIsChargedOnTheWayIntoAFight) {
  LevelTo(190);
  state_->character.AddMeso(3'000'000);
  ASSERT_TRUE(
      state_->character.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));

  EnterFight();
  ASSERT_EQ(controller_->screen(), kBossFight);
  EXPECT_EQ(state_->character.meso(), 2'000'000);

  // Switched off, and the next fight is free.
  ASSERT_FALSE(
      state_->character.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // onto Confirm
  controller_->OnEvent(ftxui::Event::Return);
  controller_->AdvanceBossRun(0.0);
  ASSERT_EQ(controller_->screen(), kBossSelect);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossFight);
  EXPECT_EQ(state_->character.meso(), 2'000'000);
}

TEST_F(TuiControllerTest, ConfirmingEntersTheFight) {
  HoldASword();
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossConfirm);
  controller_->OnEvent(ftxui::Event::Return);  // the prompt opens on Confirm

  EXPECT_EQ(controller_->screen(), kBossFight);
  ASSERT_NE(controller_->boss_run(), nullptr);
  EXPECT_EQ(controller_->boss_run()->title(), "Normal Zakum");
  EXPECT_TRUE(controller_->in_boss_fight());
}

// The fight is the whole screen: nothing on it reaches the panels behind, and
// a fight with nowhere to walk swallows the arrows too.
TEST_F(TuiControllerTest, TheFightSwallowsEverythingButEscape) {
  EnterFight();
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->screen(), kBossFight);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// The one thing the player decides during a fight: where they are standing.
TEST_F(TuiControllerTest, TheArrowsWalkThePlayerAroundTheArena) {
  BossPhase* phase =
      state_->bosses["zakum"].mutable_difficulties(0)->mutable_phases(0);
  // The middle of the floor first: that is where the phase starts them.
  const int kSpots[3][2] = {{3, 1}, {0, 1}, {6, 1}};
  for (const int (&spot)[2] : kSpots) {
    ArenaSpot* at = phase->add_player_spots();
    at->set_x(spot[0]);
    at->set_y(spot[1]);
  }
  EnterFight();
  ASSERT_NE(controller_->boss_run(), nullptr);
  ASSERT_EQ(controller_->boss_run()->player_spot().x(), 3);

  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(controller_->boss_run()->player_spot().x(), 0);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(controller_->boss_run()->player_spot().x(), 3);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(controller_->boss_run()->player_spot().x(), 3)
      << "nothing above the floor to climb to";
  EXPECT_EQ(controller_->screen(), kBossFight);
  EXPECT_EQ(panel_focus_, kEquipPanel) << "and nothing leaked behind it";
}

TEST_F(TuiControllerTest, EscapeAsksBeforeLeavingAndTheClockStops) {
  EnterFight();
  controller_->AdvanceBossRun(kBossCountdownSeconds + 5.0);
  double left = controller_->boss_run()->seconds_left();
  ASSERT_LT(left, 300.0);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBossAbort);
  // The clock must not run out while the player is deciding.
  controller_->AdvanceBossRun(60.0);
  EXPECT_DOUBLE_EQ(controller_->boss_run()->seconds_left(), left);

  // The prompt opens on Cancel, so Enter goes back to the fight.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossFight);
  controller_->AdvanceBossRun(1.0);
  EXPECT_LT(controller_->boss_run()->seconds_left(), left);
}

TEST_F(TuiControllerTest, ConfirmingTheLeavePromptEndsTheFight) {
  EnterFight();
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // onto Confirm
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossFight);

  // No closing beat on the way out: the next tick is already back at the list.
  controller_->AdvanceBossRun(0.0);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_EQ(controller_->boss_run(), nullptr);
  EXPECT_FALSE(controller_->in_boss_fight());
  // Walking out is not a clear, so the daily is still there.
  EXPECT_EQ(state_->character.BossClearedAt("zakum", "Normal"), 0);
}

// The clock running out is the one ending the player might not have watched,
// so it says so rather than dropping them back on the list.
TEST_F(TuiControllerTest, RunningOutOfTimeSaysSo) {
  // A monster the character cannot chew through inside the limit: the
  // fixture's arms die to one poke, and a won fight is not this ending.
  state_->mobs["zakum_arm"].set_max_hp(2000000000);
  EnterFight();
  controller_->AdvanceBossRun(kBossCountdownSeconds + 301.0);
  ASSERT_NE(controller_->boss_run(), nullptr) << "the closing beat is running";
  controller_->AdvanceBossRun(kBossEndHoldSeconds);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  // Held until the notice is dismissed, so the arena stands behind it, and
  // the clock is stopped: a fight already lost cannot be lost again.
  EXPECT_NE(controller_->boss_run(), nullptr);
  controller_->AdvanceBossRun(10.0);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  ASSERT_EQ(controller_->notice_lines().size(), 1u);
  EXPECT_EQ(controller_->notice_lines()[0], "Out of time!");
  EXPECT_FALSE(controller_->notice_is_refusal());
  // Nothing was cleared, so the daily is still there.
  EXPECT_EQ(state_->character.BossClearedAt("zakum", "Normal"), 0);

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_EQ(controller_->boss_run(), nullptr);
  EXPECT_FALSE(controller_->in_boss_fight());
}

TEST_F(TuiControllerTest, ClearingTheFightBanksTheDaily) {
  EnterFight();
  RunFightToEnd();
  EXPECT_EQ(controller_->screen(), kBossClear);
  EXPECT_GT(state_->character.BossClearedAt("zakum", "Normal"), 0);
}

// The card outlives the run it reports on, so what it says has to have been
// copied off it rather than read back through a pointer that is now null.
TEST_F(TuiControllerTest, TheClearCardNamesTheFightAndWhatItPaid) {
  BossDifficulty* normal = state_->bosses["zakum"].mutable_difficulties(0);
  normal->set_meso(3062500);
  MobDrop* shard = normal->add_drops();
  shard->set_item("zakums_soul_shard");
  shard->set_per_kill(1.0);
  EnterFight();
  RunFightToEnd();

  ASSERT_EQ(controller_->screen(), kBossClear);
  // The run is kept while the card is up: the arena the player just cleared
  // is what the card stands over.
  EXPECT_NE(controller_->boss_run(), nullptr);
  EXPECT_EQ(controller_->boss_clear_title(), "Normal Zakum");
  EXPECT_GT(controller_->boss_clear_seconds(), 0.0);
  EXPECT_EQ(controller_->boss_clear_reward().meso, 3062500);
  ASSERT_EQ(controller_->boss_clear_reward().items.size(), 1u);
  EXPECT_EQ(controller_->boss_clear_reward().items[0].name, shard_.name());
  EXPECT_TRUE(controller_->boss_clear_prompt().open());

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_EQ(controller_->boss_run(), nullptr);
  EXPECT_FALSE(controller_->in_boss_fight());
}

// The debug Level-Up item is used from the item menu, and a level gained hands
// things over out of the catalogs. Without this the character reaches Arcane
// River with nothing to fight it with.
TEST_F(TuiControllerTest, LevellingFromTheItemMenuStillGrantsTheSymbol) {
  EquipPrototype symbol;
  symbol.set_name("Arcane Symbol: Vanishing Journey");
  symbol.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  symbol.mutable_arcane_symbol()->set_meso_cost_base(8);
  state_->equips["symbol_vanishing_journey"] = symbol;

  ItemPrototype level_up;
  level_up.set_name("Level-Up");
  level_up.set_category(ITEM_CATEGORY_USE);
  level_up.set_effect(ITEM_EFFECT_LEVEL_UP);
  state_->character.AddStackable(level_up, 1);
  LevelTo(199);
  ASSERT_EQ(state_->character.inventory().size(), 0);

  // On the Use tab, cursor on the one stack, with the {Inspect, Use, ...} menu
  // open at Use.
  panel_focus_ = kInventoryPanel;
  inventory_component_->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);   // bar -> the stack
  inventory_component_->OnEvent(ftxui::Event::Return);      // the stack menu
  controller_->OnEvent(ftxui::Event::ArrowDown);            // Inspect -> Use
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.proto().level(), 200);
  ASSERT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.inventory()[0].prototype().name(),
            "Arcane Symbol: Vanishing Journey");
}

// --- Hyper Stats ---

TEST_F(TuiControllerTest, ConfirmingTheHyperQuestionSpendsThePoint) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenHyperAllocate(HYPER_STAT_FIELD_STR, StatPreset::kFarming);
  ASSERT_EQ(controller_->screen(), kHyperAlloc);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR), 1);
}

TEST_F(TuiControllerTest, WalkingAwayFromItSpendsNothing) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenHyperAllocate(HYPER_STAT_FIELD_STR, StatPreset::kFarming);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR), 0);
}

// The question names the allocation, and the answer empties that one alone.
TEST_F(TuiControllerTest, TheResetEmptiesTheAllocationItNamed) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenHyperAllocate(HYPER_STAT_FIELD_STR, StatPreset::kFarming);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OpenHyperAllocate(HYPER_STAT_FIELD_DEX, StatPreset::kBossing);
  controller_->OnEvent(ftxui::Event::Return);

  controller_->OpenHyperReset(StatPreset::kBossing);
  EXPECT_EQ(controller_->hyper_reset_question(), "Reset Boss Hyper Stats?");
  // It opens on Cancel, so getting to Confirm is a step of its own.
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_DEX,
                                               StatPreset::kBossing),
            0);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kFarming),
            1);
}

// The card reads the allocation it was opened on, and the level live off the
// character -- a point spent and the stat opened again says the new one.
TEST_F(TuiControllerTest, TheHyperStatCardReadsTheAllocationItWasOpenedOn) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenHyperAllocate(HYPER_STAT_FIELD_STR, StatPreset::kBossing);
  controller_->OnEvent(ftxui::Event::Return);

  controller_->OpenHyperStatInspect(HYPER_STAT_FIELD_STR, StatPreset::kBossing);
  EXPECT_EQ(controller_->screen(), kHyperStatInspect);
  EXPECT_EQ(controller_->hyper_inspect_field(), HYPER_STAT_FIELD_STR);
  EXPECT_EQ(controller_->hyper_inspect_level(), 1);
  EXPECT_EQ(controller_->hyper_inspect_max_level(),
            state_->character.max_hyper_stat_level());

  // The other allocation has had nothing spent on it.
  controller_->OpenHyperStatInspect(HYPER_STAT_FIELD_STR, StatPreset::kFarming);
  EXPECT_EQ(controller_->hyper_inspect_level(), 0);

  // Nothing to do but read it, so either key leaves.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Enter alone on the reset dialog walks away rather than emptying it.
TEST_F(TuiControllerTest, TheResetOpensOnCancel) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenHyperAllocate(HYPER_STAT_FIELD_STR, StatPreset::kFarming);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OpenHyperReset(StatPreset::kFarming);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR), 1);
}

// --- Inner Ability ---

// Enter toggles the lock and asks nothing -- there is no screen behind it.
TEST_F(TuiControllerTest, LockingALineAsksNothing) {
  LevelTo(kInnerAbilityUnlockLevel);
  // A fresh ability is three Rare lines, and a Rare line holds like any other.
  controller_->ToggleAbilityLock(0, StatPreset::kFarming);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.ability().lines(0).locked());
  controller_->ToggleAbilityLock(0, StatPreset::kFarming);
  EXPECT_FALSE(state_->character.ability().lines(0).locked());

  // An index off the end of the ability is not an ability to change.
  controller_->ToggleAbilityLock(9, StatPreset::kFarming);
  EXPECT_EQ(state_->character.ability().lines_size(), kAbilityLines);
}

// The dialog lists what it would throw away, and nothing that is being held.
TEST_F(TuiControllerTest, TheRerollDialogListsOnlyTheLinesItRerolls) {
  LevelTo(kInnerAbilityUnlockLevel);
  ASSERT_TRUE(state_->character.LockAbilityLine(0, true));

  controller_->OpenAbilityReroll(StatPreset::kFarming);
  EXPECT_EQ(controller_->screen(), kAbilityReroll);
  std::vector<AbilityLine> listed = controller_->ability_reroll_lines();
  ASSERT_EQ(listed.size(), 2u);
  for (const AbilityLine& line : listed) {
    EXPECT_FALSE(line.locked())
        << "a held line is not what is being asked about";
  }
}

// It opens on Confirm, so a reroll is Enter-Enter, and Cancel spends nothing.
TEST_F(TuiControllerTest, TheRerollOpensOnConfirmAndIsPaidOnce) {
  LevelTo(kInnerAbilityUnlockLevel);
  const int64_t cost = state_->character.ability_reset_cost();
  state_->character.AddHonor(2 * cost);
  const int64_t pool = state_->character.honor();

  controller_->OpenAbilityReroll(StatPreset::kFarming);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.honor(), pool - cost);

  controller_->OpenAbilityReroll(StatPreset::kFarming);
  controller_->OnEvent(ftxui::Event::ArrowRight);  // -> Cancel
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.honor(), pool - cost) << "Cancel spends nothing";
}

// A reroll that ranks the ability up lights the character panel, and the next
// key puts it out. Rare ranks up one reset in twenty, so the loop stands in
// for a seed the fixture does not fix.
TEST_F(TuiControllerTest, AnAbilityRankUpLightsTheCharacterPanel) {
  LevelTo(kInnerAbilityUnlockLevel);
  state_->character.AddHonor(1'000'000'000);

  for (int i = 0; i < 500 && !controller_->ability_rank_up(); ++i) {
    controller_->OpenAbilityReroll(StatPreset::kFarming);
    controller_->OnEvent(ftxui::Event::Return);
  }
  ASSERT_TRUE(controller_->ability_rank_up());
  EXPECT_GT(state_->character.ability(StatPreset::kFarming).rank(),
            ABILITY_RANK_RARE);

  controller_->OnEvent(ftxui::Event::Custom);
  EXPECT_TRUE(controller_->ability_rank_up()) << "a redraw is not an action";
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_FALSE(controller_->ability_rank_up());
}

// The two allocations are rerolled apart: the question names one of them.
TEST_F(TuiControllerTest, TheRerollLandsOnTheAllocationItNamed) {
  LevelTo(kInnerAbilityUnlockLevel);
  state_->character.AddHonor(100000);
  const std::string before =
      state_->character.ability(StatPreset::kFarming).DebugString();

  controller_->OpenAbilityReroll(StatPreset::kBossing);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.ability(StatPreset::kFarming).DebugString(),
            before)
      << "the farming ability was not the one asked about";
}

}  // namespace
}  // namespace ms
