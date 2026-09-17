// The controller's party screen, driven against a real server on the
// loopback. Everything here needs two ends of a connection to be worth
// anything: what the screen does is send an ask and draw what comes back.

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "ftxui/component/event.hpp"
#include "server/test_server.h"
#include "src/character/equip_presets.h"
#include "src/character/hyper_stats.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/combat/boss_run.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/player_inspect_panel.h"
#include "src/frontend/screens/player_list_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/multiplayer/session.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

constexpr std::chrono::milliseconds kPatience(4000);

// One skill in the first Warrior book, so a member's Skills tab has a row to
// open a card from.
std::map<std::string, Skill> SkillCatalog() {
  Skill strike;
  strike.set_name("Power Strike");
  PlaceIn(strike, JOB_ADVANCEMENT_SWORDMAN);
  strike.set_max_level(20);
  return {{"power_strike", strike}};
}

std::unique_ptr<GameState> MakeState() {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      // One weapon in the catalog, so a member has something to be seen
      // wearing -- a sheet names its items and the reader resolves them
      // against their own catalogs, so both ends need it. Not keyed "sword":
      // that is the one a new character is seeded with, and these tests want a
      // character carrying nothing.
      std::map<std::string, EquipPrototype>{{"iron_sword", IronSword()}},
      std::map<std::string, Scroll>{}, std::map<std::string, ItemPrototype>{},
      TestMobs(), std::map<std::string, MapData>{}, SkillCatalog());
  // The same fight the server holds, since both ends have to mean the same
  // thing by its name.
  state->bosses = TestBosses();
  return state;
}

// One player: their character, their connection, and the controller they
// drive it from. Everything a party needs two of.
struct Client {
  explicit Client(const std::string& name, int port)
      : state(MakeState()), session("127.0.0.1", port) {
    state->character.SetUsername(name);
    while (state->character.proto().level() < UnlockLevel(Feature::kBoss)) {
      state->character.LevelUp();
    }
    Build();
  }

  void Build() {
    char_panel = std::make_unique<CharacterPanel>(
        state->character, state->account, focus, state->skills);
    equip_panel = std::make_unique<EquippedPanel>(state->character,
                                                  state->account, focus);
    inventory_panel = std::make_unique<InventoryPanel>(state->character,
                                                       state->account, focus);
    scroll_panel =
        std::make_unique<ScrollPanel>(state->character, state->scrolls);
    trace_recover_panel = std::make_unique<TraceRecoverPanel>(state->character);
    multi_sell_panel =
        std::make_unique<MultiSellPanel>(state->character, state->account);
    map_select_panel = std::make_unique<MapSelectPanel>(*state);
    mob_inspect_panel = std::make_unique<MobInspectPanel>(*state);
    boss_select_panel = std::make_unique<BossSelectPanel>(*state);
    player_inspect_panel = std::make_unique<PlayerInspectPanel>(*state);
    shop_panel = std::make_unique<ShopPanel>(state->character, state->equips,
                                             state->items);
    job_inspect_panel = std::make_unique<JobInspectPanel>(state->skills);
    menu_panel = std::make_unique<MenuPanel>(*state, analysis, focus);
    keys = std::make_unique<KeyMap>(state->account.mutable_keybinds());
    keybinds_panel = std::make_unique<KeybindsPanel>(*keys);
    options_panel = std::make_unique<OptionsPanel>(state->account);
    controller = std::make_unique<TuiController>(
        *state,
        Screens{*char_panel,         *equip_panel,       *inventory_panel,
                *scroll_panel,       inspect_panel,      preview_inspect_panel,
                star_force_panel,    cube_panel,         *trace_recover_panel,
                sell_panel,          sell_equip_panel,   *multi_sell_panel,
                *map_select_panel,   *mob_inspect_panel, *boss_select_panel,
                party_panel,         player_list_panel,  *player_inspect_panel,
                *shop_panel,         buy_panel,          *job_inspect_panel,
                skill_inspect_panel, buff_info_panel,    *menu_panel,
                *keybinds_panel,     *options_panel},
        analysis, *keys, focus, &session);
  }

  // One turn of the game's loop: the connection, then the screen, then
  // whatever fight the screen is showing.
  void Tick(double seconds = 0.0) {
    session.Advance(*state);
    controller->AdvanceParty();
    controller->AdvanceBossRun(seconds);
  }

  // Puts a weapon in their hands, without which there is nothing to swing.
  void Arm() {
    state->character.PickUp(
        std::make_unique<EquipInstance>(state->equips.at("iron_sword")));
    state->character.Equip(0);
  }

  std::unique_ptr<GameState> state;
  MultiplayerSession session;
  int focus = kCharPanel;
  BattleAnalysis analysis;
  PartySelectPanel party_panel;
  PlayerListPanel player_list_panel;
  StarForcePanel star_force_panel;
  CubePanel cube_panel;
  SellPanel sell_panel;
  SellEquipPanel sell_equip_panel;
  BuyPanel buy_panel;
  SkillInspectPanel skill_inspect_panel;
  BuffInfoPanel buff_info_panel;
  InspectPanel inspect_panel;
  InspectPanel preview_inspect_panel;
  std::unique_ptr<CharacterPanel> char_panel;
  std::unique_ptr<EquippedPanel> equip_panel;
  std::unique_ptr<InventoryPanel> inventory_panel;
  std::unique_ptr<ScrollPanel> scroll_panel;
  std::unique_ptr<TraceRecoverPanel> trace_recover_panel;
  std::unique_ptr<MultiSellPanel> multi_sell_panel;
  std::unique_ptr<MapSelectPanel> map_select_panel;
  std::unique_ptr<MobInspectPanel> mob_inspect_panel;
  std::unique_ptr<BossSelectPanel> boss_select_panel;
  std::unique_ptr<PlayerInspectPanel> player_inspect_panel;
  std::unique_ptr<ShopPanel> shop_panel;
  std::unique_ptr<JobInspectPanel> job_inspect_panel;
  std::unique_ptr<MenuPanel> menu_panel;
  std::unique_ptr<KeyMap> keys;
  std::unique_ptr<KeybindsPanel> keybinds_panel;
  std::unique_ptr<OptionsPanel> options_panel;
  std::unique_ptr<TuiController> controller;
};

class PartyControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(server_.Start());
  }

  std::unique_ptr<Client> Connect(const std::string& name) {
    std::unique_ptr<Client> client =
        std::make_unique<Client>(name, server_.port());
    client->session.Start(*client->state);
    EXPECT_TRUE(WaitFor({client.get()}, [&]() {
      return client->session.Snapshot().state == ConnectionState::kConnected;
    }));
    return client;
  }

  // Ticks every client until `ready`, so both ends of a party keep moving.
  // `seconds` is what each tick is worth to a fight on screen.
  //
  // One more tick once it holds: a connection fills its snapshot on its own
  // thread, so a condition read straight off one can come true after the tick
  // that would have handed it to the panels. Without the extra pass a screen
  // is a message behind what the test has just proved arrived.
  bool WaitFor(const std::vector<Client*>& clients,
               const std::function<bool()>& ready, double seconds = 0.0) {
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < deadline) {
      for (Client* client : clients) {
        client->Tick(seconds);
      }
      if (ready()) {
        for (Client* client : clients) {
          client->Tick(seconds);
        }
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

  // Walks the Multiplayer box to `entry` and presses Enter, the way a player
  // reaches either screen. Up off the menu row meets Party first, and Players
  // is the row above it.
  void OpenMultiplayer(Client& client, MultiplayerEntry entry) {
    client.controller->OpenMenuEntry(MenuEntry::kMultiplayer);
    client.controller->OnEvent(ftxui::Event::ArrowUp);
    if (entry == MultiplayerEntry::kPlayers) {
      client.controller->OnEvent(ftxui::Event::ArrowUp);
    }
    client.controller->OnEvent(ftxui::Event::Return);
  }

  // Opens the party screen the way a player does.
  void OpenParty(Client& client) {
    OpenMultiplayer(client, MultiplayerEntry::kParty);
    ASSERT_EQ(client.controller->screen(), kPartySelect);
  }

  // `leader` makes a party and `guest` joins it. Both are left on the party
  // screen with the cursor at the top of the list.
  void MakeParty(Client& leader, Client& guest) {
    OpenParty(leader);
    leader.controller->OnEvent(ftxui::Event::ArrowDown);
    ASSERT_EQ(leader.party_panel.Chosen(), PartyAction::kCreate);
    leader.controller->OnEvent(ftxui::Event::Return);
    ASSERT_TRUE(WaitFor({&leader, &guest},
                        [&]() { return leader.party_panel.in_party(); }));

    OpenParty(guest);
    // The new party has to reach this client's list before Enter can mean
    // "join": on a list still empty the cursor is on Create, and the guest
    // makes a party of their own instead. Wait for the stop, not for the
    // screen -- see the wait rule in party_test_wait_rule.
    ASSERT_TRUE(WaitFor({&leader, &guest}, [&]() {
      return guest.party_panel.Chosen() == PartyAction::kJoin;
    }));
    guest.controller->OnEvent(ftxui::Event::Return);
    ASSERT_TRUE(WaitFor({&leader, &guest}, [&]() {
      return guest.party_panel.in_party() &&
             leader.session.Snapshot().party.members_size() == 2;
    }));
  }

  // A party of two, armed and ready, with the leader standing on the boss
  // list. The three fight tests below all start here.
  void ReadyParty(Client& leader, Client& guest) {
    leader.Arm();
    guest.Arm();
    MakeParty(leader, guest);
    guest.session.client().SetReady(true);
    ASSERT_TRUE(WaitFor({&leader, &guest}, [&]() {
      const Party& party = leader.session.Snapshot().party;
      return party.members_size() == 2 && party.members(1).ready();
    }));
  }

  // The leader takes them in, and both land in the arena.
  void EnterTheFight(Client& leader, Client& guest) {
    leader.controller->OnEvent(ftxui::Event::Escape);
    leader.controller->OpenMenuEntry(MenuEntry::kBoss);
    leader.controller->OnEvent(ftxui::Event::Return);
    ASSERT_EQ(leader.controller->screen(), kBossConfirm);
    leader.controller->OnEvent(ftxui::Event::Return);
    // Both ends: the fight opens each screen off its own message.
    ASSERT_TRUE(WaitFor(
        {&leader, &guest},
        [&]() {
          return leader.controller->screen() == kBossFight &&
                 guest.controller->screen() == kBossFight;
        },
        0.02));
  }

  TestServer server_;
};

TEST_F(PartyControllerTest, MakesAPartyAndJoinsIt) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  EXPECT_TRUE(leader->party_panel.is_leader());
  EXPECT_FALSE(guest->party_panel.is_leader());
  EXPECT_EQ(guest->session.Snapshot().party.members(0).player().name(),
            "Dagger");
}

TEST_F(PartyControllerTest, AMemberSaysTheyAreReady) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // Down past the two members onto the buttons, where Ready leads.
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(guest->party_panel.Chosen(), PartyAction::kReady);
  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&]() { return guest->party_panel.ready(); }));

  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&]() { return !guest->party_panel.ready(); }));
}

TEST_F(PartyControllerTest, TheLeaderKicksAMember) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // Enter on the second member raises the menu, which opens on Inspect; Kick
  // is the entry under it.
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyMenu);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  EXPECT_NE(leader->controller->party_prompt_question().find("Wand"),
            std::string::npos);

  // The question opens on Cancel, so Confirm takes a step to reach.
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(leader->controller->screen(), kPartySelect);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&]() { return !guest->party_panel.in_party(); }));

  // The one removed is told so, wherever they were standing. The notice is a
  // message of its own, so it can land a tick behind the party it is about.
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->controller->party_notice_prompt().open();
  }));
  EXPECT_FALSE(guest->controller->party_notice().empty());
  EXPECT_FALSE(guest->controller->party_notice_is_refusal());
}

TEST_F(PartyControllerTest, CancellingAKickLeavesThePartyAlone) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  // The cursor is on Cancel, so Enter is the answer that changes nothing.
  leader->controller->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(leader->controller->screen(), kPartySelect);
  leader->Tick();
  guest->Tick();
  EXPECT_TRUE(guest->party_panel.in_party());
}

TEST_F(PartyControllerTest, TheLeaderHandsThePartyOn) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  // Down two entries of the menu, from Inspect past Kick to Promote.
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  EXPECT_NE(leader->controller->party_prompt_question().find("leader"),
            std::string::npos);
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);

  ASSERT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&]() { return guest->party_panel.is_leader(); }));
  // Its own wait: the two are told separately, so the guest having heard says
  // nothing about the leader having heard.
  EXPECT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&]() { return !leader->party_panel.is_leader(); }));
  EXPECT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->controller->party_notice_prompt().open();
  }));
}

TEST_F(PartyControllerTest, TheLeaderLeavesAndThePartyGoesOn) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // Down past both members onto Leave Party, which is where the leader's
  // buttons open.
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(leader->party_panel.Chosen(), PartyAction::kLeave);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);

  // Each end hears about it in a message of its own.
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->party_panel.is_leader() && !leader->party_panel.in_party();
  }));
}

TEST_F(PartyControllerTest, ANoticeTakesKeysWhereverThePlayerIs) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // The one being removed is looking at the main view, not the party screen:
  // once out of the screen, once out of the box that opened it.
  guest->controller->OnEvent(ftxui::Event::Escape);
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kMain);

  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->controller->party_notice_prompt().open();
  }));

  // Anything the player presses goes to the notice and no further.
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_TRUE(guest->controller->party_notice_prompt().open());
  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(guest->controller->party_notice_prompt().open());
  EXPECT_EQ(guest->controller->screen(), kMain);
}

// Both screens close back to the box that opened them, the way Keybinds and
// Options do. Landing on the main view instead left the box open with the
// cursor inside it, and the menu row drew no cursor while that held.
TEST_F(PartyControllerTest, ClosingEitherScreenGoesBackToTheBox) {
  std::unique_ptr<Client> player = Connect("Dagger");
  OpenMultiplayer(*player, MultiplayerEntry::kPlayers);
  ASSERT_EQ(player->controller->screen(), kPlayerList);

  player->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(player->controller->screen(), kMenuBox);
  EXPECT_TRUE(player->menu_panel->box_open());
  // Out of the box, which is what hands the cursor back to the menu row.
  player->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(player->controller->screen(), kMain);
  EXPECT_FALSE(player->menu_panel->box_open());
  EXPECT_LT(player->menu_panel->box_cursor(), 0);

  // The party screen closes the same way, by its Close button.
  OpenMultiplayer(*player, MultiplayerEntry::kParty);
  ASSERT_EQ(player->controller->screen(), kPartySelect);
  player->controller->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(player->party_panel.Chosen(), PartyAction::kClose);
  player->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(player->controller->screen(), kMenuBox);
}

// The Players list is everyone connected, party or no. A sheet is not on the
// roster, so Enter asks for one and the screen opens when it lands.
TEST_F(PartyControllerTest, APlayerInspectsSomebodyOutsideTheirParty) {
  std::unique_ptr<Client> reader = Connect("Dagger");
  std::unique_ptr<Client> read = Connect("Wand");
  read->state->character.LevelUp();

  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->session.Snapshot().online.players_size() == 2;
  }));
  OpenMultiplayer(*reader, MultiplayerEntry::kPlayers);
  ASSERT_EQ(reader->controller->screen(), kPlayerList);
  // In the order they arrived, the reader included.
  EXPECT_EQ(reader->player_list_panel.selected_name(), "Dagger");
  reader->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(reader->player_list_panel.selected_name(), "Wand");

  reader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->controller->screen() == kPlayerInspect;
  }));
  EXPECT_EQ(reader->player_inspect_panel->character().username(), "Wand");
  EXPECT_EQ(reader->player_inspect_panel->character().proto().level(),
            read->state->character.proto().level());

  // The watch stands while the screen is open, so a level taken under the
  // reader shows.
  read->state->character.LevelUp();
  EXPECT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->player_inspect_panel->character().proto().level() ==
           read->state->character.proto().level();
  }));

  reader->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(reader->controller->screen(), kPlayerList);
}

// There is nothing left to read once they have gone, so the screen closes
// rather than holding the sheet they last sent.
TEST_F(PartyControllerTest, TheInspectScreenClosesWhenThePlayerLeaves) {
  std::unique_ptr<Client> reader = Connect("Dagger");
  std::unique_ptr<Client> read = Connect("Wand");
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->session.Snapshot().online.players_size() == 2;
  }));
  OpenMultiplayer(*reader, MultiplayerEntry::kPlayers);
  reader->controller->OnEvent(ftxui::Event::ArrowDown);
  reader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->controller->screen() == kPlayerInspect;
  }));

  read.reset();
  EXPECT_TRUE(WaitFor({reader.get()}, [&]() {
    return reader->controller->screen() == kPlayerList;
  }));
}

// A member reads another member: their sheet arrives with the party state, so
// the screen has everything it needs the moment it opens.
TEST_F(PartyControllerTest, AMemberInspectsAnother) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // The leader picks up a weapon, which reaches the guest with the party
  // state rather than being asked for. Levelled into Hyper Stats first, so
  // the sheet carries the two allocations the screen reads between.
  while (leader->state->character.proto().level() < kHyperStatUnlockLevel) {
    leader->state->character.LevelUp();
  }
  // Their own switch, which is what says the screen has two to read between.
  // Thrown before the gear, which is what sends the sheet.
  leader->state->account.SetAutoswapPresets(true);
  leader->state->ApplyPresetOptions();
  leader->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  leader->state->character.Equip(0);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->party_panel.in_party() && PresetOf(guest->session.Snapshot()
                                                         .party.members(0)
                                                         .player()
                                                         .sheet()
                                                         .equip_presets(),
                                                     StatPreset::kFirst)
                                                    .equipped()
                                                    .size() == 1;
  }));

  // The guest, who leads nothing, on the leader's row: Inspect is the entry
  // the menu opens on.
  ASSERT_FALSE(guest->party_panel.is_leader());
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPartyMenu);
  ASSERT_EQ(guest->party_panel.menu_selected(), kPartyMenuInspect);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  // The leader as they read themselves, rebuilt from the sheet they sent.
  EXPECT_EQ(guest->player_inspect_panel->character().username(), "Dagger");
  EXPECT_EQ(guest->player_inspect_panel->character().proto().level(),
            leader->state->character.proto().level());

  // Tab moves to their Character panel, where the Farm/Boss row reads between
  // their two Hyper Stat allocations.
  EXPECT_EQ(guest->player_inspect_panel->preset(), Activity::kFarming);
  guest->controller->OnEvent(ftxui::Event::Tab);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(guest->player_inspect_panel->preset(), Activity::kBossing);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(guest->player_inspect_panel->preset(), Activity::kFarming);
  guest->controller->OnEvent(ftxui::Event::Tab);

  // Enter on a worn item opens its card, off the panel's cursor rather than a
  // pointer held across a tick that may rebuild the member.
  ASSERT_NE(guest->player_inspect_panel->selected_item(), nullptr);
  EXPECT_EQ(guest->player_inspect_panel->selected_item()->prototype().name(),
            "Iron Sword");
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerItemInspect);
  guest->Tick();
  EXPECT_EQ(guest->controller->screen(), kPlayerItemInspect);
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  guest->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(guest->controller->screen(), kPartySelect);
}

// The cards the Inspect screen raises are the player's own over somebody
// else's numbers, and each one closes back onto the screen that raised it.
TEST_F(PartyControllerTest, TheInspectScreenRaisesTheMembersOwnCards) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);
  leader->state->character.AdvanceJob(JOB_SWORDMAN);
  const Skill& strike = leader->state->skills.at("power_strike");
  ASSERT_TRUE(leader->state->character.LearnSkill(strike, 3));
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&guest]() { return guest->party_panel.in_party(); }));

  // The guest opens the leader, then walks to their Character panel.
  guest->controller->OnEvent(ftxui::Event::Return);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&guest]() {
    return guest->player_inspect_panel->character().skill_level(
               guest->state->skills.at("power_strike")) == 3;
  })) << "the leader's learned skill never reached the reader";
  guest->controller->OnEvent(ftxui::Event::Tab);

  // View All Stats: their sheet, on the screen their own last stats row opens.
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(guest->controller->screen(), kPlayerAllStats);
  // It stays there through the frames that follow. The ticker's redraw is an
  // event like any other, and a screen closing on anything it did not
  // recognise was gone before the player had read it.
  guest->controller->OnEvent(ftxui::Event::Custom);
  guest->Tick();
  ASSERT_EQ(guest->controller->screen(), kPlayerAllStats)
      << "a repaint closed the screen";
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  // Their Skills tab, and the card a row opens: the LEADER's level in it, not
  // the reader's own.
  guest->controller->OnEvent(ftxui::Event::ArrowUp);  // -> the tab bar
  guest->controller->OnEvent(ftxui::Event::ArrowRight);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);  // -> the page bar
  guest->controller->OnEvent(ftxui::Event::ArrowDown);  // -> the first skill
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kSkillInspect);
  EXPECT_EQ(guest->controller->skill_inspect_skill().name(), "Power Strike");
  EXPECT_EQ(guest->controller->skill_inspect_level(), 3);
  EXPECT_EQ(guest->state->character.skill_level(strike), 0)
      << "the reader has never learned it, so 3 is the member's";
  guest->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(guest->controller->screen(), kPlayerInspect);
}

// The Equipped panel opens up the way the player's own does, and Escape
// closes that before it closes the screen.
TEST_F(PartyControllerTest, TheInspectScreensEquippedPanelExpands) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);
  leader->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  leader->state->character.Equip(0);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&guest]() { return guest->party_panel.in_party(); }));

  guest->controller->OnEvent(ftxui::Event::Return);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  // Up off the list is the tab bar, and Left off the first tab is Expand.
  guest->controller->OnEvent(ftxui::Event::ArrowUp);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(guest->player_inspect_panel->expanded());

  guest->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(guest->player_inspect_panel->expanded());
  EXPECT_EQ(guest->controller->screen(), kPlayerInspect)
      << "Escape closed the screen rather than the panel";
  guest->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(guest->controller->screen(), kPartySelect);
}

// The screen has nothing to draw once the member walks out of the party.
TEST_F(PartyControllerTest, TheMemberLeavingClosesTheInspectScreen) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPlayerInspect);
  EXPECT_EQ(leader->player_inspect_panel->character().username(), "Wand");

  // The guest walks out: down past both members onto Leave Party, then Yes.
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(guest->party_panel.Chosen(), PartyAction::kLeave);
  guest->controller->OnEvent(ftxui::Event::Return);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return leader->controller->screen() == kPartySelect;
  }));
}

TEST_F(PartyControllerTest, AMemberCannotTakeAFightOfTheirOwn) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);
  guest->controller->OnEvent(ftxui::Event::Escape);

  guest->controller->OpenMenuEntry(MenuEntry::kBoss);
  ASSERT_EQ(guest->controller->screen(), kBossSelect);
  guest->controller->OnEvent(ftxui::Event::Return);

  ASSERT_EQ(guest->controller->screen(), kBossNotice);
  ASSERT_EQ(guest->controller->notice_lines().size(), 1u);
  EXPECT_EQ(guest->controller->notice_lines()[0], "You are not the leader.");
  EXPECT_TRUE(guest->controller->notice_is_refusal());

  // The leader is refused for whatever else is wrong -- these characters
  // carry no weapon -- but never for whose party it is.
  leader->controller->OnEvent(ftxui::Event::Escape);
  leader->controller->OpenMenuEntry(MenuEntry::kBoss);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->notice_lines().size(), 1u);
  EXPECT_EQ(leader->controller->notice_lines()[0],
            "You have no weapon equipped!");
}

// The whole flow: the leader picks a fight, the server checks the party, and
// everybody lands in the arena together.
TEST_F(PartyControllerTest, TheLeaderTakesThePartyIntoAFight) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  ReadyParty(*leader, *guest);

  // Both of them, wherever they were: the guest never left the party screen.
  EnterTheFight(*leader, *guest);
  ASSERT_NE(guest->controller->boss_run(), nullptr);
  const std::vector<FightMember>& members =
      guest->controller->boss_run()->members();
  ASSERT_EQ(members.size(), 2u);
  // This player is not named on their own screen, and the other one is.
  EXPECT_TRUE(members[0].name.empty());
  EXPECT_EQ(members[1].name, "Dagger");
  EXPECT_EQ(guest->controller->boss_run()->share_count(), 2);

  // What the leader lands is drawn on the guest's screen, faint and theirs.
  ASSERT_TRUE(WaitFor(
      {leader.get(), guest.get()},
      [&]() {
        for (const DamageStack& stack :
             guest->controller->boss_run()->damage_stacks()) {
          if (stack.owner != 0) {
            return true;
          }
        }
        return false;
      },
      0.02));

  // And the monster they are both hitting is one monster.
  EXPECT_LT(guest->controller->boss_run()->phase_hp_fraction(), 1.0);
}

// The rest of the party is seated in the GameState for the length of the
// fight, so that what their skills hold over this character is folded into its
// stats -- and taken out again when the fight is done. See GameState::party.
TEST_F(PartyControllerTest, ThePartyIsSeatedForTheFightAndNoLonger) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  ReadyParty(*leader, *guest);
  // Standing in a party is not being in a fight, and buys nothing yet.
  EXPECT_TRUE(guest->state->party.empty());

  // Each screen seats its party as the fight opens it.
  EnterTheFight(*leader, *guest);

  // Everybody but themselves, rebuilt from the sheets they sent.
  ASSERT_EQ(guest->state->party.size(), 1u);
  EXPECT_EQ(guest->state->party[0].username(), "Dagger");
  ASSERT_EQ(leader->state->party.size(), 1u);
  EXPECT_EQ(leader->state->party[0].username(), "Wand");

  guest->controller->OnEvent(ftxui::Event::Escape);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor(
      {leader.get(), guest.get()},
      [&]() { return guest->controller->screen() == kBossSelect; }, 0.02));
  EXPECT_TRUE(guest->state->party.empty());
}

// Walking out leaves the fight to whoever is left in it.
TEST_F(PartyControllerTest, OneWalkingOutLeavesTheFightToTheOther) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  ReadyParty(*leader, *guest);
  EnterTheFight(*leader, *guest);

  // Escape raises the question, and the prompt opens on Cancel.
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kBossAbort);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  guest->controller->OnEvent(ftxui::Event::Return);

  ASSERT_TRUE(WaitFor(
      {leader.get(), guest.get()},
      [&]() { return guest->controller->screen() == kBossSelect; }, 0.02));
  EXPECT_EQ(guest->controller->boss_run(), nullptr);
  // The one still in it fights on, and the other's panel goes.
  ASSERT_NE(leader->controller->boss_run(), nullptr);
  EXPECT_EQ(leader->controller->screen(), kBossFight);
  ASSERT_TRUE(WaitFor(
      {leader.get(), guest.get()},
      [&]() { return leader->controller->boss_run()->members().size() == 1; },
      0.02));
}

TEST_F(PartyControllerTest, LosingTheServerClosesThePartyScreen) {
  std::unique_ptr<Client> client = Connect("Dagger");
  OpenParty(*client);

  server_.Stop();
  ASSERT_TRUE(WaitFor({client.get()}, [&]() {
    return client->controller->party_notice_prompt().open();
  }));

  // Turned out of the screen, so closing the notice lands somewhere real.
  EXPECT_EQ(client->controller->screen(), kMain);
  EXPECT_TRUE(client->controller->party_notice_is_refusal());
}

TEST_F(PartyControllerTest, TheEntrySaysSoWhenThereIsNoConnection) {
  Client client("Dagger", server_.port());
  // Never started, so there is no connection to open a lobby with.
  OpenMultiplayer(client, MultiplayerEntry::kParty);

  EXPECT_EQ(client.controller->screen(), kMain);
  EXPECT_TRUE(client.controller->party_notice_prompt().open());
  EXPECT_TRUE(client.controller->party_notice_is_refusal());
  EXPECT_FALSE(client.controller->party_notice().empty());
}

TEST_F(PartyControllerTest, PartyOpensOnceTheServerCatchesUp) {
  // The server is the end that is behind, which is what a deploy left undone
  // looks like from a player's seat.
  TestServer behind(TestBosses(), TestMobs(), kMultiplayerVersion - 1);
  ASSERT_TRUE(behind.Start());
  Client player("Dagger", behind.port());
  player.session.Start(*player.state);
  ASSERT_TRUE(WaitFor({&player}, [&]() {
    return player.session.Snapshot().state == ConnectionState::kUnavailable;
  }));

  OpenMultiplayer(player, MultiplayerEntry::kParty);
  ASSERT_NE(player.controller->screen(), kPartySelect);
  EXPECT_EQ(player.controller->party_notice(),
            "The server is running an older version. Trying again.\nClient: v" +
                std::to_string(kMultiplayerVersion) + ", Server: v" +
                std::to_string(kMultiplayerVersion - 1));

  // The server is deployed. Pressing Party again is what asks the connection
  // to try now rather than at the end of its backoff, so the player gets in
  // without restarting the game.
  ASSERT_TRUE(behind.RestartSpeaking(kMultiplayerVersion));
  OpenMultiplayer(player, MultiplayerEntry::kParty);
  ASSERT_TRUE(WaitFor({&player}, [&]() {
    return player.session.Snapshot().state == ConnectionState::kConnected;
  }));
  OpenParty(player);
}

}  // namespace
}  // namespace ms
