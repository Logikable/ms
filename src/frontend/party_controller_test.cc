// The controller's party screens, tested against a real server on loopback.
// Everything here needs both ends of a connection: the screen sends a request
// and draws the reply.

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include "ftxui/component/event.hpp"
#include "server/test_server.h"
#include "src/character/equip_presets.h"
#include "src/character/hyper_stats.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/combat/boss_run.h"
#include "src/combat/boss_timing.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/player_inspect_panel.h"
#include "src/frontend/screens/player_list_panel.h"
#include "src/frontend/screens/trade_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/multiplayer/session.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// How long a round trip may take before the test gives up. It is generous:
// passing tests never wait this long, and the suite runs these alongside many
// other targets.
constexpr std::chrono::milliseconds kPatience(15000);

// One skill in the first Warrior book, so a member's Skills tab has a row to
// open a card from.
std::map<std::string, Skill> SkillCatalog() {
  Skill strike;
  strike.set_name("Power Strike");
  PlaceIn(strike, JOB_ADVANCEMENT_SWORDMAN);
  strike.set_max_level(20);
  return {{"power_strike", strike}};
}

// One stack in the catalog, for the same reason as the weapon: items are sent
// by name, and both ends look them up in their own catalogs.
ItemPrototype TestStack() {
  ItemPrototype proto;
  proto.set_name("Chaos Scroll");
  proto.set_max_stack(100);
  return proto;
}

std::unique_ptr<GameState> MakeState() {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      // One weapon in the catalog, so a member has something to be seen
      // wearing. A sheet names its items and the reader looks them up in their
      // own catalogs, so both ends need it. Not keyed "sword", because a new
      // character starts with that, and these tests want a character with
      // nothing.
      std::map<std::string, EquipPrototype>{{"iron_sword", IronSword()}},
      std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{{"chaos_scroll", TestStack()}},
      TestMobs(), std::map<std::string, MapData>{}, SkillCatalog());
  // The same fight the server has, since both ends must mean the same thing by
  // its name.
  state->bosses = TestBosses();
  return state;
}

// One player: their character, their connection, and their controller.
// Everything a party needs two of.
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
    trade_panel =
        std::make_unique<TradePanel>(state->character, state->account);
    shop_panel = std::make_unique<ShopPanel>(state->character, state->equips,
                                             state->items);
    bank_panel = std::make_unique<BankPanel>(state->character, state->account,
                                             state->items);
    link_skill_panel =
        std::make_unique<LinkSkillPanel>(state->character, state->skills);
    job_inspect_panel = std::make_unique<JobInspectPanel>(state->skills);
    menu_panel = std::make_unique<MenuPanel>(*state, analysis, focus);
    keys = std::make_unique<KeyMap>(state->account.mutable_keybinds());
    keybinds_panel = std::make_unique<KeybindsPanel>(*keys);
    options_panel = std::make_unique<OptionsPanel>(state->account);
    jukebox_panel =
        std::make_unique<JukeboxPanel>(*state, music_director, state->account);
    controller = std::make_unique<TuiController>(
        *state, Screens{*char_panel,           *equip_panel,
                        *inventory_panel,      *scroll_panel,
                        inspect_panel,         preview_inspect_panel,
                        star_force_panel,      cube_panel,
                        *trace_recover_panel,  sell_panel,
                        sell_equip_panel,      *multi_sell_panel,
                        *map_select_panel,     *mob_inspect_panel,
                        *boss_select_panel,    party_panel,
                        player_list_panel,     *trade_panel,
                        *player_inspect_panel, player_item_panel,
                        *shop_panel,           buy_panel,
                        *bank_panel,           *link_skill_panel,
                        *job_inspect_panel,    skill_inspect_panel,
                        buff_info_panel,       *menu_panel,
                        *keybinds_panel,       *options_panel,
                        *jukebox_panel},
        analysis, *keys, focus, &session);
  }

  // One turn of the game loop: the connection, then the screen, then any fight
  // on screen.
  void Tick(double seconds = 0.0) {
    session.Advance(*state);
    controller->AdvanceParty();
    controller->AdvanceBossRun(seconds);
  }

  // Gives them a weapon, without which they can't attack.
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
  InspectPanel player_item_panel;
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
  std::unique_ptr<BankPanel> bank_panel;
  std::unique_ptr<LinkSkillPanel> link_skill_panel;
  std::unique_ptr<TradePanel> trade_panel;
  std::unique_ptr<ShopPanel> shop_panel;
  std::unique_ptr<JobInspectPanel> job_inspect_panel;
  std::unique_ptr<MenuPanel> menu_panel;
  std::unique_ptr<KeyMap> keys;
  std::unique_ptr<KeybindsPanel> keybinds_panel;
  std::unique_ptr<OptionsPanel> options_panel;
  MusicPlayer music_player{MusicPlayer::Backend::kNull};
  std::mt19937 music_rng{1};
  MusicDirector music_director{music_player, music_rng};
  std::unique_ptr<JukeboxPanel> jukebox_panel;
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

  // Ticks every client until `ready`, so both ends of a party keep running.
  // `seconds` is how long each tick counts as in a fight on screen.
  //
  // It ticks once more after `ready` holds: a connection fills its snapshot on
  // its own thread, so a condition can become true after the tick that would
  // have passed it to the panels. Without the extra tick the screen would be
  // one message behind what the test just confirmed arrived.
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

  // Moves through the Multiplayer box to `entry` and presses Enter, the way a
  // player opens either screen. Up from the menu row reaches Party first, and
  // Players is the row above it.
  void OpenMultiplayer(Client& client, MultiplayerEntry entry) {
    client.controller->OpenMenuEntry(MenuEntry::kMultiplayer);
    client.controller->OnEvent(ftxui::Event::ArrowUp);
    if (entry == MultiplayerEntry::kPlayers) {
      client.controller->OnEvent(ftxui::Event::ArrowUp);
    }
    client.controller->OnEvent(ftxui::Event::Return);
  }

  // Presses Trade the way a player does, for both players: the Players list,
  // down to the other player, Enter for the menu, Down to Trade. Both asking is
  // what opens the screen for both.
  void OpenTrade(Client& asker, Client& asked) {
    AskToTrade(asker, asked.state->character.username());
    ASSERT_TRUE(WaitFor({&asker, &asked}, [&]() {
      return asker.controller->screen() == kTrade;
    }));
    AskToTrade(asked, asker.state->character.username());
    ASSERT_TRUE(WaitFor({&asker, &asked}, [&]() {
      return asked.controller->screen() == kTrade &&
             asker.session.Snapshot().trade.partner_joined();
    }));
  }

  // Back to your own window, right along its top row to the Accept button, and
  // press it.
  void PressAccept(Client& client) {
    ASSERT_EQ(client.controller->screen(), kTrade);
    while (client.trade_panel->zone() != TradeZone::kMine) {
      client.controller->OnEvent(ftxui::Event::Tab);
    }
    while (client.trade_panel->cursor().kind != TradeCursor::Kind::kAccept) {
      client.controller->OnEvent(ftxui::Event::ArrowRight);
    }
    client.controller->OnEvent(ftxui::Event::Return);
  }

  // Tab to the bag and offer its first equip.
  void OfferFirstBagItem(Client& client) {
    while (client.trade_panel->zone() != TradeZone::kBag) {
      client.controller->OnEvent(ftxui::Event::Tab);
    }
    client.controller->OnEvent(ftxui::Event::Return);
    ASSERT_EQ(client.controller->screen(), kTradeMenu);
    client.controller->OnEvent(ftxui::Event::ArrowDown);
    ASSERT_EQ(client.trade_panel->menu_selected(), kTradeMenuOffer);
    client.controller->OnEvent(ftxui::Event::Return);
  }

  void AskToTrade(Client& client, const std::string& name) {
    OpenMultiplayer(client, MultiplayerEntry::kPlayers);
    for (int step = 0; step < 4; ++step) {
      if (client.player_list_panel.selected_name() == name) {
        break;
      }
      client.controller->OnEvent(ftxui::Event::ArrowDown);
    }
    ASSERT_EQ(client.player_list_panel.selected_name(), name);
    client.controller->OnEvent(ftxui::Event::Return);
    ASSERT_EQ(client.controller->screen(), kPlayerMenu);
    client.controller->OnEvent(ftxui::Event::ArrowDown);
    ASSERT_EQ(client.player_list_panel.menu_selected(), kPlayerMenuTrade);
    client.controller->OnEvent(ftxui::Event::Return);
  }

  // Opens the party screen the way a player does.
  void OpenParty(Client& client) {
    OpenMultiplayer(client, MultiplayerEntry::kParty);
    ASSERT_EQ(client.controller->screen(), kPartySelect);
  }

  // `leader` creates a party and `guest` joins it. Both are left on the party
  // screen with the cursor at the top of the list.
  void MakeParty(Client& leader, Client& guest) {
    OpenParty(leader);
    leader.controller->OnEvent(ftxui::Event::ArrowDown);
    ASSERT_EQ(leader.party_panel.Chosen(), PartyAction::kCreate);
    leader.controller->OnEvent(ftxui::Event::Return);
    ASSERT_TRUE(WaitFor({&leader, &guest},
                        [&]() { return leader.party_panel.in_party(); }));

    OpenParty(guest);
    // The new party must reach this client's list before Enter means "join":
    // with an empty list the cursor is on Create, and the guest would create
    // their own party. Wait on the condition, not the screen; see WaitFor.
    ASSERT_TRUE(WaitFor({&leader, &guest}, [&]() {
      return guest.party_panel.Chosen() == PartyAction::kJoin;
    }));
    guest.controller->OnEvent(ftxui::Event::Return);
    ASSERT_TRUE(WaitFor({&leader, &guest}, [&]() {
      return guest.party_panel.in_party() &&
             leader.session.Snapshot().party.members_size() == 2;
    }));
  }

  // A party of two, armed, with the leader on the boss list. The three fight
  // tests below start here.
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

  // The leader starts the fight, and both arrive in the arena.
  void EnterTheFight(Client& leader, Client& guest) {
    leader.controller->OnEvent(ftxui::Event::Escape);
    leader.controller->OpenMenuEntry(MenuEntry::kBoss);
    leader.controller->OnEvent(ftxui::Event::Return);
    ASSERT_EQ(leader.controller->screen(), kBossConfirm);
    leader.controller->OnEvent(ftxui::Event::Return);
    // Both ends: each screen opens the fight from its own message.
    ASSERT_TRUE(WaitFor(
        {&leader, &guest},
        [&]() {
          return leader.controller->screen() == kBossFight &&
                 guest.controller->screen() == kBossFight;
        },
        0.02));
    // Both screens are open on the countdown, which is three real seconds. The
    // server times it, so ticking here can't speed it up.
    server_.SkipAhead(std::chrono::milliseconds(
        static_cast<int>(1000 * kBossCountdownSeconds) + 100));
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

  // Down past the two members to the buttons, where Ready is first.
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

  // Enter on the second member opens the menu on Inspect; Kick is two entries
  // below, after Trade.
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyMenu);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  EXPECT_NE(leader->controller->party_prompt_question().find("Wand"),
            std::string::npos);

  // The confirmation starts on Cancel, so reaching Confirm takes a step.
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(leader->controller->screen(), kPartySelect);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&]() { return !guest->party_panel.in_party(); }));

  // The removed player is told, wherever they are. The notice is a separate
  // message, so it may arrive a tick after the party update.
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
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  // The cursor is on Cancel, so Enter changes nothing.
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
  // Down three entries of the menu, from Inspect past Trade and Kick to
  // Promote.
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
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
  // A separate wait: the two are told separately, so the guest having heard
  // says nothing about the leader.
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

  // Down past both members to Leave Party, where the leader's buttons start.
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(leader->party_panel.Chosen(), PartyAction::kLeave);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPartyConfirm);
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);

  // Each end hears about it in its own message.
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->party_panel.is_leader() && !leader->party_panel.in_party();
  }));
}

// Resuming keeps the party; switching to another character leaves it.
TEST_F(PartyControllerTest, SwitchingCharacterLeavesTheParty) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  CharacterSave other;
  other.mutable_character()->set_name("Other");
  other.mutable_character()->set_level(1);
  guest->state->inactive_characters.push_back(other);
  MakeParty(*leader, *guest);

  guest->controller->OpenMenuEntry(MenuEntry::kCharacters);
  ASSERT_EQ(guest->controller->screen(), kCharacterSelect);
  guest->controller->OnEvent(ftxui::Event::Return);  // Wand's menu
  guest->controller->OnEvent(ftxui::Event::Return);  // Play: a resume
  ASSERT_EQ(guest->controller->screen(), kMain);
  leader->Tick();
  guest->Tick();
  EXPECT_TRUE(guest->party_panel.in_party());

  guest->controller->OpenMenuEntry(MenuEntry::kCharacters);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);  // onto Other
  guest->controller->OnEvent(ftxui::Event::Return);
  guest->controller->OnEvent(ftxui::Event::Return);  // Play
  ASSERT_EQ(guest->state->character.username(), "Other");
  EXPECT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return !guest->party_panel.in_party() &&
           leader->session.Snapshot().party.members_size() == 1;
  }));
}

TEST_F(PartyControllerTest, ANoticeTakesKeysWhereverThePlayerIs) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // The removed player is on the main view, not the party screen: once out of
  // the screen, and once out of the box that opened it.
  guest->controller->OnEvent(ftxui::Event::Escape);
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kMain);

  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::ArrowLeft);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&]() {
    return guest->controller->party_notice_prompt().open();
  }));

  // Any key goes to the notice and nowhere else.
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_TRUE(guest->controller->party_notice_prompt().open());
  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(guest->controller->party_notice_prompt().open());
  EXPECT_EQ(guest->controller->screen(), kMain);
}

// Both screens return to the box that opened them, like Keybinds and Options.
// Going to the main view instead left the box open with the cursor inside it,
// and the menu row drew no cursor while that was the case.
TEST_F(PartyControllerTest, ClosingEitherScreenGoesBackToTheBox) {
  std::unique_ptr<Client> player = Connect("Dagger");
  OpenMultiplayer(*player, MultiplayerEntry::kPlayers);
  ASSERT_EQ(player->controller->screen(), kPlayerList);

  player->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(player->controller->screen(), kMenuBox);
  EXPECT_TRUE(player->menu_panel->box_open());
  // Out of the box, which gives the cursor back to the menu row.
  player->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(player->controller->screen(), kMain);
  EXPECT_FALSE(player->menu_panel->box_open());
  EXPECT_LT(player->menu_panel->box_cursor(), 0);

  // The party screen closes the same way, with its Close button.
  OpenMultiplayer(*player, MultiplayerEntry::kParty);
  ASSERT_EQ(player->controller->screen(), kPartySelect);
  player->controller->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(player->party_panel.Chosen(), PartyAction::kClose);
  player->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(player->controller->screen(), kMenuBox);
}

// The Players list shows everyone connected, in a party or not. Their sheet
// isn't in the roster, so Enter requests it and the screen opens when it
// arrives.
TEST_F(PartyControllerTest, APlayerInspectsSomebodyOutsideTheirParty) {
  std::unique_ptr<Client> reader = Connect("Dagger");
  std::unique_ptr<Client> read = Connect("Wand");
  read->state->character.LevelUp();

  // Wait until the server has the level-up too: a sheet requested before then
  // is the old one.
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    const OnlinePlayers& online = reader->session.Snapshot().online;
    return online.players_size() == 2 &&
           online.players(1).level() == read->state->character.proto().level();
  }));
  OpenMultiplayer(*reader, MultiplayerEntry::kPlayers);
  ASSERT_EQ(reader->controller->screen(), kPlayerList);
  // In the order they connected, the reader included.
  EXPECT_EQ(reader->player_list_panel.selected_name(), "Dagger");
  reader->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(reader->player_list_panel.selected_name(), "Wand");

  // Enter opens the menu on them, starting on Inspect.
  reader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(reader->controller->screen(), kPlayerMenu);
  reader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->controller->screen() == kPlayerInspect;
  }));
  EXPECT_EQ(reader->player_inspect_panel->character().username(), "Wand");
  EXPECT_EQ(reader->player_inspect_panel->character().proto().level(),
            read->state->character.proto().level());

  // The request stays active while the screen is open, so a level gained by the
  // inspected player shows up.
  read->state->character.LevelUp();
  EXPECT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->player_inspect_panel->character().proto().level() ==
           read->state->character.proto().level();
  }));

  reader->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(reader->controller->screen(), kPlayerList);
}

// Trade is on both menus, and it sends a request: the server decides when the
// asker's screen opens.
TEST_F(PartyControllerTest, TradeAsksTheOtherPlayer) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  AskToTrade(*asker, "Wand");
  EXPECT_EQ(asker->controller->screen(), kPlayerList);

  // The asker has a trade; the other player gets a notification, wherever they
  // are.
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return !asker->session.Snapshot().trade.id().empty();
  }));
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->controller->notification().visible();
  }));
}

// The whole flow: one asks, the other asks back, and both are in the same
// trade.
TEST_F(PartyControllerTest, BothPlayersLandOnTheTradeScreen) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  asker->state->character.AddMeso(5000);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));

  AskToTrade(*asker, "Wand");
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()},
                      [&]() { return asker->controller->screen() == kTrade; }));
  // The other window is still unnamed: the asker is alone on the screen.
  EXPECT_FALSE(asker->session.Snapshot().trade.partner_joined());
  EXPECT_NE(asked->controller->screen(), kTrade);

  AskToTrade(*asked, "Dagger");
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->controller->screen() == kTrade &&
           asker->session.Snapshot().trade.partner_joined();
  }));

  // Enter on the meso opens the overlay; type the digits, and Down reaches
  // Confirm.
  asker->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(asker->controller->screen(), kTradeAmount);
  for (char digit : std::string("5000")) {
    asker->controller->OnEvent(ftxui::Event::Character(digit));
  }
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  asker->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(asker->controller->screen(), kTrade);
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->session.Snapshot().trade.theirs().meso() == 5000;
  }));

  // Leaving asks first: Escape is one key away from everywhere, and leaving
  // ends the trade for both.
  asker->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(asker->controller->screen(), kTradeLeave);
  asker->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(asker->controller->screen(), kTrade)
      << "and can be thought better of";

  asker->controller->OnEvent(ftxui::Event::Escape);
  asker->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(asker->controller->screen(), kPlayerList);
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->controller->screen() == kPlayerList;
  }));
  EXPECT_TRUE(asked->controller->party_notice_prompt().open());
  // The screen stays closed: a state update still in flight doesn't reopen it.
  EXPECT_EQ(asker->controller->screen(), kPlayerList);
}

// An item offered from the bag arrives complete, and the bag below stops
// showing what is offered.
TEST_F(PartyControllerTest, AnItemGoesUpFromTheBag) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  Equip starred;
  starred.set_equip_name(IronSword().name());
  starred.set_stars(3);
  asker->state->character.PickUp(
      std::make_unique<EquipInstance>(IronSword(), starred));
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  OpenTrade(*asker, *asked);

  // Tab to the bag, Enter for the menu, Down to Offer.
  asker->controller->OnEvent(ftxui::Event::Tab);
  asker->controller->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(asker->trade_panel->zone(), TradeZone::kBag);
  asker->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(asker->controller->screen(), kTradeMenu);
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(asker->trade_panel->menu_selected(), kTradeMenuOffer);
  asker->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(asker->controller->screen(), kTrade);

  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->session.Snapshot().trade.theirs().equips_size() == 1;
  }));
  // Copied, because the snapshot it comes from is returned by value.
  Equip crossed = asked->session.Snapshot().trade.theirs().equips(0);
  EXPECT_EQ(crossed.equip_name(), IronSword().name());
  EXPECT_EQ(crossed.stars(), 3) << "the drop crosses, not the prototype";

  // It is hidden from the bag while offered, and back when withdrawn.
  EXPECT_EQ(asker->trade_panel->cursor().kind, TradeCursor::Kind::kNothing);
  asker->controller->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(asker->trade_panel->zone(), TradeZone::kMine);
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  asker->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(asker->controller->screen(), kTradeMenu);
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(asker->trade_panel->menu_selected(), kTradeMenuRemove);
  asker->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(asker->trade_panel->own().items(), 0);
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->session.Snapshot().trade.theirs().equips_size() == 0;
  }));
}

// A stack is offered through an amount dialog, and Inspect opens a card on a
// row in any of the three windows: the bag, your offer, and theirs.
TEST_F(PartyControllerTest, AStackGoesUpAndEveryRowInspects) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  asker->Arm();
  asker->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  asker->state->character.AddItem(TestStack(), 12);
  asked->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  OpenTrade(*asker, *asked);
  OfferFirstBagItem(*asked);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().trade.theirs().equips_size() == 1;
  }));

  // The bag's sword, compared against the one in hand.
  while (asker->trade_panel->zone() != TradeZone::kBag) {
    asker->controller->OnEvent(ftxui::Event::Tab);
  }
  asker->controller->OnEvent(ftxui::Event::Return);
  asker->controller->OnEvent(ftxui::Event::Return);  // Inspect
  ASSERT_EQ(asker->controller->screen(), kTradeInspect);
  ASSERT_NE(asker->controller->trade_inspect_equip(), nullptr);
  EXPECT_EQ(asker->controller->trade_inspect_equip()->name(),
            IronSword().name());
  InspectPanel::ComparisonSlots slots = asker->controller->comparison_slots();
  ASSERT_EQ(slots.worn.size(), 1u);
  EXPECT_NE(slots.worn[0], nullptr);
  asker->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(asker->controller->screen(), kTrade);

  // Over to the Etc tab: Escape on the amount offers nothing, and Confirm
  // offers what was typed.
  asker->controller->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_TRUE(asker->trade_panel->on_etc_tab());
  asker->controller->OnEvent(ftxui::Event::Return);
  asker->controller->OnEvent(ftxui::Event::ArrowDown);  // Inspect -> Offer
  asker->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(asker->controller->screen(), kTradeItemAmount);
  asker->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(asker->controller->screen(), kTrade);
  EXPECT_EQ(asker->trade_panel->own().items(), 0);

  asker->controller->OnEvent(ftxui::Event::Return);
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  asker->controller->OnEvent(ftxui::Event::Return);
  asker->controller->OnEvent(ftxui::Event::Character('5'));
  asker->controller->OnEvent(ftxui::Event::ArrowDown);  // -> Confirm
  asker->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(asker->controller->screen(), kTrade);
  ASSERT_EQ(asker->trade_panel->own().stacks.size(), 1u);
  EXPECT_EQ(asker->trade_panel->own().stacks[0].count(), 5);
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    TradeOffer theirs = asked->session.Snapshot().trade.theirs();
    return theirs.stacks_size() == 1 && theirs.stacks(0).count() == 5;
  }));

  // The stack on your own side of the trade.
  while (asker->trade_panel->zone() != TradeZone::kMine) {
    asker->controller->OnEvent(ftxui::Event::Tab);
  }
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  asker->controller->OnEvent(ftxui::Event::Return);
  asker->controller->OnEvent(ftxui::Event::Return);  // Inspect
  ASSERT_EQ(asker->controller->screen(), kTradeInspect);
  ASSERT_NE(asker->controller->trade_inspect_stack(), nullptr);
  EXPECT_EQ(asker->controller->trade_inspect_stack()->name(),
            TestStack().name());
  EXPECT_EQ(asker->controller->trade_inspect_equip(), nullptr);
  asker->controller->OnEvent(ftxui::Event::Escape);

  // And their sword, rebuilt from the snapshot.
  asker->controller->OnEvent(ftxui::Event::Tab);
  ASSERT_EQ(asker->trade_panel->zone(), TradeZone::kTheirs);
  asker->controller->OnEvent(ftxui::Event::Return);
  asker->controller->OnEvent(ftxui::Event::Return);  // Inspect
  ASSERT_EQ(asker->controller->screen(), kTradeInspect);
  ASSERT_NE(asker->controller->trade_inspect_equip(), nullptr);
  EXPECT_EQ(asker->controller->trade_inspect_equip()->name(),
            IronSword().name());
  asker->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(asker->controller->screen(), kTrade);
}

// Accept toggles, and accepting applies to the offers as they are: changing
// either side withdraws both acceptances.
TEST_F(PartyControllerTest, AcceptingAndThenChangingTheTable) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  asker->state->character.AddMeso(5000);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  OpenTrade(*asker, *asked);

  PressAccept(*asker);
  PressAccept(*asked);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().trade.mine_accepted() &&
           asker->session.Snapshot().trade.theirs_accepted();
  }));

  // When both accept, the final confirmation opens, and nothing in the offers
  // can change until someone backs out of it.
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->controller->screen() == kTradeConfirm;
  }));
  asker->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->controller->screen() == kTrade &&
           !asker->session.Snapshot().trade.mine_accepted();
  }));
  ASSERT_TRUE(asker->session.Snapshot().trade.theirs_accepted());

  // Adding meso afterwards withdraws the remaining acceptance.
  asker->controller->OnEvent(ftxui::Event::ArrowLeft);
  asker->controller->OnEvent(ftxui::Event::ArrowLeft);
  asker->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(asker->controller->screen(), kTradeAmount);
  asker->controller->OnEvent(ftxui::Event::Character('5'));
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  asker->controller->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return !asked->session.Snapshot().trade.mine_accepted() &&
           !asked->session.Snapshot().trade.theirs_accepted();
  }));
}

// The one moment the bag can be checked: any change to the offers withdraws an
// acceptance.
TEST_F(PartyControllerTest, AFullBagCannotAccept) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  asked->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  while (asker->state->character.inventory().room() > 0) {
    asker->state->character.PickUp(
        std::make_unique<EquipInstance>(IronSword()));
  }
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  OpenTrade(*asker, *asked);
  OfferFirstBagItem(*asked);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().trade.theirs().equips_size() == 1;
  }));

  PressAccept(*asker);
  ASSERT_TRUE(asker->controller->party_notice_prompt().open());
  // Checks for one word, because the notice wraps and a phrase could be split
  // across lines.
  EXPECT_NE(asker->controller->party_notice().find("inventory"),
            std::string::npos);
  EXPECT_FALSE(asker->session.Snapshot().trade.mine_accepted());

  // Offering one of their own items makes room, and then it goes through.
  asker->controller->OnEvent(ftxui::Event::Return);  // closes the notice
  OfferFirstBagItem(*asker);
  PressAccept(*asker);
  EXPECT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().trade.mine_accepted();
  }));
}

// The final confirmation follows the trade state: the second acceptance opens
// it on both screens, and a cancel closes it without touching the other side's.
TEST_F(PartyControllerTest, TheFinalizeDialogAndItsCancel) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  OpenTrade(*asker, *asked);

  PressAccept(*asker);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->session.Snapshot().trade.theirs_accepted();
  }));
  EXPECT_EQ(asker->controller->screen(), kTrade) << "one acceptance is not two";

  PressAccept(*asked);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->controller->screen() == kTradeConfirm &&
           asked->controller->screen() == kTradeConfirm;
  }));

  // The one who answers first waits, and can still take it back.
  asked->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()},
                      [&]() { return asked->controller->trade_waiting(); }));
  EXPECT_EQ(asked->controller->screen(), kTradeConfirm);
  EXPECT_FALSE(asker->controller->trade_waiting());

  asked->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asked->controller->screen() == kTrade &&
           asker->controller->screen() == kTrade;
  }));
  // Only the cancelling player's acceptance is withdrawn, so one key press
  // brings the dialog back rather than two.
  EXPECT_FALSE(asked->session.Snapshot().trade.mine_accepted());
  EXPECT_TRUE(asker->session.Snapshot().trade.mine_accepted());
  EXPECT_FALSE(asked->controller->trade_waiting());
}

// The whole exchange: both confirm, both bags change, and both screens close.
TEST_F(PartyControllerTest, BothConfirmAndTheItemsCross) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  asker->state->character.AddMeso(5000);
  asked->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  asked->state->character.AddItem(TestStack(), 12);
  const int64_t my_meso = asker->state->character.meso();
  const int64_t their_meso = asked->state->character.meso();
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().online.players_size() == 2;
  }));
  OpenTrade(*asker, *asked);

  // The asker offers 5,000 meso; the other offers the sword.
  asker->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(asker->controller->screen(), kTradeAmount);
  for (char digit : std::string("5000")) {
    asker->controller->OnEvent(ftxui::Event::Character(digit));
  }
  asker->controller->OnEvent(ftxui::Event::ArrowDown);
  asker->controller->OnEvent(ftxui::Event::Return);
  OfferFirstBagItem(*asked);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->session.Snapshot().trade.theirs().equips_size() == 1 &&
           asked->session.Snapshot().trade.theirs().meso() == 5000;
  }));

  PressAccept(*asker);
  PressAccept(*asked);
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->controller->screen() == kTradeConfirm &&
           asked->controller->screen() == kTradeConfirm;
  }));
  asker->controller->OnEvent(ftxui::Event::Return);
  asked->controller->OnEvent(ftxui::Event::Return);

  // Both screens close, and neither is told the other left.
  ASSERT_TRUE(WaitFor({asker.get(), asked.get()}, [&]() {
    return asker->controller->screen() == kPlayerList &&
           asked->controller->screen() == kPlayerList;
  }));
  EXPECT_FALSE(asker->controller->party_notice_prompt().open());
  EXPECT_FALSE(asked->controller->party_notice_prompt().open());
  EXPECT_TRUE(asker->controller->notification().visible());

  EXPECT_EQ(asker->state->character.meso(), my_meso - 5000);
  EXPECT_EQ(asked->state->character.meso(), their_meso + 5000);
  ASSERT_EQ(asker->state->character.inventory().size(), 1);
  EXPECT_EQ(asker->state->character.inventory()[0].name(), IronSword().name());
  EXPECT_EQ(asked->state->character.inventory().size(), 0);
}

TEST_F(PartyControllerTest, TradingABusyPlayerIsRefused) {
  std::unique_ptr<Client> asker = Connect("Dagger");
  std::unique_ptr<Client> asked = Connect("Wand");
  std::unique_ptr<Client> latecomer = Connect("Bow");
  ASSERT_TRUE(WaitFor({asker.get(), asked.get(), latecomer.get()}, [&]() {
    return latecomer->session.Snapshot().online.players_size() == 3;
  }));
  AskToTrade(*asker, "Wand");
  ASSERT_TRUE(WaitFor({asker.get(), asked.get(), latecomer.get()}, [&]() {
    return !asker->session.Snapshot().trade.id().empty();
  }));

  AskToTrade(*latecomer, "Wand");

  ASSERT_TRUE(WaitFor({asker.get(), asked.get(), latecomer.get()}, [&]() {
    return latecomer->controller->party_notice_prompt().open();
  }));
  EXPECT_NE(latecomer->controller->party_notice().find("busy"),
            std::string::npos);
  EXPECT_TRUE(latecomer->controller->party_notice_is_refusal());
}

// Nothing is left to show once they leave, so the screen closes rather than
// keeping their last sheet.
TEST_F(PartyControllerTest, TheInspectScreenClosesWhenThePlayerLeaves) {
  std::unique_ptr<Client> reader = Connect("Dagger");
  std::unique_ptr<Client> read = Connect("Wand");
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->session.Snapshot().online.players_size() == 2;
  }));
  OpenMultiplayer(*reader, MultiplayerEntry::kPlayers);
  reader->controller->OnEvent(ftxui::Event::ArrowDown);
  reader->controller->OnEvent(ftxui::Event::Return);
  reader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(WaitFor({reader.get(), read.get()}, [&]() {
    return reader->controller->screen() == kPlayerInspect;
  }));

  read.reset();
  EXPECT_TRUE(WaitFor({reader.get()}, [&]() {
    return reader->controller->screen() == kPlayerList;
  }));
}

// A member inspects another member: their sheet comes with the party state, so
// the screen has everything it needs as soon as it opens.
TEST_F(PartyControllerTest, AMemberInspectsAnother) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  // The leader picks up a weapon, which reaches the guest with the party state
  // without being requested. They are levelled to Hyper Stats first, so the
  // sheet has the two allocations the screen switches between.
  while (leader->state->character.proto().level() < kHyperStatUnlockLevel) {
    leader->state->character.LevelUp();
  }
  // The leader's own autoswap switch, which gives the screen two allocations to
  // switch between. It is set before the gear, since the gear change is what
  // sends the sheet.
  leader->state->account.SetAutoswapPresets(true);
  leader->state->MirrorAccount();
  ASSERT_TRUE(leader->state->character.AllocateHyperStat(
      HYPER_STAT_FIELD_STR, StatPreset::kSecond, 2));
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

  // The guest, who isn't the leader, on the leader's row: the menu opens on
  // Inspect.
  ASSERT_FALSE(guest->party_panel.is_leader());
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPartyMenu);
  ASSERT_EQ(guest->party_panel.menu_selected(), kPartyMenuInspect);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  // The leader as they see themselves, rebuilt from the sheet they sent.
  EXPECT_EQ(guest->player_inspect_panel->character().username(), "Dagger");
  EXPECT_EQ(guest->player_inspect_panel->character().proto().level(),
            leader->state->character.proto().level());

  // Tab moves to their Character panel, where the Farm/Boss row switches
  // between their two Hyper Stat allocations.
  EXPECT_EQ(guest->player_inspect_panel->preset(), Activity::kFarming);
  guest->controller->OnEvent(ftxui::Event::Tab);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(guest->player_inspect_panel->preset(), Activity::kBossing);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(guest->player_inspect_panel->preset(), Activity::kFarming);

  // Their Hyper tab under the Boss allocation: Enter on a stat opens its card
  // for that allocation, read from their sheet.
  guest->controller->OnEvent(ftxui::Event::ArrowUp);     // -> the tab bar
  guest->controller->OnEvent(ftxui::Event::ArrowRight);  // -> Skills
  guest->controller->OnEvent(ftxui::Event::ArrowRight);  // -> Hyper
  guest->controller->OnEvent(ftxui::Event::ArrowDown);   // -> Farm/Boss
  guest->controller->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(guest->player_inspect_panel->preset(), Activity::kBossing);
  guest->controller->OnEvent(ftxui::Event::ArrowDown);  // -> the first stat
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kHyperStatInspect);
  EXPECT_EQ(guest->controller->hyper_inspect_field(), HYPER_STAT_FIELD_STR);
  EXPECT_EQ(guest->controller->hyper_inspect_level(), 2)
      << "the leader's Boss allocation, not the Farm one or the reader's";
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);
  guest->controller->OnEvent(ftxui::Event::Tab);

  // Enter on a worn item opens its card, using the panel's cursor rather than a
  // pointer held across a tick that may rebuild the member.
  ASSERT_NE(guest->player_inspect_panel->selected_item(), nullptr);
  EXPECT_EQ(guest->player_inspect_panel->selected_item()->prototype().name(),
            "Iron Sword");
  // The card compares their item with the reader's own weapon, taken from the
  // reader's sheet rather than the member's.
  guest->state->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  guest->state->character.Equip(guest->state->character.inventory().size() - 1);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerItemInspect);
  EXPECT_EQ(guest->controller->player_item_comparison(),
            guest->state->character.WornAt(StatPreset::kFirst,
                                           EQUIP_SLOT_PRIMARY_WEAPON));
  guest->Tick();
  EXPECT_EQ(guest->controller->screen(), kPlayerItemInspect);
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  guest->controller->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(guest->controller->screen(), kPartySelect);
}

// The cards the Inspect screen opens are the player's own cards showing someone
// else's numbers, and each returns to the screen that opened it.
TEST_F(PartyControllerTest, TheInspectScreenRaisesTheMembersOwnCards) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);
  leader->state->character.AdvanceJob(JOB_SWORDMAN);
  const Skill& strike = leader->state->skills.at("power_strike");
  ASSERT_TRUE(leader->state->character.LearnSkill(strike, 3));
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()},
                      [&guest]() { return guest->party_panel.in_party(); }));

  // The guest opens the leader, then moves to their Character panel.
  guest->controller->OnEvent(ftxui::Event::Return);
  guest->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);
  ASSERT_TRUE(WaitFor({leader.get(), guest.get()}, [&guest]() {
    return guest->player_inspect_panel->character().skill_level(
               guest->state->skills.at("power_strike")) == 3;
  })) << "the leader's learned skill never reached the reader";
  guest->controller->OnEvent(ftxui::Event::Tab);

  // View All Stats: their sheet, on the same screen their own last stats row
  // opens.
  guest->controller->OnEvent(ftxui::Event::ArrowDown);
  guest->controller->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(guest->controller->screen(), kPlayerAllStats);
  // It stays open over the following frames. The ticker's redraw is an event
  // like any other, and a screen that closed on any unrecognised event was gone
  // before the player could read it.
  guest->controller->OnEvent(ftxui::Event::Custom);
  guest->Tick();
  ASSERT_EQ(guest->controller->screen(), kPlayerAllStats)
      << "a repaint closed the screen";
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kPlayerInspect);

  // Their Skills tab, and the card a row opens: the leader's level, not the
  // reader's.
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

// The Equipped panel expands like the player's own, and Escape closes the
// expansion before the screen.
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

  // Up from the list is the tab bar, and Left from the first tab is Expand.
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

// The screen has nothing to show once the member leaves the party.
TEST_F(PartyControllerTest, TheMemberLeavingClosesTheInspectScreen) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  MakeParty(*leader, *guest);

  leader->controller->OnEvent(ftxui::Event::ArrowDown);
  leader->controller->OnEvent(ftxui::Event::Return);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->screen(), kPlayerInspect);
  EXPECT_EQ(leader->player_inspect_panel->character().username(), "Wand");

  // The guest leaves: down past both members to Leave Party, then Yes.
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

  // The leader may be refused for other reasons (these characters have no
  // weapon), but never because of whose party it is.
  leader->controller->OnEvent(ftxui::Event::Escape);
  leader->controller->OpenMenuEntry(MenuEntry::kBoss);
  leader->controller->OnEvent(ftxui::Event::Return);
  ASSERT_EQ(leader->controller->notice_lines().size(), 1u);
  EXPECT_EQ(leader->controller->notice_lines()[0],
            "You have no weapon equipped!");
}

// The whole flow: the leader picks a fight, the server checks the party, and
// everyone arrives in the arena together.
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
  // The player isn't named on their own screen, and the other player is.
  EXPECT_TRUE(members[0].name.empty());
  EXPECT_EQ(members[1].name, "Dagger");
  EXPECT_EQ(guest->controller->boss_run()->share_count(), 2);

  // The leader's hits are drawn on the guest's screen, faint, as someone
  // else's.
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

  // And the monster they are both hitting is the same monster.
  EXPECT_LT(guest->controller->boss_run()->phase_hp_fraction(), 1.0);
}

// The rest of the party is copied into the GameState for the fight, so their
// skills' effects on this character are included in its stats, and removed when
// the fight ends. See GameState::party.
TEST_F(PartyControllerTest, ThePartyIsSeatedForTheFightAndNoLonger) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  ReadyParty(*leader, *guest);
  // Being in a party isn't being in a fight, and gives nothing yet.
  EXPECT_TRUE(guest->state->party.empty());

  // Each screen copies its party in as the fight opens.
  EnterTheFight(*leader, *guest);

  // Everyone except themselves, rebuilt from the sheets they sent.
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

// Leaving the fight leaves it to whoever remains.
TEST_F(PartyControllerTest, OneWalkingOutLeavesTheFightToTheOther) {
  std::unique_ptr<Client> leader = Connect("Dagger");
  std::unique_ptr<Client> guest = Connect("Wand");
  ReadyParty(*leader, *guest);
  EnterTheFight(*leader, *guest);

  // Escape asks first, and the prompt starts on Cancel.
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kBossAbort);
  guest->controller->OnEvent(ftxui::Event::ArrowLeft);
  guest->controller->OnEvent(ftxui::Event::Return);

  ASSERT_TRUE(WaitFor(
      {leader.get(), guest.get()},
      [&]() { return guest->controller->screen() == kBossSelect; }, 0.02));
  EXPECT_EQ(guest->controller->boss_run(), nullptr);
  // The guest was pulled into the fight from the party screen, under the
  // Multiplayer box. Back on the main view that box is gone, and the Menu row
  // has its cursor.
  guest->controller->OnEvent(ftxui::Event::Escape);
  ASSERT_EQ(guest->controller->screen(), kMain);
  EXPECT_FALSE(guest->menu_panel->box_open());
  EXPECT_LT(guest->menu_panel->box_cursor(), 0);
  // The remaining player keeps fighting, and the other's panel disappears.
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

  // Moved off the screen, so closing the notice lands somewhere real.
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
  // The server is the outdated side, which is what a missed deploy looks like
  // to a player.
  TestServer behind(TestBosses(), TestMobs(), kMultiplayerVersion - 1);
  ASSERT_TRUE(behind.Start());
  Client player("Dagger", behind.port());
  player.session.Start(*player.state);
  ASSERT_TRUE(WaitFor({&player}, [&]() {
    return player.session.Snapshot().state == ConnectionState::kUnavailable;
  }));

  OpenMultiplayer(player, MultiplayerEntry::kParty);
  ASSERT_NE(player.controller->screen(), kPartySelect);
  // Wrapped to the dialog's width on arrival, keeping the message's own line
  // break: the two versions stay on their own line.
  EXPECT_EQ(
      player.controller->party_notice(),
      "The server is running an\nolder version. Trying again.\nClient: v" +
          std::to_string(kMultiplayerVersion) + ", Server: v" +
          std::to_string(kMultiplayerVersion - 1));

  // The server is deployed. Pressing Party again makes the connection retry now
  // rather than after its backoff, so the player gets in without restarting.
  ASSERT_TRUE(behind.RestartSpeaking(kMultiplayerVersion));
  OpenMultiplayer(player, MultiplayerEntry::kParty);
  ASSERT_TRUE(WaitFor({&player}, [&]() {
    return player.session.Snapshot().state == ConnectionState::kConnected;
  }));
  OpenParty(player);
}

}  // namespace
}  // namespace ms
