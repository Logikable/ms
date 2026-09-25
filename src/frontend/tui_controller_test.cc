#include "src/frontend/tui_controller.h"

#include <gtest/gtest.h>

#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/consumables.h"
#include "src/character/dailies.h"
#include "src/character/link.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/combat/boss_run.h"
#include "src/combat/offline.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/screens/bank_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/cube_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/player_inspect_panel.h"
#include "src/frontend/screens/player_list_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_equip_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/screens/trade_panel.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
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

// A notice is one sentence split into evenly sized lines, so a test that only
// checks the text joins it back into the sentence.
std::string NoticeText(const std::vector<std::string>& lines) {
  std::string sentence;
  for (const std::string& line : lines) {
    sentence += sentence.empty() ? line : " " + line;
  }
  return sentence;
}

class TuiControllerTest : public testing::Test {
 protected:
  // A ScrollPanel holds its catalog by reference, so the catalog must outlive
  // it.
  const std::map<std::string, Scroll> no_scrolls_;

  void SetUp() override {
    MakeState();
    SeedCharacter();
    MakePanels();
  }

  // The catalogs these tests use: small enough to define here instead of
  // loading from data/. Each entry exists because some test below needs it
  // before a panel is built.
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
    // A stand-in under the catalog key a Magician's advancement uses, so the
    // advancement tests see gear arrive without loading data/equip.
    EquipPrototype staff;
    staff.set_name("Wooden Staff");
    staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    equips["wooden_staff"] = staff;
    // Something for the shop to sell. ShopPanel fixes its list at construction,
    // so this must exist before the panel does.
    EquipPrototype machete;
    machete.set_name("Machete");
    machete.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    machete.set_required_level(20);
    machete.set_shop_price(10000);
    equips["machete"] = machete;
    // The token shelf's stock, and the token that buys it.
    EquipPrototype frozen;
    frozen.set_name("Frozen Sword");
    frozen.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    frozen.set_required_level(120);
    frozen.set_token_item("weapon_token");
    frozen.set_token_price(1);
    equips["frozen_sword"] = frozen;
    // Two of the six symbols, which is enough for a claim to have a ladder.
    symbol_.set_name("Arcane Symbol: Vanishing Journey");
    symbol_.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
    symbol_.set_required_level(200);
    symbol_.mutable_arcane_symbol()->set_meso_cost_base(8);
    equips[symbol_.name()] = symbol_;
    EquipPrototype chu_chu;
    chu_chu.set_name("Arcane Symbol: Chu Chu Island");
    chu_chu.set_equip_slot(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND);
    chu_chu.set_required_level(200);
    chu_chu.mutable_arcane_symbol()->set_meso_cost_base(10);
    equips[chu_chu.name()] = chu_chu;
    token_.set_name("Weapon Token");
    token_.set_currency_mark("●");
    token_.set_currency_color(CURRENCY_COLOR_THEME);
    // An ordinary item for a boss to drop, so the clear card has a row that
    // isn't a currency.
    shard_.set_name("Zakum's Soul Shard");
    shard_.set_max_stack(100);
    std::map<std::string, ItemPrototype> items;
    items["weapon_token"] = token_;
    items["zakums_soul_shard"] = shard_;
    std::map<std::string, Scroll> scrolls;
    scrolls["Test Scroll"] = scroll;

    state_ = std::make_unique<GameState>(
        std::move(equips), std::move(scrolls), std::move(items),
        std::map<std::string, Mob>{}, std::map<std::string, MapData>{});
    // Normal Zakum, so the boss screen has a fight. Set up here rather than per
    // test because BossSelectPanel fixes its list at construction, and the
    // fixture builds the panels once.
    Mob arm;
    arm.set_name("Zakum's Arm");
    arm.set_level(110);
    // One HP: these tests are about the fight's screen, not how long it takes.
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

  // A character with no weapon is refused the fight, so every boss test that
  // needs to get past the list equips one first.
  void HoldASword() {
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
    state_->character.Equip(state_->character.inventory().size() - 1);
  }

  // A sword on the character, with the equip panel drawn so its row list is
  // filled. The starting point for every test that opens the equip menu.
  void WearASwordAndDraw() {
    HoldASword();
    RenderEquipPanel();
  }

  // Moves the bag's cursor onto the panel and down to its first row, where
  // every test that opens an item menu wants to be. Focus arrives on the tab
  // bar, where Enter opens the tab's own menu instead.
  void DescendIntoBag() {
    panel_focus_ = kInventoryPanel;
    RenderInventoryPanel();  // builds the row the cursor moves to
    inventory_component_->OnEvent(ftxui::Event::ArrowDown);
  }

  // A ring on the character, in the first free ring slot.
  void WearRing(const EquipPrototype& proto) {
    state_->character.PickUp(std::make_unique<EquipInstance>(proto));
    state_->character.Equip(state_->character.inventory().size() - 1);
  }

  // Moves the arrows onto the Equipped card, where the ring tab bar is. The
  // panel is first told what the screen shows, since it cycles through the
  // cards that are drawn.
  void FocusTheEquippedCard() {
    inspect_panel_.SetItem(controller_->inspect_item());
    inspect_panel_.SetComparison(controller_->comparison_slots());
    controller_->OnEvent(ftxui::Event::Tab);
    ASSERT_EQ(inspect_panel_.focused_card(), InspectPanel::kEquippedCard);
  }

  // A sword in the bag instead, with the cursor on its row.
  void BagASword() {
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
    DescendIntoBag();
  }

  // From an open item menu: onto Scroll, into kScrollSelect, and open the first
  // row's menu.
  void OpenScrollRowMenu() {
    controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
    controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
    controller_->OnEvent(ftxui::Event::Return);     // enter kScrollSelect
    controller_->OnEvent(ftxui::Event::Return);     // open the row's menu
  }

  // Picks and confirms the first row's scroll.
  void ScrollTheFirstRow() {
    OpenScrollRowMenu();
    controller_->OnEvent(ftxui::Event::Return);  // pick Scroll -> confirm
    controller_->OnEvent(ftxui::Event::Return);  // confirm
  }

  // Moves along the bag's tab bar to the Bank tab and opens it, as a player
  // would.
  void OpenBankScreen() {
    panel_focus_ = kInventoryPanel;
    for (int step = 0;
         step < kNumInventoryTabs && inventory_panel_->active_tab() != kBankTab;
         ++step) {
      inventory_component_->OnEvent(ftxui::Event::ArrowRight);
    }
    ASSERT_EQ(inventory_panel_->active_tab(), kBankTab);
    inventory_component_->OnEvent(ftxui::Event::Return);
    ASSERT_EQ(controller_->screen(), kBank);
  }

  // Runs the current fight until it ends, however it ends. The run outlives the
  // fight (it is kept behind whatever panel ended it), so this waits on the
  // screen rather than on in_boss_fight().
  void RunFightToEnd() {
    for (int i = 0; i < 20000 && controller_->screen() == kBossFight; ++i) {
      controller_->AdvanceBossRun(0.1);
    }
  }

  // Starts the only fight in the catalog, so the test begins inside it.
  void EnterFight() {
    HoldASword();
    controller_->OpenMenuEntry(MenuEntry::kBoss);
    controller_->OnEvent(ftxui::Event::Return);
    controller_->OnEvent(ftxui::Event::Return);
  }

  // A Swordman with SP to spend, at the level scrolling unlocks.
  void SeedCharacter() {
    state_->character.AdvanceJob(JOB_SWORDMAN);
    // The starting character is at its advancement with no SP yet. These tests
    // spend SP, so they level far enough to earn some.
    while (state_->character.sp(1) < 60) {
      state_->character.LevelUp();
    }
    // Tests reach the Scroll entry by counting rows, and a locked entry isn't
    // drawn, so the level is set explicitly rather than relying on where the SP
    // loop ends.
    LevelTo(UnlockLevel(Feature::kScrolling));
  }

  // Every panel the controller drives, and the controller itself.
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
    player_inspect_panel_ = std::make_unique<PlayerInspectPanel>(*state_);
    trade_panel_ =
        std::make_unique<TradePanel>(state_->character, state_->account);
    shop_panel_ = std::make_unique<ShopPanel>(state_->character, state_->equips,
                                              state_->items);
    buy_panel_ = std::make_unique<BuyPanel>();
    bank_panel_ = std::make_unique<BankPanel>(state_->character,
                                              state_->account, state_->items);
    link_skill_panel_ =
        std::make_unique<LinkSkillPanel>(state_->character, state_->skills);
    job_inspect_panel_ = std::make_unique<JobInspectPanel>(state_->skills);
    menu_panel_ = std::make_unique<MenuPanel>(*state_, analysis_, panel_focus_);
    keys_ = std::make_unique<KeyMap>(state_->account.mutable_keybinds());
    keybinds_panel_ = std::make_unique<KeybindsPanel>(*keys_);
    options_panel_ = std::make_unique<OptionsPanel>(state_->account);
    jukebox_panel_ = std::make_unique<JukeboxPanel>(*state_, music_director_,
                                                    state_->account);
    controller_ = std::make_unique<TuiController>(
        *state_, Screens{*char_panel_,           *equip_panel_,
                         *inventory_panel_,      *scroll_panel_,
                         inspect_panel_,         preview_inspect_panel_,
                         *star_force_panel_,     cube_panel_,
                         *trace_recover_panel_,  *sell_panel_,
                         *sell_equip_panel_,     *multi_sell_panel_,
                         *map_select_panel_,     *mob_inspect_panel_,
                         *boss_select_panel_,    party_select_panel_,
                         player_list_panel_,     *trade_panel_,
                         *player_inspect_panel_, player_item_panel_,
                         *shop_panel_,           *buy_panel_,
                         *bank_panel_,           *link_skill_panel_,
                         *job_inspect_panel_,    skill_inspect_panel_,
                         buff_info_panel_,       *menu_panel_,
                         *keybinds_panel_,       *options_panel_,
                         *jukebox_panel_},
        analysis_, *keys_, panel_focus_);

    // Build the equip component so RenderEquipPanel() can fill slots_.
    equip_component_ = equip_panel_->MakeComponent([]() {});
    // The inventory component handles tab switching and opens the context menu.
    inventory_component_ = inventory_panel_->MakeComponent(
        [this]() { controller_->OpenInventoryMenu(); },
        [this]() { controller_->ToggleExpanded(kInventoryPanel); });
  }

  // Opens the Keybinds screen the way a player does, from the Settings box. Up
  // moves through the box from the bottom, so Options is the first stop and
  // Keybinds the second.
  void OpenKeybinds() {
    controller_->OpenMenuEntry(MenuEntry::kSettings);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::Return);
  }

  void OpenJukebox() {
    controller_->OpenMenuEntry(MenuEntry::kSettings);
    for (int i = 0; i < 3; ++i) {
      controller_->OnEvent(ftxui::Event::ArrowUp);
    }
    controller_->OnEvent(ftxui::Event::Return);
  }

  void OpenOptions() {
    controller_->OpenMenuEntry(MenuEntry::kSettings);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::Return);
  }

  // Another character on the account, in its own slot.
  void AddCharacter(const std::string& name) {
    CharacterSave slot;
    slot.mutable_character()->set_name(name);
    slot.mutable_character()->set_level(1);
    state_->inactive_characters.push_back(slot);
  }

  // Levels the character to `level`. The item menu's entries are level-gated
  // (see progression.h), so a test that uses one must reach its level first.
  void LevelTo(int level) {
    while (state_->character.proto().level() < level) {
      state_->character.LevelUp();
    }
  }

  // Adds a sellable Etc stack and moves the inventory to the Etc tab.
  void EnterEtcTabWithStack(int count, int sell_price) {
    ItemPrototype shell;
    shell.set_name("Green Snail Shell");
    shell.set_sell_price(sell_price);
    state_->character.AddItem(shell, count);
    panel_focus_ = kInventoryPanel;
    OpenBagTab(kEtcTab);
    inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> stack
  }

  // Moves right along the bag's tab bar until `tab` is open. Tests name the tab
  // rather than counting presses, so adding a tab to the bar doesn't break
  // tests that only pass through it.
  void OpenBagTab(int tab) {
    for (int step = 0;
         step < kNumInventoryTabs && inventory_panel_->active_tab() != tab;
         ++step) {
      inventory_component_->OnEvent(ftxui::Event::ArrowRight);
    }
  }

  // Opens the stack menu and moves to Sell. Inspect is first, so Return alone
  // opens the wrong screen, quietly enough that a nothing-sold check still
  // passes.
  void OpenStackSell() {
    inventory_component_->OnEvent(ftxui::Event::Return);  // the stack menu
    controller_->OnEvent(ftxui::Event::ArrowDown);        // Inspect -> Sell
    controller_->OnEvent(ftxui::Event::Return);
  }

  // Moves along the tab bar to the Shop tab and opens it, leaving the shop
  // screen open with the cursor on the first item.
  void OpenShop() {
    // The Shop tab unlocks at 20. The fixture already levels past that while
    // earning SP, but that is a coincidence the shop tests shouldn't rely on.
    LevelTo(UnlockLevel(Feature::kShop));
    panel_focus_ = kInventoryPanel;
    OpenBagTab(kShopTab);
    inventory_component_->OnEvent(ftxui::Event::Return);
  }

  // Opens the shop on the Buy-Back shelf with the cursor on its first row. Up
  // twice puts the cursor on the tab bar, past the pay row; Down brings it back
  // into the list, since the Buy-Back shelf has no pay row to stop at.
  void OpenBuyBackShelf() {
    OpenShop();
    controller_->OnEvent(ftxui::Event::ArrowUp);
    controller_->OnEvent(ftxui::Event::ArrowUp);
    for (int i = 0; i < kShopBuyBackTab; ++i) {
      controller_->OnEvent(ftxui::Event::ArrowRight);
    }
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }

  // Like OpenBuyDialog, but from the buy-back shelf.
  void OpenBuyBackDialog() {
    OpenBuyBackShelf();
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopMenu, on Inspect
    controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Buy
    controller_->OnEvent(ftxui::Event::Return);     // -> kShopBuy
  }

  // Opens the shop, the first item's menu, and its Buy entry, leaving the buy
  // dialog open with the quantity field focused.
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

  // The buff menu's entries, read the same way.
  std::string RenderBuffMenu() {
    ftxui::Element menu = controller_->buff_menu().Render(0, 0);
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(menu));
    ftxui::Render(screen, menu);
    return ScreenText(screen);
  }

  // The buy dialog's text, read from the screen cell by cell rather than from
  // Screen::ToString, which puts colour escapes in the rows.
  std::string RenderBuyDialog() {
    ftxui::Element dialog = buy_panel_->Render();
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(dialog));
    ftxui::Render(screen, dialog);
    return ScreenText(screen);
  }

  // MapSelectPanel fixes its display order at construction, so the maps must
  // exist first. Rebuild both after changing state_->maps.
  void RebuildMapSelect() {
    map_select_panel_ = std::make_unique<MapSelectPanel>(*state_);
    mob_inspect_panel_ = std::make_unique<MobInspectPanel>(*state_);
    boss_select_panel_ = std::make_unique<BossSelectPanel>(*state_);
    player_inspect_panel_ = std::make_unique<PlayerInspectPanel>(*state_);
    trade_panel_ =
        std::make_unique<TradePanel>(state_->character, state_->account);
    controller_ = std::make_unique<TuiController>(
        *state_, Screens{*char_panel_,           *equip_panel_,
                         *inventory_panel_,      *scroll_panel_,
                         inspect_panel_,         preview_inspect_panel_,
                         *star_force_panel_,     cube_panel_,
                         *trace_recover_panel_,  *sell_panel_,
                         *sell_equip_panel_,     *multi_sell_panel_,
                         *map_select_panel_,     *mob_inspect_panel_,
                         *boss_select_panel_,    party_select_panel_,
                         player_list_panel_,     *trade_panel_,
                         *player_inspect_panel_, player_item_panel_,
                         *shop_panel_,           *buy_panel_,
                         *bank_panel_,           *link_skill_panel_,
                         *job_inspect_panel_,    skill_inspect_panel_,
                         buff_info_panel_,       *menu_panel_,
                         *keybinds_panel_,       *options_panel_,
                         *jukebox_panel_},
        analysis_, *keys_, panel_focus_);
  }

  // Adds a map in the second level band, so paging has somewhere to go. Both
  // maps from LoadTwoMaps are in the first.
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

  // Loads two maps, both in the first level band. Field (level 1 snail) sorts
  // before Cave (level 8 mushroom).
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

  // Renders equip_panel_ to sync its slots_ with character.equipped(). Must be
  // called after any change to the equipped items and before using
  // selected_slot() (directly or through controller_ OnEvent).
  void RenderEquipPanel() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                              ftxui::Dimension::Fixed(5));
    ftxui::Render(scr, equip_component_->Render());
  }

  // Renders inventory_component_ to build the Equip tab's row list, which is
  // filled during rendering. The list must exist before the cursor can move
  // down it.
  void RenderInventoryPanel() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                              ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, inventory_component_->Render());
  }

  // The skill card as text, for checking which rows are on screen.
  std::string RenderSkillCard() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                              ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, skill_inspect_panel_.Render());
    return scr.ToString();
  }

  // The colour of the open cube dialog's border: gold after a reroll that
  // raised the rank, steel blue otherwise.
  ftxui::Color CubeQuestionColor() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                              ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, cube_panel_.RenderConfirm());
    return scr.PixelAt(0, 0).foreground_color;
  }

  // The equip sell dialog as text, for checking what it tells the player.
  std::string RenderSellDialog() {
    ftxui::Screen scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(44),
                                              ftxui::Dimension::Fixed(12));
    ftxui::Render(scr, sell_equip_panel_->Render());
    return scr.ToString();
  }

  // Moves down the open gear menu to `entry`. Counting key presses would only
  // count the entries this item happens to have.
  void WalkGearMenuTo(int entry) {
    for (int i = 0; i < 10 && equip_panel_->menu().selected() != entry; ++i) {
      controller_->OnEvent(ftxui::Event::ArrowDown);
    }
    ASSERT_EQ(equip_panel_->menu().selected(), entry);
  }

  // Picks up sword_ with every upgrade slot used, which star force needs.
  void PickUpScrolledSword() {
    sword_.set_upgrade_slots(0);
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  }

  // Replaces the scroll map with a single 0% scroll and rebuilds scroll_panel_
  // and controller_ to use it.
  void UseFailScroll() {
    Scroll fail;
    fail.set_name("Fail Scroll");
    // GMS has no 0% scroll, so no price band covers it and the price set here
    // is what the player pays. That makes a guaranteed failure affordable to
    // test, since every real rate sometimes succeeds.
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
        *state_, Screens{*char_panel_,           *equip_panel_,
                         *inventory_panel_,      *scroll_panel_,
                         inspect_panel_,         preview_inspect_panel_,
                         *star_force_panel_,     cube_panel_,
                         *trace_recover_panel_,  *sell_panel_,
                         *sell_equip_panel_,     *multi_sell_panel_,
                         *map_select_panel_,     *mob_inspect_panel_,
                         *boss_select_panel_,    party_select_panel_,
                         player_list_panel_,     *trade_panel_,
                         *player_inspect_panel_, player_item_panel_,
                         *shop_panel_,           *buy_panel_,
                         *bank_panel_,           *link_skill_panel_,
                         *job_inspect_panel_,    skill_inspect_panel_,
                         buff_info_panel_,       *menu_panel_,
                         *keybinds_panel_,       *options_panel_,
                         *jukebox_panel_},
        analysis_, *keys_, panel_focus_);
  }

  int panel_focus_ = kEquipPanel;
  // Scrolling costs spell traces, so a test that scrolls adds some first. Not
  // in SetUp, because that would put a stack first on the Etc tab and shift
  // every index the sell tests use.
  void GiveTraces(int count) {
    ItemPrototype trace;
    trace.set_name(kSpellTraceName);
    trace.set_kind(ITEM_KIND_SPELL_TRACE);
    state_->character.AddItem(trace, count);
  }

  EquipPrototype sword_;
  EquipPrototype symbol_;
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
  // Not a pointer: it is cheap to build, and there is no connection in these
  // tests for it to show.
  PartySelectPanel party_select_panel_;
  PlayerListPanel player_list_panel_;
  std::unique_ptr<PlayerInspectPanel> player_inspect_panel_;
  std::unique_ptr<TradePanel> trade_panel_;
  std::unique_ptr<ShopPanel> shop_panel_;
  std::unique_ptr<BuyPanel> buy_panel_;
  std::unique_ptr<BankPanel> bank_panel_;
  std::unique_ptr<LinkSkillPanel> link_skill_panel_;
  std::unique_ptr<JobInspectPanel> job_inspect_panel_;
  SkillInspectPanel skill_inspect_panel_;
  BuffInfoPanel buff_info_panel_;
  InspectPanel inspect_panel_;
  InspectPanel preview_inspect_panel_;
  InspectPanel player_item_panel_;
  BattleAnalysis analysis_;
  std::unique_ptr<MenuPanel> menu_panel_;
  std::unique_ptr<KeyMap> keys_;
  std::unique_ptr<KeybindsPanel> keybinds_panel_;
  std::unique_ptr<OptionsPanel> options_panel_;
  MusicPlayer music_player_{MusicPlayer::Backend::kNull};
  std::mt19937 music_rng_{1};
  MusicDirector music_director_{music_player_, music_rng_};
  std::unique_ptr<JukeboxPanel> jukebox_panel_;
  std::unique_ptr<TuiController> controller_;
  ftxui::Component equip_component_;
  ftxui::Component inventory_component_;
};

// --- Tab ---

// Focus starts on the equipped panel, and Tab goes clockwise: equipped ->
// inventory -> menu -> combat -> character -> equipped.

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

// The same ring in reverse, wrapping the same way.
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

// A gold tab means "there is something new here". Tabbing to a panel where that
// tab is already open counts as reading it, so the gold must clear. Otherwise
// it could only be cleared by moving off the tab and back.
TEST_F(TuiControllerTest, ArrivingOnAPanelReadsTheTabLeftOpenOnIt) {
  int stage = state_->character.proto().job_stage();
  ASSERT_FALSE(state_->account.Seen(EquipGiftTabKey(stage)));

  controller_->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(panel_focus_, kInventoryPanel);
  EXPECT_TRUE(state_->account.Seen(EquipGiftTabKey(stage)));
}

// That is its purpose: undoing a Tab that went one panel too far.
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

// The Bishop's toggle, placed in the Swordman's book so the fixture's character
// can learn it.
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

// An ordinary skill has nothing to toggle, so Down from Inspect goes to Close
// rather than to an entry that does nothing.
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

  // The label is reversed the second time.
  controller_->OpenSkillMenu(toggle);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(state_->character.SkillToggledOn("Righteously Indignant"));
}

// An unlearned toggle still lists its switch, greyed out, since a missing entry
// would be more surprising, and the cursor skips it.
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

// The level is read live, not stored when the screen opened, so a point spent
// between two views shows on the second.
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

// The arrows scroll the card rather than leaving the screen, and each card
// opens at the top however far the last one was scrolled.
TEST_F(TuiControllerTest, AFreshSkillCardStartsAtTheTop) {
  Skill skill = SlashBlast();
  ASSERT_TRUE(state_->character.LearnSkill(skill, 3));
  controller_->OpenSkillInspect(skill);
  skill_inspect_panel_.SetSkill(&skill, 3, 0);
  // Small enough that there is room to scroll.
  skill_inspect_panel_.SetMaxRows(6);

  std::string head = RenderSkillCard();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->screen(), kSkillInspect) << "scrolled, not closed";
  EXPECT_NE(RenderSkillCard(), head);

  controller_->OpenSkillInspect(skill);
  EXPECT_EQ(RenderSkillCard(), head);
}

// The screen is read-only, so Enter also leaves rather than doing nothing.
TEST_F(TuiControllerTest, EnterAlsoLeavesTheSkillInspectScreen) {
  controller_->OpenSkillInspect(SlashBlast());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// --- Job advancement ---

// --- the Buffs tab's menu, card and question ---

// The switch is the menu's first entry, labelled with the state it would put
// the buff in. Pressing it asks nothing, since nothing is spent until the buff
// is used.
TEST_F(TuiControllerTest, TheFirstEntryThrowsTheSwitchAndSaysWhichWay) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_NE(RenderBuffMenu().find("Enable"), std::string::npos);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.ConsumableActive(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));

  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_NE(RenderBuffMenu().find("Disable"), std::string::npos);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(state_->character.ConsumableActive(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
}

TEST_F(TuiControllerTest, TheBuffMenuOpensOnInspectAndReadsTheCard) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_EQ(controller_->screen(), kBuffMenu);
  EXPECT_EQ(controller_->buff_type(),
            CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Enable -> Inspect
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBuffInfo);
  // Read-only, and Back returns to the menu it was opened from.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBuffInfo);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBuffMenu);
}

TEST_F(TuiControllerTest, BuyPermBuysTheBuffOutright) {
  LevelTo(kConsumableUnlockLevel);
  state_->character.AddMeso(200'000'000);
  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Enable -> Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Buy Perm
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBuffBuy);
  EXPECT_EQ(controller_->buff_buy_price(), 100'000'000);
  EXPECT_TRUE(controller_->buff_buy_affordable());

  // It starts on Cancel, so buying is Left then Enter.
  const int64_t before = state_->character.proto().meso();
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.ConsumableOwned(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(state_->character.proto().meso(), before - 100'000'000);
}

// The dialog still opens when the character can't afford the buff, so they can
// see the price, but [Confirm] does nothing.
TEST_F(TuiControllerTest, AShortPurseOpensTheQuestionAndRefusesTheAnswer) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenBuffBuy(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  ASSERT_EQ(controller_->screen(), kBuffBuy);
  EXPECT_FALSE(controller_->buff_buy_affordable());
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBuffBuy);
  EXPECT_FALSE(state_->character.ConsumableOwned(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// A buff already bought keeps its entry, greyed out, and the cursor skips it.
TEST_F(TuiControllerTest, AnOwnedBuffHasNothingLeftToBuy) {
  LevelTo(kConsumableUnlockLevel);
  state_->character.AddMeso(200'000'000);
  ASSERT_TRUE(state_->character.BuyConsumable(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Enable -> Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Close, stepping
  controller_->OnEvent(ftxui::Event::Return);     // over the greyed Buy Perm
  EXPECT_EQ(controller_->screen(), kMain);
}

TEST_F(TuiControllerTest, CloseAndEscapeBothLeaveTheBuffMenu) {
  LevelTo(kConsumableUnlockLevel);
  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::ArrowUp);  // Enable -> Close, wrapping
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);

  controller_->OpenBuffMenu(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Enter on a job opens a menu rather than going straight to the confirmation,
// so a job can be read before it is chosen.
TEST_F(TuiControllerTest, OpenJobMenuFloatsTheMenuAndNotTheConfirmation) {
  controller_->OpenJobMenu(JOB_ROGUE);
  EXPECT_EQ(controller_->screen(), kJobMenu);
  EXPECT_EQ(controller_->job_advance_job(), JOB_ROGUE);
}

// --- the preset menu ---

TEST_F(TuiControllerTest, TheMenuPutsAPresetInUse) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenPresetMenu(PresetKind::kHyperStats, StatPreset::kThird);
  ASSERT_EQ(controller_->screen(), kPresetMenu);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.SlotInUse(PresetKind::kHyperStats),
            StatPreset::kThird);
  // The other kind keeps its own choice.
  EXPECT_EQ(state_->character.SlotInUse(PresetKind::kInnerAbility),
            StatPreset::kFirst);
}

// Use is dimmed where it would do nothing: on the preset already in use, and on
// all of them while autoswap is choosing. The menu skips a dimmed entry, so
// Enter lands on Move.
TEST_F(TuiControllerTest, UseIsDimmedWhereItWouldDoNothing) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->OpenPresetMenu(PresetKind::kHyperStats, StatPreset::kFirst);
  EXPECT_EQ(controller_->preset_menu().selected(), kPresetMenuMove);

  state_->account.SetAutoswapPresets(true);
  state_->MirrorAccount();
  controller_->OpenPresetMenu(PresetKind::kHyperStats, StatPreset::kThird);
  EXPECT_EQ(controller_->preset_menu().selected(), kPresetMenuMove);
}

TEST_F(TuiControllerTest, MoveSwapsThePresetsAndWhatIsInUseWithThem) {
  LevelTo(kHyperStatUnlockLevel);
  ASSERT_TRUE(state_->character.AllocateHyperStat(HYPER_STAT_FIELD_STR,
                                                  StatPreset::kFirst, 1));
  state_->character.SetSlotInUse(PresetKind::kHyperStats, StatPreset::kFirst);

  controller_->OpenPresetMenu(PresetKind::kHyperStats, StatPreset::kFirst);
  // Use is dimmed on the preset already in use, so the menu opens on Move.
  ASSERT_EQ(controller_->preset_menu().selected(), kPresetMenuMove);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kPresetMove);
  // It starts on the preset the menu named, so a swap needs a step first.
  EXPECT_EQ(controller_->preset_move_row(), 0);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kThird),
            1);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kFirst),
            0);
  // The in-use marker followed its contents: moving presets doesn't change
  // which one is being played.
  EXPECT_EQ(state_->character.SlotInUse(PresetKind::kHyperStats),
            StatPreset::kThird);
}

TEST_F(TuiControllerTest, CancelAndEscapeBothLeaveTheMoveAlone) {
  LevelTo(kHyperStatUnlockLevel);
  ASSERT_TRUE(state_->character.AllocateHyperStat(HYPER_STAT_FIELD_STR,
                                                  StatPreset::kFirst, 1));
  controller_->OpenPresetMenu(PresetKind::kHyperStats, StatPreset::kFirst);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kPresetMove);
  // Cancel comes after the last preset.
  for (int i = 0; i < kNumStatPresets; ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  EXPECT_EQ(controller_->preset_move_row(), kNumStatPresets);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kFirst),
            1);

  controller_->OpenPresetMenu(PresetKind::kHyperStats, StatPreset::kFirst);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kFirst),
            1);
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
  // It is still the same confirmation, still starting on Cancel.
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

// The menu starts at the top each time, so the cursor never starts where the
// last visit left it.
TEST_F(TuiControllerTest, TheJobMenuOpensAtInspectEveryTime) {
  controller_->OpenJobMenu(JOB_ROGUE);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OpenJobMenu(JOB_ARCHER);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kJobInspect);
}

// Read-only: the arrows scroll the book, and Back returns to the menu it was
// opened from, where the advancement is one key press away.
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

// The prompt starts on Cancel, so a stray Enter backs out rather than picking a
// job that can't be undone.
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
  // gives the gear and leaves the stats alone. A reset would set the new
  // primary to 25.
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

// The learn screen counts from the pool the skill is bought from. A Hyper Skill
// uses the Hyper pool; if the screen used the job stage's pool, it would open
// with nothing to spend.
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

// The arrows scroll the card rather than closing it. Only Confirm and Cancel
// leave.
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

// A bag item is compared against what the player already wears in the slot it
// would go in.
TEST_F(TuiControllerTest, ABagItemIsComparedWithTheOneItWouldReplace) {
  HoldASword();
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);

  const EquipTabItem* worn = controller_->inspect_comparison();
  ASSERT_NE(worn, nullptr);
  EXPECT_EQ(worn, state_->character.WornAt(StatPreset::kFirst,
                                           EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_NE(worn, controller_->inspect_item()) << "not the item itself";
}

// Nothing in the slot, so nothing to compare: the card would be empty.
TEST_F(TuiControllerTest, ABagItemWithAnEmptySlotIsComparedWithNothing) {
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  EXPECT_EQ(controller_->inspect_comparison(), nullptr);
}

// An item inspected on the character is itself the comparison, so there is
// neither a card nor a number: it would be compared with itself.
TEST_F(TuiControllerTest, AWornItemIsComparedWithNothing) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  EXPECT_EQ(controller_->inspect_comparison(), nullptr);
  EXPECT_FALSE(controller_->inspect_delta().has_value());
}

// The number on a bag item is what equipping it would do: a gain over an empty
// slot.
TEST_F(TuiControllerTest, ABagItemCarriesWhatWearingItWouldDoToCombatPower) {
  EquipPrototype better = sword_;
  better.set_name("Better Sword");
  better.mutable_base_stats()->set_attack(50);
  state_->character.PickUp(std::make_unique<EquipInstance>(better));
  DescendIntoBag();
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  EXPECT_GT(*controller_->inspect_delta(), 0);
}

// And a loss against something better, which is what the player most needs to
// know before equipping it.
TEST_F(TuiControllerTest, ABagItemWorseThanTheWornOneReadsNegative) {
  EquipPrototype better = sword_;
  better.set_name("Better Sword");
  better.mutable_base_stats()->set_attack(50);
  state_->character.PickUp(std::make_unique<EquipInstance>(better));
  ASSERT_TRUE(state_->character.Equip(0));
  BagASword();

  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  EXPECT_LT(*controller_->inspect_delta(), 0);
}

// Both use the Gear tab the player is on, which is where Equip would put the
// item. A card and number about gear the item wouldn't replace are worse than
// none.
TEST_F(TuiControllerTest, TheComparisonAndTheFigureFollowTheGearTab) {
  LevelTo(UnlockLevel(Feature::kEquipPresets));
  EquipPrototype better = sword_;
  better.set_name("Better Sword");
  better.mutable_base_stats()->set_attack(50);
  state_->character.PickUp(std::make_unique<EquipInstance>(better));
  ASSERT_TRUE(state_->character.Equip(0, StatPreset::kFirst));

  panel_focus_ = kEquipPanel;
  RenderEquipPanel();
  equip_component_->OnEvent(ftxui::Event::ArrowUp);  // onto the preset row
  equip_component_->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(controller_->ComparisonPreset(), StatPreset::kSecond);

  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  DescendIntoBag();
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  // The second preset inherits the first's sword, so the bare one is compared
  // against it, and losing it is a loss.
  EXPECT_EQ(
      controller_->inspect_comparison(),
      state_->character.WornAt(StatPreset::kSecond, EQUIP_SLOT_PRIMARY_WEAPON));
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  EXPECT_LT(*controller_->inspect_delta(), 0);
}

// A second copy of a worn ring is compared against the worn one, since Equip
// would swap it for that one, so the card and number describe that swap.
TEST_F(TuiControllerTest, ASecondCopyOfAWornRingIsComparedWithIt) {
  HoldASword();
  EquipPrototype ring;
  ring.set_name("Silver Blossom Ring");
  ring.set_equip_slot(EQUIP_SLOT_RING);
  ring.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  state_->character.PickUp(std::make_unique<EquipInstance>(ring));
  ASSERT_TRUE(state_->character.Equip(0));
  EquipPrototype better = ring;
  better.mutable_base_stats()->set_attack(50);
  state_->character.PickUp(std::make_unique<EquipInstance>(better));

  DescendIntoBag();
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  EXPECT_EQ(controller_->inspect_comparison(),
            state_->character.WornAt(StatPreset::kFirst, EQUIP_SLOT_RING));
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  EXPECT_GT(*controller_->inspect_delta(), 0);
}

// A ring worth `attack`, which every job can wear.
EquipPrototype RingWorth(const std::string& name, int attack) {
  EquipPrototype ring;
  ring.set_name(name);
  ring.set_equip_slot(EQUIP_SLOT_RING);
  ring.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  ring.mutable_base_stats()->set_attack(attack);
  return ring;
}

// A ring's Equipped card has a tab bar for the four slots, and the figure
// follows it: lower against a good ring than an empty slot.
TEST_F(TuiControllerTest, TheRingBarWalksTheFourSlotsAndTheFigureFollows) {
  WearRing(RingWorth("Plain Ring", 5));
  WearRing(RingWorth("Good Ring", 40));
  state_->character.PickUp(
      std::make_unique<EquipInstance>(RingWorth("New Ring", 20)));

  DescendIntoBag();
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);

  // It opens where Equip would put it: the first free slot, which is empty.
  InspectPanel::ComparisonSlots slots = controller_->comparison_slots();
  ASSERT_EQ(slots.worn.size(), 4u);
  EXPECT_EQ(slots.active, 2);
  EXPECT_EQ(controller_->inspect_comparison(), nullptr);
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  const int into_empty = *controller_->inspect_delta();
  EXPECT_GT(into_empty, 0);

  FocusTheEquippedCard();
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(controller_->comparison_slots().active, 1);
  EXPECT_EQ(controller_->inspect_comparison(),
            state_->character.WornAt(StatPreset::kFirst, EQUIP_SLOT_RING_2));
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  const int over_good = *controller_->inspect_delta();
  EXPECT_LT(over_good, 0) << "the Good Ring is the better of the two";

  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(controller_->comparison_slots().active, 0);
  ASSERT_TRUE(controller_->inspect_delta().has_value());
  EXPECT_GT(*controller_->inspect_delta(), over_good)
      << "the Plain Ring costs less to give up";
  EXPECT_LT(*controller_->inspect_delta(), into_empty);

  // A ring, so the tab bar wraps like every tab bar in the game.
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(controller_->comparison_slots().active, 3);
}

// Only a ring has the tab bar. An item with one slot has nothing to move
// through, and the arrows scroll the card as usual.
TEST_F(TuiControllerTest, AnItemWithOneSlotHasNoBarToWalk) {
  HoldASword();  // something for the Equipped card to hold
  BagASword();
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  ASSERT_EQ(controller_->comparison_slots().worn.size(), 1u);

  FocusTheEquippedCard();
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(controller_->comparison_slots().active, 0);
  EXPECT_EQ(controller_->screen(), kInspect);
}

// The chosen slot doesn't carry over to the next item: every screen opens on
// the slot Equip would fill.
TEST_F(TuiControllerTest, OpeningAnInspectScreenForgetsTheSlot) {
  WearRing(RingWorth("Plain Ring", 5));
  state_->character.PickUp(
      std::make_unique<EquipInstance>(RingWorth("New Ring", 20)));

  DescendIntoBag();
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  FocusTheEquippedCard();
  controller_->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(controller_->comparison_slots().active, 2);

  controller_->OnEvent(ftxui::Event::Escape);
  controller_->OpenInventoryMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kInspect);
  EXPECT_EQ(controller_->comparison_slots().active, 1)
      << "the first free slot, which is where Equip would put it";
}

// Left and Right scroll a narrow card, and like the other arrows they don't
// close the screen.
TEST_F(TuiControllerTest, SidewaysArrowsInInspectDoNotLeave) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::ArrowRight);

  EXPECT_EQ(controller_->screen(), kInspect);
}

// A screen opens with the left half taking the arrows, whatever the last one
// was left on.
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

// The scroll list keeps the arrows until Tab moves them to the card, and Tab
// moves them back.
TEST_F(TuiControllerTest, TabOnTheScrollScreenMovesToTheCard) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kScrollSelect);
  ASSERT_FALSE(controller_->right_card_focused());

  // Short enough that the card has something to scroll, and drawn once so it
  // knows its size. Tui does this every frame; nothing renders in a controller
  // test.
  inspect_panel_.SetItem(controller_->scroll_item());
  inspect_panel_.SetMaxRows(4);
  inspect_panel_.RenderItemOnly();
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(controller_->right_card_focused());
  EXPECT_EQ(controller_->screen(), kScrollSelect);

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_FALSE(controller_->right_card_focused());
}

// A card that fits is still a Tab stop, and Shift+Tab cycles the same two in
// reverse.
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

// Unequipping leaves focus where it is: an empty list no longer pushes focus to
// the other panel.
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

TEST_F(TuiControllerTest, ScrollingTheWornSwordShowsTheResult) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  ScrollTheFirstRow();

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                ->equip_state()
                .scroll_stats()
                .attack(),
            5);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
  // sword_ has 3 upgrade slots; one was used.
  EXPECT_EQ(controller_->scroll_result().slots_remaining, 2);
}

TEST_F(TuiControllerTest, NoSlotsShowsTheNoSlotsOutcome) {
  sword_.set_upgrade_slots(0);
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  OpenScrollRowMenu();
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll, which has no slot

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

TEST_F(TuiControllerTest, StarForceOpensOnceTheSlotsAreSpent) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  sword_.set_upgrade_slots(1);
  WearASwordAndDraw();
  GiveTraces(100);

  // Open the menu while the item still has 1 slot, so Star Force should be
  // disabled.
  controller_->OpenEquipMenu();
  ScrollTheFirstRow();                         // -> kScrollResult, 0 slots
  controller_->OnEvent(ftxui::Event::Escape);  // → kScrollSelect
  controller_->OnEvent(ftxui::Event::Escape);  // → kItemMenu (re-opens menu)

  EXPECT_EQ(controller_->screen(), kItemMenu);
  // Move to Star Force's position; it must be reachable, not skipped.
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Scroll
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Star Force
  EXPECT_EQ(equip_panel_->menu().selected(), kGearMenuStarForce);
}

TEST_F(TuiControllerTest, EnterOnAResultReturnsToSelect) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  ScrollTheFirstRow();
  controller_->OnEvent(ftxui::Event::Return);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

TEST_F(TuiControllerTest, EscapeInScrollResultGoesToScrollSelect) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  ScrollTheFirstRow();
  controller_->OnEvent(ftxui::Event::Escape);  // dismiss result

  EXPECT_EQ(controller_->screen(), kScrollSelect);
}

// A scroll is paid for, so the traces must leave the bag. The price depends on
// the sword, not the scroll: a level 60 weapon at 100% costs 5 traces in GMS's
// table.
TEST_F(TuiControllerTest, ScrollingSpendsItsTraces) {
  LevelTo(60);
  sword_.set_required_level(60);
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  ScrollTheFirstRow();

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(state_->character.CountItem(kSpellTraceName), 95);
}

// Pin is the second entry of the row's menu, and the pin is stored on the
// character rather than the screen, so it is still there next time.
TEST_F(TuiControllerTest, PinningFromTheMenuMarksTheCharacter) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  OpenScrollRowMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // onto Pin
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_TRUE(scroll_panel_->SelectedIsPinned());
  EXPECT_TRUE(
      state_->character.ScrollPinned(scroll_panel_->PinKeyOfSelected()));
  EXPECT_EQ(controller_->screen(), kScrollSelect) << "still on the list";

  // The same entry removes it.
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(scroll_panel_->SelectedIsPinned());
}

// Close leaves the menu without changing anything.
TEST_F(TuiControllerTest, CloseLeavesTheMenuWithoutScrolling) {
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  OpenScrollRowMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Pin
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Close
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(scroll_panel_->IsMenuOpen());
  EXPECT_FALSE(scroll_panel_->IsConfirming());
  EXPECT_FALSE(scroll_panel_->SelectedIsPinned());
  EXPECT_EQ(controller_->screen(), kScrollSelect);
  EXPECT_EQ(state_->character.CountItem(kSpellTraceName), 100);
}

// Escape closes the menu, not the screen behind it. Otherwise one key would
// back out of both at once.
TEST_F(TuiControllerTest, EscapeClosesTheRowMenuAndStaysOnTheList) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  OpenScrollRowMenu();
  ASSERT_TRUE(scroll_panel_->IsMenuOpen());

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(scroll_panel_->IsMenuOpen());
  EXPECT_EQ(controller_->screen(), kScrollSelect);

  // A second Escape does leave, now that nothing is over the list.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kItemMenu);
}

// A failed roll still uses the scroll: the traces pay for the attempt, not the
// result.
TEST_F(TuiControllerTest, AFailedScrollStillCosts) {
  UseFailScroll();
  WearASwordAndDraw();
  GiveTraces(100);

  controller_->OpenEquipMenu();
  ScrollTheFirstRow();

  EXPECT_EQ(controller_->scroll_result().outcome, kScrollFail);
  EXPECT_EQ(state_->character.CountItem(kSpellTraceName), 95);
}

// With too few traces, Enter on the confirm window does nothing: no scroll, no
// cost, and the window stays up rather than moving the player elsewhere.
TEST_F(TuiControllerTest, ScrollingWithoutTheTracesIsRefused) {
  LevelTo(60);
  sword_.set_required_level(60);
  WearASwordAndDraw();
  GiveTraces(4);  // one short of the 5 a level 60 weapon costs at 100%

  controller_->OpenEquipMenu();
  ScrollTheFirstRow();
  ASSERT_TRUE(scroll_panel_->IsConfirming());

  EXPECT_EQ(controller_->screen(), kScrollSelect);
  EXPECT_EQ(state_->character.CountItem(kSpellTraceName), 4);
  EXPECT_EQ(state_->character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                ->equip_state()
                .scroll_stats()
                .attack(),
            0);
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
  ScrollTheFirstRow();

  EXPECT_EQ(controller_->screen(), kScrollResult);
  const EquipInstance* item = state_->character.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->equip_state().scroll_stats().attack(), 5);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollSuccess);
  EXPECT_EQ(controller_->scroll_result().equip_name, "Sword");
  EXPECT_EQ(controller_->scroll_result().scroll_name, "Test Scroll");
}

TEST_F(TuiControllerTest, BagScrollWithNoSlotsShowsThatOutcome) {
  sword_.set_upgrade_slots(0);
  BagASword();

  controller_->OpenInventoryMenu();
  OpenScrollRowMenu();
  controller_->OnEvent(ftxui::Event::Return);  // pick Scroll, which has no slot

  EXPECT_EQ(controller_->screen(), kScrollResult);
  EXPECT_EQ(controller_->scroll_result().outcome, kScrollNoSlots);
}

// --- the Golden Hammer ---

// The whole flow: the entry opens the confirmation, confirming pays the price,
// and the new slot is there afterwards.
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
      *state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_EQ(worn.equip_state().hammers(), 1);
  EXPECT_EQ(worn.equip_state().remaining_upgrade_slots(),
            sword_.upgrade_slots() + 1);
  EXPECT_EQ(state_->character.meso(), 0);
}

// If the character can't afford it, the price is red and the button greyed, and
// the dialog stays open rather than closing as if something happened.
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
                ->equip_state()
                .hammers(),
            0);
}

// The same flow from the bag, which covers the other half of ItemRef.
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

// A hammer adds a slot, and star force waits until an item has no slots left.
// So the Star Force entry is dimmed until the new slot is used.
TEST_F(TuiControllerTest, AHammerHoldsTheStarsUntilItsSlotIsSpent) {
  LevelTo(UnlockLevel(Feature::kHammer));
  state_->character.AddMeso(kGoldenHammerCost);
  // Every slot used, so star force is available: PickUpScrolledSword's weapon
  // has no slots at all, so a hammer has nothing to add to.
  Equip spent;
  spent.set_equip_name(sword_.name());
  spent.set_scroll_successes(sword_.upgrade_slots());
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_, spent));
  state_->character.Equip(0);
  RenderEquipPanel();
  const EquipInstance& worn =
      *state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  ASSERT_TRUE(worn.CanStarForce());

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_FALSE(state_->character.equipped()
                   .at(EQUIP_SLOT_PRIMARY_WEAPON)
                   ->CanStarForce());
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

// There is a button as well as a key: the screen's own [Cancel] closes it, so a
// player who doesn't know Escape isn't stuck.
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
  // At 0 stars the success rate is 95%, so with a seeded rng the first attempt
  // succeeds. This only checks the screen change, not the outcome.
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

  // If the item wasn't destroyed, dismissing the result returns to kStarForce.
  if (controller_->star_force_result().outcome != kStarForceDestroy) {
    controller_->OnEvent(ftxui::Event::Return);
    EXPECT_EQ(controller_->screen(), kStarForce);
  }
}

// Two cards, one on each side of the panel: Tab picks which the arrows scroll,
// and Left and Right stay with the button row.
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

// An item at its maximum stars has no "after" card, so there is no second card
// for Tab and no attempt for Enter. The screen is reached by starring an item
// to its limit, since the bag greys out the entry for one already there.
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
  // attempts run out of stars, not out of item.
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
            state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Confirm buys a roll and stays on the screen: the player presses again after
// seeing the new lines.
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
                ->potential()
                .rank(),
            POTENTIAL_RANK_RARE);
}

// A cube that raises the rank lights the confirmation gold, and the next key
// clears it. It rerolls until a rank-up happens.
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
      *state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
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

// The accessors return the item from whichever kind of ref the modal was opened
// from. The controller decides that when the item is picked, so these check
// that both kinds return the right item.
TEST_F(TuiControllerTest, EquipInspectGoesToInspectOnTheWornItem) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kInspect);
  ASSERT_NE(controller_->inspect_item(), nullptr);
  EXPECT_EQ(controller_->inspect_item(),
            state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
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
            state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

// Moving focus after opening the modal must not change which item it is about.
// The item is fixed when the modal opens, not re-read from panel_focus_.
TEST_F(TuiControllerTest, MovingFocusDoesNotRepointAnOpenModal) {
  WearASwordAndDraw();

  controller_->OpenEquipMenu();
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect
  controller_->OnEvent(ftxui::Event::Return);
  panel_focus_ = kInventoryPanel;

  EXPECT_EQ(controller_->inspect_item(),
            state_->character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
}

// --- Sell via Etc tab ---

// --- Sell via bag panel ---

// Sell is third from last (above Multi-Sell and Close), so three steps up the
// wrapping menu reach it from wherever it opened. Counting down would land
// elsewhere whenever a locked entry is missing.
void StepToSell(TuiController& controller) {
  for (int i = 0; i < 3; ++i) {
    controller.OnEvent(ftxui::Event::ArrowUp);
  }
}

// Multi-Sell is directly below Sell, so it is one step closer to the bottom.
void StepToMultiSell(TuiController& controller) {
  controller.OnEvent(ftxui::Event::ArrowUp);
  controller.OnEvent(ftxui::Event::ArrowUp);
}

// The whole trace recovery flow from the bag, which nothing else tests: the
// menu entry, the confirmation, and the [Continue] that closes the result.
TEST_F(TuiControllerTest, RecoveringATraceEndsOnAResultThatCloses) {
  Equip destroyed;
  destroyed.set_equip_name(sword_.name());
  destroyed.set_scroll_successes(2);
  destroyed.mutable_scroll_stats()->set_attack(5);
  state_->character.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  BagASword();

  // A trace has only Inspect and Recover, so the test moves to the entry rather
  // than counting.
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

// Two cards and a tab row: Tab picks which card the arrows scroll, and the tabs
// keep Left and Right.
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

// Moving off [Confirm] backs out. The dialog opens on the way through, since
// the shop's buy-back shelf can undo a mistaken sale.
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

// A trace sells for nothing however valuable the item was, and is still
// removed. This is the only way a trace leaves the bag.
TEST_F(TuiControllerTest, BagSellThrowsATraceAwayForNothing) {
  sword_.set_sell_price(900);
  state_->character.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  DescendIntoBag();

  controller_->OpenInventoryMenu();
  StepToSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);
  // The dialog must say so too. What it shows and what the sale pays are
  // computed separately, so one can be wrong while the other is right.
  EXPECT_NE(RenderSellDialog().find("Sell for \U0001FA99 0"),
            std::string::npos);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.inventory().size(), 0);
  EXPECT_EQ(state_->character.meso(), 0);
}

// The dialog opens on the row under the cursor, not the first row.
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

// The Etc tab's menu leads to the same screen, opened on that tab.
TEST_F(TuiControllerTest, MultiSellOpensFromAStackToo) {
  ItemPrototype shell;
  shell.set_name("Green Snail Shell");
  shell.set_sell_price(50);
  state_->character.AddItem(shell, 4);
  panel_focus_ = kInventoryPanel;
  RenderInventoryPanel();

  OpenBagTab(kEtcTab);
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // onto the stack
  controller_->OpenInventoryMenu();
  StepToMultiSell(*controller_);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMultiSell);
  EXPECT_EQ(controller_->multi_sell_basket().etc, std::set<int>({0}));
}

// --- shop ---

// The Shop tab has no list to move into, so Enter opens it directly from the
// tab bar.
TEST_F(TuiControllerTest, EnterOnTheShopTabOpensTheShop) {
  panel_focus_ = kInventoryPanel;
  OpenBagTab(kShopTab);
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

// The card compares the shop item with what the character wears in that slot.
TEST_F(TuiControllerTest, ShopMenuInspectOpensTheInspectScreen) {
  HoldASword();
  OpenShop();
  controller_->OnEvent(ftxui::Event::Return);  // -> kShopMenu, on Inspect
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kShopInspect);
  InspectPanel::ComparisonSlots slots = controller_->comparison_slots();
  ASSERT_EQ(slots.worn.size(), 1u);
  EXPECT_EQ(slots.worn[0], state_->character.WornAt(StatPreset::kFirst,
                                                    EQUIP_SLOT_PRIMARY_WEAPON));
}

// Inspecting is how a player decides whether to buy, so leaving the inspect
// screen returns to the list rather than the bag.
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

// Escape from the menu returns to the list as it was, so the next Escape leaves
// the shop.
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
  // Back to the shop rather than the bag: a player buying one thing often buys
  // another.
  EXPECT_EQ(controller_->screen(), kShop);
}

// The dialog reads the owned count when it opens. A panel test can't catch
// this, since BuyPanel is handed the number.
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

// The token shelf charges tokens and leaves meso alone: the same dialog,
// counting a different balance.
TEST_F(TuiControllerTest, BuyingWithATokenSpendsTheTokenAndNotTheMeso) {
  state_->character.AddMeso(25000);
  state_->character.AddItem(token_, 2);
  OpenTokenBuyDialog();
  std::string dialog = RenderBuyDialog();
  ASSERT_NE(dialog.find("Frozen Sword"), std::string::npos);
  EXPECT_NE(dialog.find("● 1"), std::string::npos) << "priced in its mark";

  controller_->OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.meso(), 25000);
  EXPECT_EQ(state_->character.CountItem(token_), 1);
  ASSERT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.inventory()[0].name(), "Frozen Sword");
}

// The tokens held limit the quantity field, as meso does: a larger number can't
// be typed, so the shop never gets an order it would refuse.
TEST_F(TuiControllerTest, TheTokenDialogCapsTheOrderAtTheTokensHeld) {
  state_->character.AddItem(token_, 1);
  OpenTokenBuyDialog();
  controller_->OnEvent(ftxui::Event::Backspace);
  controller_->OnEvent(ftxui::Event::Character('2'));
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.CountItem(token_), 0);
  EXPECT_EQ(state_->character.inventory().size(), 1)
      << "one, not the two typed";
}

// Meso doesn't help on this shelf: with no tokens, the dialog opens at a
// quantity of zero and Confirm does nothing.
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

// The dialog opens on an unaffordable item instead of refusing, and shows it by
// making Confirm do nothing.
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
  EXPECT_TRUE(state_->character.stackables().empty());
  EXPECT_EQ(state_->character.meso(), 20);  // 10 * 2
}

TEST_F(TuiControllerTest, SellEscapeCancelsWithoutSelling) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  OpenStackSell();
  controller_->OnEvent(ftxui::Event::Escape);  // cancel

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.stackables()[0].count(), 10);
  EXPECT_EQ(state_->character.meso(), 0);
}

TEST_F(TuiControllerTest, SellConfirmSellsTypedQuantity) {
  EnterEtcTabWithStack(/*count=*/10, /*sell_price=*/2);
  OpenStackSell();
  // Digits append, so clear the field before typing the quantity to sell.
  controller_->OnEvent(ftxui::Event::Backspace);       // 10 -> 1
  controller_->OnEvent(ftxui::Event::Backspace);       // 1 -> 0
  controller_->OnEvent(ftxui::Event::Character('3'));  // quantity 3
  controller_->OnEvent(ftxui::Event::ArrowDown);       // textbox -> [Confirm]
  controller_->OnEvent(ftxui::Event::Return);          // Confirm

  EXPECT_EQ(state_->character.stackables()[0].count(), 7);
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
  shell.set_sell_price(7);
  state_->items["green_snail_shell"] = shell;
  state_->character.AddItem(shell, 50);
  state_->character.SellStackable(0, 50);
  int64_t meso = state_->character.meso();

  OpenBuyBackDialog();
  ASSERT_EQ(controller_->screen(), kShopBuy);
  // The dialog opens on the whole row; [1] takes one copy.
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(state_->character.meso(), meso - 7);
  EXPECT_EQ(state_->character.CountItem(shell), 1);
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

// Inspect shows the item as it was sold, not a fresh one from the shop: its
// stars are why the row is worth buying back.
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
  EXPECT_EQ(controller_->comparison_slots().worn.size(), 1u)
      << "the sold sword resolved against the catalog";
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

// Enter opens the map's menu, which starts on Move, so travelling is Enter
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
  // Viewed, not travelled to.
  EXPECT_EQ(state_->current_map, "cave");
  EXPECT_FALSE(mob_inspect_panel_->selected_mob().empty());

  // Escape returns to the list it was opened from, not to the game.
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

// The tab bar owns Left and Right, as in the bag and the shop. In the list,
// they would change the list under the cursor.
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

// Enter works in the list. On the tab bar it does nothing, since the bar isn't
// a map and a menu there would be about nothing.
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

// The two right-hand panels unlock as the player levels. Until then they aren't
// drawn and Tab doesn't stop on them.
TEST_F(TuiControllerTest, TheRightHandPanelsArriveWithTheirLevels) {
  // The fixture levels past both while earning SP, so this builds a new
  // character at each level rather than levelling up to it.
  GameState fresh({}, {}, {}, {}, {});
  ASSERT_EQ(fresh.character.proto().level(), 1);

  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, no_scrolls_);
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PlayerListPanel players;
  PlayerInspectPanel player_inspect(fresh);
  TradePanel trade(fresh.character, fresh.account);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  BankPanel bank(fresh.character, fresh.account, fresh.items);
  LinkSkillPanel links(fresh.character, fresh.skills);
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  BuffInfoPanel buffs;
  InspectPanel item_card;
  InspectPanel trace_card;
  InspectPanel worn_card;
  CubePanel cube;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  MusicPlayer player(MusicPlayer::Backend::kNull);
  std::mt19937 music_rng(1);
  MusicDirector director(player, music_rng);
  JukeboxPanel jukebox(fresh, director, fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,      bag,   scroll,         item_card,
                     trace_card, star,       cube,  trace,          sell,
                     sell_equip, multi_sell, maps,  mobs,           bosses,
                     party,      players,    trade, player_inspect, worn_card,
                     shop,       buy,        bank,  links,          jobs,
                     skill_card, buffs,      menu,  keybinds,       options,
                     jukebox},
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

// Tab cycles through the panels that exist. At level 1 there are two, so the
// player can never Tab to a panel that isn't on screen.
TEST_F(TuiControllerTest, TabSkipsThePanelsThatAreNotThereYet) {
  GameState fresh({}, {}, {}, {}, {});
  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, no_scrolls_);
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PlayerListPanel players;
  PlayerInspectPanel player_inspect(fresh);
  TradePanel trade(fresh.character, fresh.account);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  BankPanel bank(fresh.character, fresh.account, fresh.items);
  LinkSkillPanel links(fresh.character, fresh.skills);
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  BuffInfoPanel buffs;
  InspectPanel item_card;
  InspectPanel trace_card;
  InspectPanel worn_card;
  CubePanel cube;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  MusicPlayer player(MusicPlayer::Backend::kNull);
  std::mt19937 music_rng(1);
  MusicDirector director(player, music_rng);
  JukeboxPanel jukebox(fresh, director, fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,      bag,   scroll,         item_card,
                     trace_card, star,       cube,  trace,          sell,
                     sell_equip, multi_sell, maps,  mobs,           bosses,
                     party,      players,    trade, player_inspect, worn_card,
                     shop,       buy,        bank,  links,          jobs,
                     skill_card, buffs,      menu,  keybinds,       options,
                     jukebox},
      analysis, keys, focus);

  controller.OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(focus, kCombatPanel) << "past both locked panels";
  controller.OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(focus, kCharPanel) << "and back round";
}

// And in reverse over the same gap. Going backwards reaches the two locked
// panels from the other side, which is where a step of -1 would have made the
// modulo negative.
TEST_F(TuiControllerTest, ShiftTabSkipsThePanelsThatAreNotThereYet) {
  GameState fresh({}, {}, {}, {}, {});
  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, no_scrolls_);
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PlayerListPanel players;
  PlayerInspectPanel player_inspect(fresh);
  TradePanel trade(fresh.character, fresh.account);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  BankPanel bank(fresh.character, fresh.account, fresh.items);
  LinkSkillPanel links(fresh.character, fresh.skills);
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  BuffInfoPanel buffs;
  InspectPanel item_card;
  InspectPanel trace_card;
  InspectPanel worn_card;
  CubePanel cube;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  MusicPlayer player(MusicPlayer::Backend::kNull);
  std::mt19937 music_rng(1);
  MusicDirector director(player, music_rng);
  JukeboxPanel jukebox(fresh, director, fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,      bag,   scroll,         item_card,
                     trace_card, star,       cube,  trace,          sell,
                     sell_equip, multi_sell, maps,  mobs,           bosses,
                     party,      players,    trade, player_inspect, worn_card,
                     shop,       buy,        bank,  links,          jobs,
                     skill_card, buffs,      menu,  keybinds,       options,
                     jukebox},
      analysis, keys, focus);

  controller.OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(focus, kCombatPanel) << "back past both locked panels";
  controller.OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(focus, kCharPanel) << "and back round";
}

// The game starts focused on the equipped panel, which a level 1 character
// doesn't have. Focus must move before a key is handled, or it goes to a panel
// the player can't see.
TEST_F(TuiControllerTest, FocusLeavesAPanelThatIsNotOnScreen) {
  GameState fresh({}, {}, {}, {}, {});
  CharacterPanel chars(fresh.character, fresh.account, panel_focus_,
                       fresh.skills);
  EquippedPanel equip(fresh.character, fresh.account, panel_focus_);
  InventoryPanel bag(fresh.character, fresh.account, panel_focus_);
  ScrollPanel scroll(fresh.character, no_scrolls_);
  StarForcePanel star;
  TraceRecoverPanel trace(fresh.character);
  SellPanel sell;
  SellEquipPanel sell_equip;
  MultiSellPanel multi_sell(fresh.character, fresh.account);
  MapSelectPanel maps(fresh);
  MobInspectPanel mobs(fresh);
  BossSelectPanel bosses(fresh);
  PartySelectPanel party;
  PlayerListPanel players;
  PlayerInspectPanel player_inspect(fresh);
  TradePanel trade(fresh.character, fresh.account);
  ShopPanel shop(fresh.character, fresh.equips, fresh.items);
  BuyPanel buy;
  BankPanel bank(fresh.character, fresh.account, fresh.items);
  LinkSkillPanel links(fresh.character, fresh.skills);
  JobInspectPanel jobs(fresh.skills);
  SkillInspectPanel skill_card;
  BuffInfoPanel buffs;
  InspectPanel item_card;
  InspectPanel trace_card;
  InspectPanel worn_card;
  CubePanel cube;
  int focus = kEquipPanel;  // where the game starts
  BattleAnalysis analysis;
  MenuPanel menu(fresh, analysis, focus);
  KeyMap keys(fresh.account.mutable_keybinds());
  KeybindsPanel keybinds(keys);
  OptionsPanel options(fresh.account);
  MusicPlayer player(MusicPlayer::Backend::kNull);
  std::mt19937 music_rng(1);
  MusicDirector director(player, music_rng);
  JukeboxPanel jukebox(fresh, director, fresh.account);
  TuiController controller(
      fresh, Screens{chars,      equip,      bag,   scroll,         item_card,
                     trace_card, star,       cube,  trace,          sell,
                     sell_equip, multi_sell, maps,  mobs,           bosses,
                     party,      players,    trade, player_inspect, worn_card,
                     shop,       buy,        bank,  links,          jobs,
                     skill_card, buffs,      menu,  keybinds,       options,
                     jukebox},
      analysis, keys, focus);

  controller.OnEvent(ftxui::Event::Custom);  // any key at all
  EXPECT_TRUE(controller.PanelVisible(focus));
}

// Inspect is first on the stack menu and opens the same screen as the equip
// lists, showing what the item is rather than what it is worth.
TEST_F(TuiControllerTest, StackInspectShowsTheItemsDescription) {
  ItemPrototype shell;
  shell.set_name("Green Snail Shell");
  shell.set_description("A shell shed by a snail.");
  state_->character.AddItem(shell, 3);
  panel_focus_ = kInventoryPanel;
  OpenBagTab(kEtcTab);
  inventory_component_->OnEvent(ftxui::Event::ArrowDown);  // into the list
  inventory_component_->OnEvent(ftxui::Event::Return);     // the stack menu
  controller_->OnEvent(ftxui::Event::Return);              // Inspect

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

// An hour away with something earned.
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

// A player who restarts a minute after closing the game is shown nothing.
TEST_F(TuiControllerTest, TooShortAnAbsenceRaisesNoCard) {
  OfflineReport report = PaidReport();
  report.absence = kOfflineNoticeSeconds - 1.0;

  controller_->OpenOfflineReport(report);

  EXPECT_EQ(controller_->screen(), kMain);
}

// Nor is a player who logged off in town, since there is nothing to report.
TEST_F(TuiControllerTest, NothingFarmedRaisesNoCard) {
  OfflineReport report = PaidReport();
  report.farmed = false;

  controller_->OpenOfflineReport(report);

  EXPECT_EQ(controller_->screen(), kMain);
}

// --- the expanded bag ---

// The expanded bag replaces the main view, so Escape closes it instead of
// opening the quit prompt, and only asks about quitting once it is closed.
TEST_F(TuiControllerTest, EscapeClosesTheExpandedPanelBeforeTheGame) {
  controller_->ToggleExpanded(kInventoryPanel);
  ASSERT_EQ(controller_->expanded_panel(), kInventoryPanel);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->expanded_panel(), kNoPanel);
  EXPECT_EQ(controller_->screen(), kMain);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kQuit);
}

// The button closes the panel it is on and un-expands any other, so the two
// panels that have one can't both be expanded.
TEST_F(TuiControllerTest, OnlyOnePanelIsExpandedAtATime) {
  controller_->ToggleExpanded(kInventoryPanel);
  controller_->ToggleExpanded(kEquipPanel);
  EXPECT_EQ(controller_->expanded_panel(), kEquipPanel);

  controller_->ToggleExpanded(kEquipPanel);
  EXPECT_EQ(controller_->expanded_panel(), kNoPanel);
}

// Nowhere to move to: the other panels aren't drawn behind it.
TEST_F(TuiControllerTest, TabDoesNotLeaveTheExpandedPanel) {
  panel_focus_ = kInventoryPanel;
  controller_->ToggleExpanded(kInventoryPanel);

  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(panel_focus_, kInventoryPanel);
}

// --- the character select ---

TEST_F(TuiControllerTest, CharactersOpensTheSelectAndStopsTheFarm) {
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  EXPECT_EQ(controller_->screen(), kCharacterSelect);
  EXPECT_TRUE(controller_->OnCharacterSelect())
      << "the fight does not run behind it";
}

TEST_F(TuiControllerTest, EscapeResumesTheCharacterInPlay) {
  state_->character.SetUsername("First");
  AddCharacter("Second");
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // the cursor on "Second"
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(controller_->OnCharacterSelect());
  EXPECT_EQ(state_->character.username(), "First");
  EXPECT_FALSE(controller_->TakeCharacterSwitch())
      << "a resume must not rebuild the fight they left going";
}

// Cancelling the Quit button's confirmation returns to the list, not the game.
TEST_F(TuiControllerTest, QuitButtonCancelComesBackToTheList) {
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  controller_->OnEvent(ftxui::Event::ArrowDown);   // onto the buttons: Create
  controller_->OnEvent(ftxui::Event::ArrowRight);  // -> Quit
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kQuit);

  controller_->OnEvent(ftxui::Event::Return);  // the prompt opens on Cancel
  EXPECT_EQ(controller_->screen(), kCharacterSelect);
  EXPECT_FALSE(controller_->quit_requested());
}

TEST_F(TuiControllerTest, CreateMakesACharacterAndPlaysThem) {
  state_->character.SetUsername("First");
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  // Down from the only character onto the buttons, where Create is first.
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.username(), kDefaultUsername);
  EXPECT_EQ(state_->character.proto().level(), 1);
  EXPECT_EQ(state_->played_slot, 1);
  EXPECT_EQ(state_->offline_slot, 0) << "the check does not follow";
  EXPECT_TRUE(controller_->TakeCharacterSwitch());
  EXPECT_TRUE(controller_->TakeSaveRequest());
}

TEST_F(TuiControllerTest, PlayPutsTheChosenCharacterIn) {
  state_->character.SetUsername("First");
  AddCharacter("Second");
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // onto the other character
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kCharacterMenu);
  controller_->OnEvent(ftxui::Event::Return);  // Play

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.username(), "Second");
  EXPECT_TRUE(controller_->TakeCharacterSwitch());
}

// The way back into the game for an account with one character, which is also
// what the player uses after deleting everyone else.
TEST_F(TuiControllerTest, PlayOnTheCharacterInPlayResumesThem) {
  state_->character.SetUsername("Only");
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kCharacterMenu);
  controller_->OnEvent(ftxui::Event::Return);  // Play

  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.username(), "Only");
  EXPECT_FALSE(controller_->TakeCharacterSwitch())
      << "a resume must not rebuild the fight they left going";
}

TEST_F(TuiControllerTest, SetOfflineMovesTheCheckAndStaysOnTheList) {
  state_->character.SetUsername("First");
  AddCharacter("Second");
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Play -> Set Offline
  controller_->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(controller_->screen(), kCharacterSelect);
  EXPECT_EQ(state_->offline_slot, 1);
  EXPECT_EQ(state_->played_slot, 0) << "the check is not a switch";
  EXPECT_FALSE(controller_->TakeCharacterSwitch());
  EXPECT_TRUE(controller_->TakeSaveRequest());
}

TEST_F(TuiControllerTest, DeleteAsksFirstAndCanBeBackedOutOf) {
  state_->character.SetUsername("First");
  AddCharacter("Second");
  controller_->OpenMenuEntry(MenuEntry::kCharacters);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Play -> Set Offline
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Delete
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kCharacterDelete);

  // The confirmation starts on Cancel, like every confirmation that can't be
  // undone.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kCharacterSelect);
  EXPECT_EQ(state_->inactive_characters.size(), 1u);

  // Backing out left the cursor on the row it was opened from.
  controller_->OnEvent(ftxui::Event::Return);  // the menu again
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);     // Delete
  controller_->OnEvent(ftxui::Event::ArrowLeft);  // Cancel -> Confirm
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kCharacterSelect);
  EXPECT_TRUE(state_->inactive_characters.empty());
  EXPECT_EQ(state_->character.username(), "First") << "who was being played";
}

// --- quitting ---

TEST_F(TuiControllerTest, EscapeOnTheMainScreenAsksBeforeQuitting) {
  controller_->OnEvent(ftxui::Event::Escape);

  EXPECT_EQ(controller_->screen(), kQuit);
  EXPECT_FALSE(controller_->quit_requested()) << "asked, not acted on";
}

// This one key press must not be able to end the game by itself. Nothing is
// saved, so the prompt starts on Cancel and a second Enter answers "no".
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

// Escape means "close what is open" everywhere else, and only means "quit" once
// nothing else is open. The quit branch is below every screen branch in OnEvent
// to make this work, so it is worth testing.
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
  // The cursor is still on the menu row until the player moves up into the box.
  EXPECT_EQ(menu_panel_->box_cursor(), -1);
  // Up moves through the box from the bottom, so the entry nearest the row
  // comes first: Jukebox, Keybinds, Options.
  controller_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(menu_panel_->box_cursor(), 2);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(menu_panel_->box_cursor(), -1);
  // Escape closes the box.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_FALSE(menu_panel_->box_open());
}

// The box belongs to the Settings entry, so moving the menu row off it closes
// the box too.
TEST_F(TuiControllerTest, WalkingOffSettingsClosesItsBox) {
  LevelTo(UnlockLevel(Feature::kBoss));
  // Analysis, Boss, Multiplayer, Settings: the cursor starts on the first.
  menu_panel_->MoveCursor(3);
  ASSERT_EQ(menu_panel_->selected(), MenuEntry::kSettings);
  controller_->OpenMenuEntry(MenuEntry::kSettings);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_FALSE(menu_panel_->box_open());
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(menu_panel_->selected(), MenuEntry::kMultiplayer);
}

// Inside the box, Left doesn't move the menu row below.
TEST_F(TuiControllerTest, LeftInsideTheBoxDoesNothing) {
  controller_->OpenMenuEntry(MenuEntry::kSettings);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_TRUE(menu_panel_->box_open());
  EXPECT_EQ(menu_panel_->box_cursor(), 2);
}

TEST_F(TuiControllerTest, TheJukeboxOpensFromTheBoxAndComesBackToIt) {
  OpenJukebox();
  EXPECT_EQ(controller_->screen(), kJukebox);
  // The screen opens on the song list, and Tab moves the keys to the buttons.
  EXPECT_FALSE(jukebox_panel_->on_buttons());
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(jukebox_panel_->on_buttons());

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
}

// Escape closes the mode box first, instead of leaving the screen.
TEST_F(TuiControllerTest, EscapeShutsTheJukeboxModeBoxBeforeTheScreen) {
  OpenJukebox();
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  ASSERT_EQ(jukebox_panel_->selected_button(), JukeboxButton::kMode);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(jukebox_panel_->mode_box_open());

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(jukebox_panel_->mode_box_open());
  EXPECT_EQ(controller_->screen(), kJukebox);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
}

TEST_F(TuiControllerTest, KeybindsOpensFromTheBoxAndComesBackToIt) {
  OpenKeybinds();
  EXPECT_EQ(controller_->screen(), kKeybinds);
  // Escape on an empty slot returns to the box, which is still open where it
  // was.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
}

TEST_F(TuiControllerTest, EnterOnASlotTakesTheNextKeyPressed) {
  OpenKeybinds();
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(controller_->capturing_key());
  // The ticker's redraw isn't a key press.
  controller_->OnEvent(ftxui::Event::Custom);
  EXPECT_TRUE(controller_->capturing_key());
  controller_->OnEvent(ftxui::Event::w);
  EXPECT_FALSE(controller_->capturing_key());
  EXPECT_EQ(keys_->Label(KEY_ACTION_UP, 1), "W");
  EXPECT_EQ(keys_->Translate(ftxui::Event::w), ftxui::Event::ArrowUp);
}

// The name field takes letters, so while it is open keys must arrive as pressed
// rather than as their bound actions.
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

// Escape on the main view asks whether to quit, so an open field must take it
// first: backing out of a field isn't backing out of the game.
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

// Tab would move focus off the panel mid-edit, leaving an open field no key can
// reach, and every rebound key stuck behind it.
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
  // Nothing left to clear, so the same key leaves.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
}

TEST_F(TuiControllerTest, AReservedKeyIsRefused) {
  OpenKeybinds();
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(controller_->capturing_key());
  EXPECT_EQ(keys_->Label(KEY_ACTION_UP, 1), "");
  // The screen didn't change: the refusal is a message, not an exit.
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

// The switch takes effect immediately: no confirmation, and nothing to close
// before the panels use the new setting.
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
  // Close comes after the last option, however many there are.
  for (int i = 0; i < kOptionCount; ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  EXPECT_TRUE(options_panel_->on_close());
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  // Close didn't toggle anything on the way out.
  EXPECT_FALSE(state_->account.panel_title_blink());
}

// --- battle analysis ---

// Left and Right change the volume under the cursor, and only a volume.
TEST_F(TuiControllerTest, ArrowsMoveTheVolumeUnderTheCursor) {
  if (!kAudioEnabled) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  OpenOptions();
  // The cursor starts on the switch, which the arrows must leave alone.
  controller_->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_FALSE(state_->account.panel_title_blink());

  for (int i = 0; i < static_cast<int>(Option::kMapBgmVolume); ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(options_panel_->selected_option(), Option::kMapBgmVolume);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(state_->account.map_bgm_volume(), kDefaultBgmVolume + 2);
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(state_->account.map_bgm_volume(), kDefaultBgmVolume + 1);
  EXPECT_EQ(state_->account.boss_bgm_volume(), kDefaultBgmVolume)
      << "the other slider did not move";
}

// The first row of the box toggles the tool, and the box stays open: the row
// the player pressed has just changed to the other action.
TEST_F(TuiControllerTest, TheAnalysisBoxStartsAndStopsTheTool) {
  controller_->OpenMenuEntry(MenuEntry::kAnalysis);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_EQ(menu_panel_->box_entry(), MenuEntry::kAnalysis);

  // The box is above the menu row, so Up reaches its bottom entry first, and
  // Start, listed at the top, is the second stop.
  controller_->OnEvent(ftxui::Event::ArrowUp);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  ASSERT_EQ(menu_panel_->selected_analysis_entry(), AnalysisEntry::kStartStop);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(analysis_.state(), AnalysisState::kWaitingToStart);
  EXPECT_EQ(controller_->screen(), kMenuBox);

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(analysis_.state(), AnalysisState::kStopped);
}

// A Stop pressed by mistake is undone by pressing the same row again.
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

// --- Dailies ---

// The claim lists every symbol up to the highest one held, and Confirm puts a
// day's worth of each into the bag.
TEST_F(TuiControllerTest, TheDailiesClaimPaysEverySymbolListed) {
  state_->character.PickUp(std::make_unique<EquipInstance>(
      state_->equips.at("Arcane Symbol: Chu Chu Island")));
  controller_->OpenMenuEntry(MenuEntry::kDailies);
  ASSERT_EQ(controller_->screen(), kDailies);

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 3)
      << "the one held, plus a packed stack for each of the two areas";
  EXPECT_EQ(
      state_->character.SpareSymbolWorths(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY),
      (std::vector<int>{kSymbolsPerDay}));
  EXPECT_GT(state_->character.DailiesClaimedAt(), 0);
}

// A character who has never had a symbol has nothing to claim, and is told so
// rather than shown an empty list.
TEST_F(TuiControllerTest, NoSymbolsMeansNoClaim) {
  controller_->OpenMenuEntry(MenuEntry::kDailies);
  EXPECT_EQ(controller_->screen(), kDailiesNotice);
  EXPECT_FALSE(controller_->notice_is_refusal())
      << "a level they have not reached, not a refusal";
  EXPECT_EQ(NoticeText(controller_->notice_lines()),
            "You have no dailies to claim.");
  EXPECT_EQ(state_->character.DailiesClaimedAt(), 0) << "the day is untouched";
}

TEST_F(TuiControllerTest, CancellingTheClaimTakesNothing) {
  state_->character.PickUp(
      std::make_unique<EquipInstance>(state_->equips.at(symbol_.name())));
  controller_->OpenMenuEntry(MenuEntry::kDailies);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->character.DailiesClaimedAt(), 0)
      << "the day is still there";
}

// Once claimed, the dailies say so rather than asking a question whose answer
// would be no.
TEST_F(TuiControllerTest, AClaimedDaySaysSo) {
  state_->character.PickUp(
      std::make_unique<EquipInstance>(state_->equips.at(symbol_.name())));
  state_->character.RecordDailiesClaim(
      static_cast<int64_t>(std::time(nullptr)));
  controller_->OpenMenuEntry(MenuEntry::kDailies);
  EXPECT_EQ(controller_->screen(), kDailiesNotice);
  EXPECT_FALSE(controller_->notice_is_refusal()) << "the clock, not the player";
  EXPECT_EQ(NoticeText(controller_->notice_lines()), "Already claimed today.");
  EXPECT_EQ(controller_->notice_button(), "Close");

  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Claiming part would lose the rest until tomorrow, so a full bag takes none of
// it and the claim stays available.
TEST_F(TuiControllerTest, AFullBagRefusesTheWholeClaim) {
  state_->character.PickUp(std::make_unique<EquipInstance>(
      state_->equips.at("Arcane Symbol: Chu Chu Island")));
  while (!state_->character.inventory().full()) {
    state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  controller_->OpenMenuEntry(MenuEntry::kDailies);
  ASSERT_EQ(controller_->screen(), kDailies);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kDailiesNotice);
  EXPECT_TRUE(controller_->notice_is_refusal());
  EXPECT_EQ(state_->character.DailiesClaimedAt(), 0);
}

TEST_F(TuiControllerTest, ViewOpensTheAnalysisOverlayAndBackClosesIt) {
  controller_->OpenMenuEntry(MenuEntry::kAnalysis);
  controller_->OnEvent(ftxui::Event::ArrowUp);
  ASSERT_EQ(menu_panel_->selected_analysis_entry(), AnalysisEntry::kView);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kAnalysis);

  // Escape returns to the box, which is still open where it was.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMenuBox);
  EXPECT_TRUE(menu_panel_->box_open());
}

// Tab moves through the boss screen's three windows. Enter asks to start the
// fight from the two that describe it, and toggles a switch on the row that
// holds the switches.
TEST_F(TuiControllerTest, TabWalksTheBossScreensWindows) {
  HoldASword();
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  EXPECT_EQ(boss_select_panel_->focus(), BossPanel::kList);
  controller_->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(boss_select_panel_->focus(), BossPanel::kFight);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossConfirm);
  controller_->OnEvent(ftxui::Event::Escape);

  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(boss_select_panel_->focus(), BossPanel::kList);
  controller_->OnEvent(ftxui::Event::TabReverse);
  EXPECT_EQ(boss_select_panel_->focus(), BossPanel::kOptions);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossSelect)
      << "the row takes its own Enter";
  EXPECT_TRUE(boss_select_panel_->practice());
}

TEST_F(TuiControllerTest, EnterOnAFightAsksBeforeTakingIt) {
  HoldASword();
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossConfirm);
  EXPECT_EQ(controller_->boss_prompt_title(), "Normal Zakum");

  // The prompt starts on Confirm, and Escape backs out to the list.
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBossSelect);
}

// A boss already cleared this reset says when it resets instead of asking a
// question whose answer would be no. It doesn't name a difficulty, since
// clearing any one locks the rest.
TEST_F(TuiControllerTest, AClearedFightSaysWhenItComesBack) {
  HoldASword();
  state_->character.RecordBossClear("zakum", "Normal",
                                    static_cast<int64_t>(std::time(nullptr)));
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_FALSE(controller_->notice_is_refusal()) << "the reset, not the player";
  // Split into even lines rather than at the name: a short name over a long
  // remainder looks like two unrelated rows.
  EXPECT_EQ(
      controller_->notice_lines(),
      (std::vector<std::string>{"Zakum has already", "been killed today."}));
  EXPECT_TRUE(controller_->notice_prompt().open());

  // The notice stays until it is dismissed.
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_FALSE(controller_->notice_prompt().open());
}

// The difficulties next to the cleared one are locked too, and the notice names
// the boss rather than the difficulty under the cursor.
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
  EXPECT_EQ(NoticeText(controller_->notice_lines()),
            "Zakum has already been killed today.");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";
}

// A fight above the character's level says what level it needs. The difficulty
// is dimmed on the list, and this is what Enter on it shows.
TEST_F(TuiControllerTest, ALockedFightNamesTheLevelItOpensAt) {
  HoldASword();
  state_->bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  EXPECT_TRUE(controller_->notice_is_refusal()) << "drawn in red";
  EXPECT_EQ(NoticeText(controller_->notice_lines()),
            "Normal Zakum unlocks at level 130.");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";
}

// A fight that isn't built yet says only that, not the weapon, level or reset,
// since none of those is the reason.
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
  EXPECT_EQ(NoticeText(controller_->notice_lines()),
            "Chaos Zakum is coming soon!");
  EXPECT_EQ(controller_->boss_run(), nullptr) << "nothing was started";
}

// A character without a weapon can't fight, and is told so.
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

// Practice ignores the reset and records no clear: a boss already beaten today
// can still be entered, and beating it again records nothing.
TEST_F(TuiControllerTest, PracticeBypassesTheResetAndSpendsNoClear) {
  HoldASword();
  state_->character.RecordBossClear("zakum", "Normal",
                                    static_cast<int64_t>(std::time(nullptr)));
  int64_t cleared = state_->character.BossClearedAt("zakum", "Normal");
  ASSERT_GT(cleared, 0);

  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossNotice) << "cleared today";
  controller_->OnEvent(ftxui::Event::Return);

  // Onto the options row, and toggle the switch.
  controller_->OnEvent(ftxui::Event::TabReverse);
  ASSERT_EQ(boss_select_panel_->focus(), BossPanel::kOptions);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(state_->boss_options.practice());

  // Back onto the grid, after the row took the Enter.
  controller_->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(boss_select_panel_->focus(), BossPanel::kList);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossConfirm) << "the reset is walked past";
  EXPECT_TRUE(controller_->boss_prompt_practice());
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossFight);
  ASSERT_NE(controller_->boss_run(), nullptr);
  EXPECT_TRUE(controller_->boss_run()->practice());

  RunFightToEnd();
  ASSERT_EQ(controller_->screen(), kBossClear);
  EXPECT_EQ(state_->character.BossClearedAt("zakum", "Normal"), cleared)
      << "the clock never heard about it";
  EXPECT_EQ(controller_->boss_clear_reward().meso, 0);
}

TEST_F(TuiControllerTest, EscapeLeavesTheBossScreen) {
  controller_->OpenMenuEntry(MenuEntry::kBoss);
  controller_->OnEvent(ftxui::Event::Tab);  // would cycle focus in kMain
  EXPECT_EQ(controller_->screen(), kBossSelect);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// The Extreme Green Potion is used on entry, whatever happens in the fight. It
// is charged once per entry, not per phase or per clear.
TEST_F(TuiControllerTest, TheGreenPotionIsChargedOnTheWayIntoAFight) {
  LevelTo(190);
  state_->character.AddMeso(3'000'000);
  ASSERT_TRUE(
      state_->character.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));

  EnterFight();
  ASSERT_EQ(controller_->screen(), kBossFight);
  EXPECT_EQ(state_->character.meso(), 2'000'000);

  // Switched off, the next fight is free.
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

// The fight fills the screen: no key reaches the panels behind it, and a fight
// with nowhere to move swallows the arrows too.
TEST_F(TuiControllerTest, TheFightSwallowsEverythingButEscape) {
  EnterFight();
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(controller_->screen(), kBossFight);
  EXPECT_EQ(panel_focus_, kEquipPanel);
}

// The only choice the player makes during a fight: where to stand.
TEST_F(TuiControllerTest, TheArrowsWalkThePlayerAroundTheArena) {
  BossPhase* phase =
      state_->bosses["zakum"].mutable_difficulties(0)->mutable_phases(0);
  // The middle of the floor first, where the phase starts them.
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

  // The prompt starts on Cancel, so Enter returns to the fight.
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

  // No ending pause when leaving: the next tick is already back at the list.
  controller_->AdvanceBossRun(0.0);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_EQ(controller_->boss_run(), nullptr);
  EXPECT_FALSE(controller_->in_boss_fight());
  // Leaving isn't a clear, so the daily is still available.
  EXPECT_EQ(state_->character.BossClearedAt("zakum", "Normal"), 0);
}

// Running out of time is the ending the player may not have seen, so it says so
// rather than just returning to the list.
TEST_F(TuiControllerTest, RunningOutOfTimeSaysSo) {
  // A mob the character can't kill within the time limit. The fixture's arms
  // die in one hit, and a won fight is a different ending.
  state_->mobs["zakum_arm"].set_max_hp(2000000000);
  EnterFight();
  controller_->AdvanceBossRun(kBossCountdownSeconds + 301.0);
  ASSERT_NE(controller_->boss_run(), nullptr) << "the closing beat is running";
  controller_->AdvanceBossRun(kBossEndHoldSeconds);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  // Kept until the notice is dismissed, so the arena stays behind it, and the
  // clock is stopped: a lost fight can't be lost again.
  EXPECT_NE(controller_->boss_run(), nullptr);
  controller_->AdvanceBossRun(10.0);
  EXPECT_EQ(controller_->screen(), kBossNotice);
  ASSERT_EQ(controller_->notice_lines().size(), 1u);
  EXPECT_EQ(controller_->notice_lines()[0], "Out of time!");
  EXPECT_FALSE(controller_->notice_is_refusal());
  // Nothing was cleared, so the daily is still available.
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

// The card outlives the run it reports on, so its contents must be copied from
// the run rather than read through a pointer that is now null.
TEST_F(TuiControllerTest, TheClearCardNamesTheFightAndWhatItPaid) {
  BossDifficulty* normal = state_->bosses["zakum"].mutable_difficulties(0);
  normal->set_meso(3062500);
  MobDrop* shard = normal->add_drops();
  shard->set_item("zakums_soul_shard");
  shard->set_per_kill(1.0);
  EnterFight();
  RunFightToEnd();

  ASSERT_EQ(controller_->screen(), kBossClear);
  // The run is kept while the card is up, since the card is drawn over the
  // arena the player just cleared.
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

// [Analysis] opens the fight's damage table under the player's own name, and
// Escape returns to the card with [Analysis] selected; Escape there leaves.
TEST_F(TuiControllerTest, TheClearCardOpensTheAnalysisAndComesBack) {
  EnterFight();
  RunFightToEnd();
  ASSERT_EQ(controller_->screen(), kBossClear);
  EXPECT_FALSE(controller_->boss_clear_on_analysis());

  controller_->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_TRUE(controller_->boss_clear_on_analysis());
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBossAnalysis);
  const BossAnalysisPanel& panel = controller_->boss_analysis_panel();
  EXPECT_FALSE(panel.party());
  ASSERT_EQ(panel.players().size(), 1u);
  EXPECT_EQ(panel.players()[0].name, state_->character.username());
  EXPECT_FALSE(panel.table().empty());

  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.skill_cursor(), panel.table().size() > 1 ? 1 : 0);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBossClear);
  EXPECT_TRUE(controller_->boss_clear_on_analysis());
  EXPECT_NE(controller_->boss_run(), nullptr);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBossSelect);
  EXPECT_EQ(controller_->boss_run(), nullptr);
}

// --- Hyper Stats ---

// [+] and [-] spend and refund immediately, with no dialog, so the screen never
// leaves the main view.
TEST_F(TuiControllerTest, RaisingAndLoweringAStatAsksNothing) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->RaiseHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR), 1);

  controller_->LowerHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR), 0);
  EXPECT_EQ(state_->character.hyper_stat_points_left(StatPreset::kFirst),
            state_->character.hyper_stat_points())
      << "the point came back";
}

// Each affects the allocation it was given and not the other.
TEST_F(TuiControllerTest, RaisingAndLoweringNameTheirAllocation) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->RaiseHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kSecond);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kFirst),
            0);

  // Nothing was spent on the farming allocation, so its [-] has nothing to
  // refund.
  controller_->LowerHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kSecond),
            1);
}

// The confirmation names the allocation, and confirming resets only that one.
TEST_F(TuiControllerTest, TheResetEmptiesTheAllocationItNamed) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->RaiseHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst);
  controller_->RaiseHyperStat(HYPER_STAT_FIELD_DEX, StatPreset::kSecond);

  controller_->OpenHyperReset(StatPreset::kSecond);
  // The question uses the tab's label, which is a number while autoswap is off.
  EXPECT_EQ(controller_->hyper_reset_question(), "Reset 2 Hyper Stats?");
  state_->account.SetAutoswapPresets(true);
  state_->MirrorAccount();
  EXPECT_EQ(controller_->hyper_reset_question(), "Reset Boss Hyper Stats?");
  // It starts on Cancel, so reaching Confirm takes a step.
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_DEX,
                                               StatPreset::kSecond),
            0);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR,
                                               StatPreset::kFirst),
            1);
}

// The V page's confirmation: it starts on Cancel, and Confirm refunds the whole
// matrix to the pool.
TEST_F(TuiControllerTest, TheVMatrixResetHandsTheNodesBack) {
  Skill node;
  node.set_name("Rope Lift");
  PlaceIn(node, JOB_ADVANCEMENT_COMMON);
  node.set_v_node(V_NODE_KIND_COMMON);
  node.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  state_->skills["rope_lift"] = node;
  LevelTo(200);
  state_->character.AdvanceJob(JOB_SPEARMAN);
  state_->character.AdvanceJob(JOB_BERSERKER);
  state_->character.AdvanceJob(JOB_DARK_KNIGHT);
  state_->character.AdvanceJob(JOB_DARK_KNIGHT);  // the 5th
  state_->character.AddVPoints(20);
  ASSERT_TRUE(state_->character.LearnSkill(node));

  controller_->OpenVMatrixReset();
  EXPECT_EQ(controller_->screen(), kVMatrixReset);
  // It starts on Cancel, so the first Enter backs out and the matrix is
  // unchanged.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.skill_level(node), 1);

  controller_->OpenVMatrixReset();
  controller_->OnEvent(ftxui::Event::ArrowLeft);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.skill_level(node), 0);
  EXPECT_EQ(state_->character.v_points(), 20) << "the seven came back";
}

// The card reads the allocation it was opened on, and reads the level live from
// the character, so spending a point and reopening shows the new level.
TEST_F(TuiControllerTest, TheHyperStatCardReadsTheAllocationItWasOpenedOn) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->RaiseHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kSecond);

  controller_->OpenHyperStatInspect(HYPER_STAT_FIELD_STR, StatPreset::kSecond);
  EXPECT_EQ(controller_->screen(), kHyperStatInspect);
  EXPECT_EQ(controller_->hyper_inspect_field(), HYPER_STAT_FIELD_STR);
  EXPECT_EQ(controller_->hyper_inspect_level(), 1);
  EXPECT_EQ(controller_->hyper_inspect_max_level(),
            state_->character.max_hyper_stat_level());

  // Nothing has been spent on the other allocation.
  controller_->OpenHyperStatInspect(HYPER_STAT_FIELD_STR, StatPreset::kFirst);
  EXPECT_EQ(controller_->hyper_inspect_level(), 0);

  // Read-only, so either key leaves.
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Enter alone on the reset dialog backs out rather than resetting.
TEST_F(TuiControllerTest, TheResetOpensOnCancel) {
  LevelTo(kHyperStatUnlockLevel);
  controller_->RaiseHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst);
  controller_->OpenHyperReset(StatPreset::kFirst);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.hyper_stat_level(HYPER_STAT_FIELD_STR), 1);
}

// --- Inner Ability ---

// Enter toggles the lock without asking; there is no screen for it.
TEST_F(TuiControllerTest, LockingALineAsksNothing) {
  LevelTo(kInnerAbilityUnlockLevel);
  // A new ability is three Rare lines, and a Rare line can be locked like any
  // other.
  controller_->ToggleAbilityLock(0, StatPreset::kFirst);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_TRUE(state_->character.ability().lines(0).locked());
  controller_->ToggleAbilityLock(0, StatPreset::kFirst);
  EXPECT_FALSE(state_->character.ability().lines(0).locked());

  // An index past the end isn't a line that can change.
  controller_->ToggleAbilityLock(9, StatPreset::kFirst);
  EXPECT_EQ(state_->character.ability().lines_size(), kAbilityLines);
}

// The dialog lists the lines it would replace, and none of the locked ones.
TEST_F(TuiControllerTest, TheRerollDialogListsOnlyTheLinesItRerolls) {
  LevelTo(kInnerAbilityUnlockLevel);
  ASSERT_TRUE(state_->character.LockAbilityLine(0, true));

  controller_->OpenAbilityReroll(StatPreset::kFirst);
  EXPECT_EQ(controller_->screen(), kAbilityReroll);
  std::vector<AbilityLine> listed = controller_->ability_reroll_lines();
  ASSERT_EQ(listed.size(), 2u);
  for (const AbilityLine& line : listed) {
    EXPECT_FALSE(line.locked())
        << "a held line is not what is being asked about";
  }
}

// It starts on Confirm, so a reroll is Enter twice, and Cancel spends nothing.
TEST_F(TuiControllerTest, TheRerollOpensOnConfirmAndIsPaidOnce) {
  LevelTo(kInnerAbilityUnlockLevel);
  const int64_t cost = state_->character.ability_reset_cost();
  state_->character.AddHonor(2 * cost);
  const int64_t pool = state_->character.honor();

  controller_->OpenAbilityReroll(StatPreset::kFirst);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.honor(), pool - cost);

  controller_->OpenAbilityReroll(StatPreset::kFirst);
  controller_->OnEvent(ftxui::Event::ArrowRight);  // -> Cancel
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.honor(), pool - cost) << "Cancel spends nothing";
}

// A reroll that raises the rank lights the character panel, and the next key
// clears it. It rerolls until a rank-up happens, since the fixture's seed isn't
// chosen for one.
TEST_F(TuiControllerTest, AnAbilityRankUpLightsTheCharacterPanel) {
  LevelTo(kInnerAbilityUnlockLevel);
  state_->character.AddHonor(1'000'000'000);

  for (int i = 0; i < 500 && !controller_->ability_rank_up(); ++i) {
    controller_->OpenAbilityReroll(StatPreset::kFirst);
    controller_->OnEvent(ftxui::Event::Return);
  }
  ASSERT_TRUE(controller_->ability_rank_up());
  EXPECT_GT(state_->character.ability(StatPreset::kFirst).rank(),
            ABILITY_RANK_RARE);

  controller_->OnEvent(ftxui::Event::Custom);
  EXPECT_TRUE(controller_->ability_rank_up()) << "a redraw is not an action";
  controller_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_FALSE(controller_->ability_rank_up());
}

// The two allocations are rerolled separately: the confirmation names one.
TEST_F(TuiControllerTest, TheRerollLandsOnTheAllocationItNamed) {
  LevelTo(kInnerAbilityUnlockLevel);
  state_->character.AddHonor(100000);
  const std::string before =
      state_->character.ability(StatPreset::kFirst).DebugString();

  controller_->OpenAbilityReroll(StatPreset::kSecond);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.ability(StatPreset::kFirst).DebugString(), before)
      << "the farming ability was not the one asked about";
}

// --- The Link Skills screen ---

// A link skill of `line`, which is in no book and bought by nobody.
Skill LinkSkillFor(const std::string& name, Job line) {
  Skill skill;
  skill.set_name(name);
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_LINK);
  skill.set_link_line(line);
  skill.set_max_level(9);
  skill.mutable_base()->set_crit_rate(0.03);
  return skill;
}

// The catalog, a roster that has levelled the other lines, and the screen open
// on the preset in use.
void OpenLinkScreen(GameState& state, TuiController& controller) {
  state.skills["thiefs_cunning"] = LinkSkillFor("Thief's Cunning", JOB_ROGUE);
  state.skills["empirical"] = LinkSkillFor("Empirical Knowledge", JOB_MAGICIAN);
  state.character.set_account_max_level(kLinkSkillsLevel);
  LinkTally tally;
  tally.Record(JOB_NIGHT_LORD, 210);
  tally.Record(JOB_BISHOP, 210);
  state.character.set_link_tally(tally);
  controller.OpenLinkSkills();
}

// Add on a row of the bottom window equips the skill in the displayed preset,
// and Remove unequips it.
TEST_F(TuiControllerTest, TheLinkScreenEquipsAndUnequipsASkill) {
  OpenLinkScreen(*state_, *controller_);
  ASSERT_EQ(controller_->screen(), kLinkSkills);

  // Tab twice to All Skills, Enter for its menu, Down to Add, Enter.
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kLinkSkillMenu);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kLinkSkills);
  ASSERT_EQ(state_->character.link_skills(StatPreset::kFirst).size(), 1);

  // Around to the middle window, down into its list, and Remove.
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.link_skills(StatPreset::kFirst).size(), 0);
}

// A full preset refuses the next skill, and says so rather than dropping it.
TEST_F(TuiControllerTest, AFullLinkPresetSaysSo) {
  OpenLinkScreen(*state_, *controller_);
  for (int i = 0; i < kMaxEquippedLinkSkills; ++i) {
    ASSERT_TRUE(state_->character.EquipLinkSkill("Filler " + std::to_string(i),
                                                 StatPreset::kFirst));
  }
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(controller_->notification().visible());
}

// Inspect opens the skill card, which shows the level the account has reached
// rather than 0, and returns to the screen that opened it.
TEST_F(TuiControllerTest, TheLinkCardReadsTheAccountsLevelAndComesBack) {
  OpenLinkScreen(*state_, *controller_);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);  // Inspect
  EXPECT_EQ(controller_->screen(), kSkillInspect);
  EXPECT_EQ(controller_->skill_inspect_level(), 3)
      << "what the roster paid for that line";

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kLinkSkills);
}

// Enter on the preset bar opens the Hyper tab's menu, and Use makes that preset
// active.
TEST_F(TuiControllerTest, ThePresetBarPutsALinkPresetInUse) {
  OpenLinkScreen(*state_, *controller_);
  state_->character.set_autoswap_presets(false);
  controller_->OnEvent(ftxui::Event::Tab);  // -> Enabled Skills, on its bar
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kLinkSkillMenu);
  controller_->OnEvent(ftxui::Event::Return);  // Use
  EXPECT_EQ(controller_->screen(), kLinkSkills);
  EXPECT_EQ(state_->character.SlotInUse(PresetKind::kLinkSkills),
            StatPreset::kSecond);
}

// --- The Bank ---

// A row's Move sends the item to the other half and back, Inspect opens its
// card over the bank, and Escape returns to the main screen.
TEST_F(TuiControllerTest, TheBankMovesAnEquipAcrossAndBack) {
  LevelTo(UnlockLevel(Feature::kBank));
  HoldASword();  // something for the card to weigh the bagged one against
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  OpenBankScreen();

  controller_->OnEvent(ftxui::Event::ArrowDown);  // the chip -> the first row
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBankMenu);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Move
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBank);
  EXPECT_EQ(state_->character.inventory().size(), 0);
  ASSERT_EQ(state_->account.bank().equips().size(), 1);

  controller_->OnEvent(ftxui::Event::Tab);  // -> the bank's half
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);  // Inspect
  ASSERT_EQ(controller_->screen(), kBankInspect);
  InspectPanel::ComparisonSlots slots = controller_->comparison_slots();
  ASSERT_EQ(slots.worn.size(), 1u);
  EXPECT_EQ(slots.worn[0], state_->character.WornAt(StatPreset::kFirst,
                                                    EQUIP_SLOT_PRIMARY_WEAPON))
      << "the banked sword is weighed against the one in hand";
  controller_->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(controller_->screen(), kBank);

  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Move
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->account.bank().equips().size(), 0);

  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
}

// Escape and Close on a row's menu move nothing, and Sort on a tab's menu sorts
// that half: the staff a Swordman can't wear goes after the others.
TEST_F(TuiControllerTest, TheBankMenusCloseAndSort) {
  LevelTo(UnlockLevel(Feature::kBank));
  state_->character.PickUp(
      std::make_unique<EquipInstance>(state_->equips.at("wooden_staff")));
  state_->character.PickUp(std::make_unique<EquipInstance>(sword_));
  OpenBankScreen();

  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBank);
  EXPECT_FALSE(bank_panel_->menu_open());
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Close
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBank);
  EXPECT_EQ(state_->account.bank().equips().size(), 0);
  EXPECT_EQ(state_->character.inventory().size(), 2);

  controller_->OnEvent(ftxui::Event::ArrowUp);  // back to the Equip chip
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(bank_panel_->tab_menu_open());
  controller_->OnEvent(ftxui::Event::Return);  // Sort
  EXPECT_EQ(controller_->screen(), kBank);
  EXPECT_EQ(state_->character.inventory()[0].name(), "Sword");
}

// A move the bank refuses leaves the item in place and says why.
TEST_F(TuiControllerTest, TheBankRefusesASymbolAndSaysSo) {
  LevelTo(UnlockLevel(Feature::kBank));
  state_->character.PickUp(std::make_unique<EquipInstance>(symbol_));
  OpenBankScreen();

  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Move
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(controller_->notification().visible());
  EXPECT_EQ(state_->character.inventory().size(), 1);
  EXPECT_EQ(state_->account.bank().equips().size(), 0);
}

// Enter on a balance asks for an amount: the typed amount moves across, Escape
// moves nothing, and the bank's half sends it back.
TEST_F(TuiControllerTest, BankMesoCrossesThroughTheAmountDialog) {
  LevelTo(UnlockLevel(Feature::kBank));
  state_->character.AddMeso(5000);
  const int64_t meso = state_->character.meso();
  OpenBankScreen();

  controller_->OnEvent(ftxui::Event::ArrowRight);  // -> the Etc chip
  controller_->OnEvent(ftxui::Event::ArrowRight);  // -> the meso
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kBankAmount);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kBank);
  EXPECT_EQ(state_->account.bank().meso(), 0);

  controller_->OnEvent(ftxui::Event::Return);
  for (int i = 0; i < 20; ++i) {
    controller_->OnEvent(ftxui::Event::Backspace);
  }
  for (char digit : std::string("1234")) {
    controller_->OnEvent(ftxui::Event::Character(digit));
  }
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Confirm
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kBank);
  EXPECT_EQ(state_->account.bank().meso(), 1234);
  EXPECT_EQ(state_->character.meso(), meso - 1234);

  // The dialog starts at everything that half holds.
  controller_->OnEvent(ftxui::Event::Tab);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::ArrowRight);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(state_->account.bank().meso(), 0);
  EXPECT_EQ(state_->character.meso(), meso);
}

// --- Arcane Symbols ---

// Level Up on a worn symbol asks for the meso: Escape spends nothing, and
// Confirm pays the cost and raises the symbol a level.
TEST_F(TuiControllerTest, LevelUpPaysForTheWornSymbol) {
  LevelTo(200);
  Equip ready;
  ready.set_symbol_level(1);
  ready.set_symbol_exp(12);  // exactly what level 1 asks for
  state_->character.PickUp(std::make_unique<EquipInstance>(symbol_, ready));
  ASSERT_TRUE(state_->character.Equip(0));
  state_->character.AddMeso(2000000);
  const int64_t meso = state_->character.meso();

  RenderEquipPanel();
  equip_component_->OnEvent(ftxui::Event::ArrowUp);
  equip_component_->OnEvent(ftxui::Event::ArrowUp);
  equip_component_->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(equip_panel_->active_tab(), EquippedPanel::kSymbolTab);
  equip_component_->OnEvent(ftxui::Event::ArrowDown);
  RenderEquipPanel();
  ASSERT_EQ(equip_panel_->selected_slot(), EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);

  controller_->OpenEquipMenu();
  WalkGearMenuTo(kSymbolMenuLevelUp);
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kSymbolLevel);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.meso(), meso);

  controller_->OpenEquipMenu();
  WalkGearMenuTo(kSymbolMenuLevelUp);
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::Return);  // Confirm
  EXPECT_EQ(controller_->screen(), kMain);
  const EquipInstance* worn = state_->character.WornAt(
      StatPreset::kFirst, EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  ASSERT_NE(worn, nullptr);
  EXPECT_EQ(worn->equip_state().symbol_level(), 2);
  // Twelve duplicates at 8.1 each, floored, in units of ten thousand.
  EXPECT_EQ(state_->character.meso(), meso - 970000);
}

// Combine on a spare feeds all spares into the worn symbol by default; Escape
// feeds none.
TEST_F(TuiControllerTest, CombineFeedsTheSparesIn) {
  LevelTo(200);
  state_->character.PickUp(std::make_unique<EquipInstance>(symbol_));
  ASSERT_TRUE(state_->character.Equip(0));
  state_->character.PickUp(std::make_unique<EquipInstance>(symbol_));
  state_->character.PickUp(std::make_unique<EquipInstance>(symbol_));
  const EquipInstance* worn = state_->character.WornAt(
      StatPreset::kFirst, EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  ASSERT_NE(worn, nullptr);
  const int exp = worn->equip_state().symbol_exp();

  DescendIntoBag();
  controller_->OpenInventoryMenu();
  for (int i = 0; i < 10 && inventory_panel_->menu().selected() != kMenuCombine;
       ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  controller_->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(controller_->screen(), kSymbolCombine);
  controller_->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 2);

  controller_->OpenInventoryMenu();
  for (int i = 0; i < 10 && inventory_panel_->menu().selected() != kMenuCombine;
       ++i) {
    controller_->OnEvent(ftxui::Event::ArrowDown);
  }
  controller_->OnEvent(ftxui::Event::Return);
  controller_->OnEvent(ftxui::Event::ArrowDown);  // -> Confirm
  controller_->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(controller_->screen(), kMain);
  EXPECT_EQ(state_->character.inventory().size(), 0);
  worn = state_->character.WornAt(StatPreset::kFirst,
                                  EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  EXPECT_EQ(worn->equip_state().symbol_exp(), exp + 2);
}

}  // namespace
}  // namespace ms
