#include "src/multiplayer/session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "google/protobuf/util/message_differencer.h"
#include "server/test_server.h"
#include "src/character/equip_presets.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/multiplayer.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// How many items the sheet's first gear preset has.
int WornInSheet(const Character& sheet) {
  return PresetOf(sheet.equip_presets(), StatPreset::kFirst).equipped().size();
}

constexpr std::chrono::milliseconds kPatience(4000);

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

  // Runs the session against the server until `ready` says the test can
  // continue, advancing it the way the game's tick does.
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
  // Their own Autoswap setting comes with them, so a sheet is read the way its
  // owner reads it.
  EXPECT_FALSE(snapshot.party.members(0).player().autoswap_presets());
}

TEST_F(SessionTest, TheAutoswapSwitchTravelsWithThePlayer) {
  state_->account.SetAutoswapPresets(true);
  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));

  session.client().CreateParty();
  ASSERT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));
  EXPECT_TRUE(session.Snapshot().party.members(0).player().autoswap_presets());
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

// The Inspect screen draws a party member from their sheet: the character and
// their unspent points, but none of their belongings.
TEST_F(SessionTest, TheSheetCarriesTheCharacterAndNotTheirBelongings) {
  state_->character.AddExp(50);
  state_->character.AddMeso(1'000'000);
  state_->character.AddHonor(500);
  state_->character.AddVPoints(30);
  state_->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  state_->character.Equip(0);
  state_->character.PickUp(std::make_unique<EquipInstance>(IronSword()));

  Character sheet = PublicSheet(state_->character);
  EXPECT_EQ(sheet.name(), "Dagger");
  EXPECT_EQ(sheet.level(), state_->character.proto().level());
  EXPECT_EQ(WornInSheet(sheet), 1);
  EXPECT_EQ(sheet.inventory().equip_tab_size(), 0);
  EXPECT_EQ(sheet.meso(), 0);
  // The Inspect screen draws these: the EXP bar, and the point pools its tabs
  // count against. Not 50, since the level-up used most of it.
  EXPECT_EQ(sheet.exp(), state_->character.proto().exp());
  EXPECT_GT(sheet.exp(), 0);
  EXPECT_EQ(sheet.v_points(), 30);
  EXPECT_EQ(sheet.ap(), state_->character.proto().ap());
  // Honor is the only balance withheld. See HonorEarnedDoesNotMoveTheSheet.
  EXPECT_EQ(sheet.honor(), 0);
}

// An update is sent whenever the sheet changes, so anything on it that changes
// with every kill sends the whole sheet that often (312 times in one second,
// measured on the live server). Honor is one of these, and nobody else can see
// it.
TEST_F(SessionTest, HonorEarnedDoesNotMoveTheSheet) {
  Character before = PublicSheet(state_->character);
  state_->character.AddHonor(500);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      before, PublicSheet(state_->character)))
      << "a kill's honor must not be an update";
}

// EXP reaches the lobby, since the Inspect screen has a bar to fill, but no
// more often than kExpUpdatePeriod however many kills happen. Any other change
// to the sheet is still sent immediately.
TEST_F(SessionTest, ExpReachesTheLobbyOnItsOwnClock) {
  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));
  session.client().CreateParty();
  ASSERT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));
  auto exp = [&session]() {
    const MultiplayerSnapshot snapshot = session.Snapshot();
    return snapshot.party.members_size() == 1
               ? snapshot.party.members(0).player().sheet().exp()
               : -1;
  };

  // One EXP at a time: a level-up would change the sheet by itself, and then
  // the test wouldn't be measuring the EXP timer.
  state_->character.AddExp(1);
  ASSERT_TRUE(WaitFor(
      session, [&exp](const MultiplayerSnapshot&) { return exp() == 1; }));

  // The next kill lands within the period, so the lobby keeps its old value.
  // Well short of kExpUpdatePeriod, or this would be measuring the timer.
  state_->character.AddExp(1);
  std::chrono::steady_clock::time_point until =
      std::chrono::steady_clock::now() + kExpUpdatePeriod / 5;
  while (std::chrono::steady_clock::now() < until) {
    session.Advance(*state_);
    ASSERT_EQ(exp(), 1) << "a kill's EXP went out on its own";
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // A name change isn't EXP, so it isn't delayed, and it sends the withheld EXP
  // value along with it.
  state_->character.SetUsername("Wand");
  EXPECT_TRUE(WaitFor(session, [&exp](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1 &&
           snapshot.party.members(0).player().name() == "Wand" && exp() == 2;
  }));
}

// A re-scrolled weapon changes what the Inspect screen draws, but nothing in
// the lobby list. The update must still be sent.
TEST_F(SessionTest, TellsTheLobbyAboutNewGear) {
  MultiplayerSession session = MakeSession();
  session.Start(*state_);
  ASSERT_TRUE(WaitUntilConnected(session));
  session.client().CreateParty();
  ASSERT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));

  state_->character.PickUp(std::make_unique<EquipInstance>(IronSword()));
  state_->character.Equip(0);
  EXPECT_TRUE(WaitFor(session, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1 &&
           WornInSheet(snapshot.party.members(0).player().sheet()) == 1;
  }));
}

}  // namespace
}  // namespace ms
