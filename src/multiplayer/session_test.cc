#include "src/multiplayer/session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "server/test_server.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

constexpr std::chrono::milliseconds kPatience(4000);

// A weapon for a character to be seen wearing.
EquipPrototype MakeSword() {
  EquipPrototype sword;
  sword.set_name("Iron Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.mutable_base_stats()->set_attack(30);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return sword;
}

// A state with no catalogs: nothing here reads an item or a map.
std::unique_ptr<GameState> MakeState() {
  return std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{});
}

class SessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(server_.Start());
    state_ = MakeState();
    state_->character.SetUsername("Dagger");
  }

  // Runs the session against the server until `ready` says the test can go
  // on, advancing it the way the game's tick does.
  bool WaitFor(MultiplayerSession& session,
               const std::function<bool(const MultiplayerSnapshot&)>& ready) {
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < deadline) {
      session.Advance(*state_);
      if (ready(session.Snapshot())) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

  bool WaitUntilConnected(MultiplayerSession& session) {
    return WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
      return snapshot.state == ConnectionState::kConnected;
    });
  }

  MultiplayerSession MakeSession() {
    return MultiplayerSession("127.0.0.1", server_.port());
  }

  TestServer server_;
  std::unique_ptr<GameState> state_;
};

TEST_F(SessionTest, KeepsTheAccountTheServerIssued) {
  MultiplayerSession session = MakeSession();
  EXPECT_FALSE(session.started());
  session.Start(*state_);
  EXPECT_TRUE(session.started());

  ASSERT_TRUE(WaitUntilConnected(session));
  session.Advance(*state_);
  EXPECT_FALSE(state_->account.multiplayer_account_id().empty());
  EXPECT_FALSE(state_->account.multiplayer_token().empty());
  EXPECT_EQ(state_->account.multiplayer_account_id(),
            session.Snapshot().account_id);
}

TEST_F(SessionTest, ComesBackAsTheSavedAccount) {
  state_->account.SetMultiplayerAccount("0123456789abcdef", "a-token");

  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));
  EXPECT_EQ(session.Snapshot().account_id, "0123456789abcdef");
  EXPECT_EQ(state_->account.multiplayer_account_id(), "0123456789abcdef");
}

TEST_F(SessionTest, IntroducesTheCharacterBeingPlayed) {
  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));

  session.client().CreateParty();
  ASSERT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));
  MultiplayerSnapshot snapshot = session.Snapshot();
  EXPECT_EQ(snapshot.party.members(0).player().name(), "Dagger");
  EXPECT_EQ(snapshot.party.members(0).player().level(),
            state_->character.proto().level());
}

TEST_F(SessionTest, TellsTheLobbyAboutANewName) {
  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));
  session.client().CreateParty();
  ASSERT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));

  state_->character.SetUsername("Wand");
  EXPECT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1 &&
           snapshot.party.members(0).player().name() == "Wand";
  }));
}

// The sheet is what the Inspect screen draws a party member from. What it
// leaves behind is everything a party has no business with.
TEST_F(SessionTest, TheSheetCarriesTheCharacterAndNotTheirBelongings) {
  state_->character.AddExp(50);
  state_->character.AddMeso(1'000'000);
  state_->character.PickUp(std::make_unique<EquipInstance>(MakeSword()));
  state_->character.Equip(0);
  state_->character.PickUp(std::make_unique<EquipInstance>(MakeSword()));

  Character sheet = PublicSheet(state_->character);
  EXPECT_EQ(sheet.name(), "Dagger");
  EXPECT_EQ(sheet.level(), state_->character.proto().level());
  EXPECT_EQ(sheet.equipped_size(), 1);
  EXPECT_EQ(sheet.inventory().equip_tab_size(), 0);
  EXPECT_EQ(sheet.meso(), 0);
  EXPECT_EQ(sheet.exp(), 0);
}

// A re-scrolled weapon changes what the Inspect screen draws and nothing the
// lobby list does. The update has to go out regardless.
TEST_F(SessionTest, TellsTheLobbyAboutNewGear) {
  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));
  session.client().CreateParty();
  ASSERT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));

  state_->character.PickUp(std::make_unique<EquipInstance>(MakeSword()));
  state_->character.Equip(0);
  EXPECT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1 &&
           snapshot.party.members(0).player().sheet().equipped_size() == 1;
  }));
}

}  // namespace
}  // namespace ms
