#include "server/lobby.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/protos/boss.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

PlayerInfo Player(const std::string& account_id, int level) {
  PlayerInfo player;
  player.set_account_id(account_id);
  player.set_name(account_id);
  player.set_level(level);
  return player;
}

CreateParty Fight(const std::string& boss_key, int difficulty_index = 0) {
  CreateParty request;
  request.set_boss_key(boss_key);
  request.set_difficulty_index(difficulty_index);
  return request;
}

// Two fights to try the rules against: Zakum, whose second difficulty is
// written down but not built, and Hilla, who is gated on level.
std::map<std::string, Boss> Bosses() {
  std::map<std::string, Boss> bosses;
  Boss& zakum = bosses["zakum"];
  zakum.set_name("Zakum");
  zakum.add_difficulties()->set_name("Normal");
  BossDifficulty* chaos = zakum.add_difficulties();
  chaos->set_name("Chaos");
  chaos->set_coming_soon(true);
  Boss& hilla = bosses["hilla"];
  hilla.set_name("Hilla");
  BossDifficulty* normal = hilla.add_difficulties();
  normal->set_name("Normal");
  normal->set_unlock_level(120);
  return bosses;
}

class LobbyTest : public ::testing::Test {
 protected:
  LobbyTest() : bosses_(Bosses()), lobby_(bosses_, 5) {
  }

  // The one party in the list, which is what most of these are asking about.
  Party OnlyListed() {
    PartyList list = lobby_.Listed();
    EXPECT_EQ(list.parties_size(), 1);
    return list.parties_size() == 1 ? list.parties(0) : Party();
  }

  std::map<std::string, Boss> bosses_;
  Lobby lobby_;
};

TEST_F(LobbyTest, MakesAParty) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);

  Party party = OnlyListed();
  EXPECT_FALSE(party.id().empty());
  EXPECT_EQ(party.boss_key(), "zakum");
  EXPECT_EQ(party.leader_account_id(), "one");
  ASSERT_EQ(party.members_size(), 1);
  EXPECT_EQ(party.members(0).account_id(), "one");
  // A client that names no mode gets the one everybody means.
  EXPECT_EQ(party.mode(), PARTY_MODE_SHARED);
  EXPECT_EQ(lobby_.StateFor("one").id(), party.id());
  EXPECT_TRUE(lobby_.StateFor("two").id().empty());
}

TEST_F(LobbyTest, KeepsTheModeItWasAskedFor) {
  CreateParty request = Fight("zakum");
  request.set_mode(PARTY_MODE_SOLO_TOGETHER);
  ASSERT_TRUE(lobby_.Create(Player("one", 140), request).ok);

  EXPECT_EQ(OnlyListed().mode(), PARTY_MODE_SOLO_TOGETHER);
}

TEST_F(LobbyTest, OnePartyAtATime) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);

  LobbyResult again = lobby_.Create(Player("one", 140), Fight("zakum"));
  EXPECT_FALSE(again.ok);
  EXPECT_EQ(again.reason, Refused::REASON_ALREADY_IN_PARTY);
  EXPECT_EQ(lobby_.party_count(), 1);
}

TEST_F(LobbyTest, RefusesAFightItCannotRun) {
  PlayerInfo player = Player("one", 140);
  EXPECT_EQ(lobby_.Create(player, Fight("balrog")).reason,
            Refused::REASON_UNKNOWN_BOSS);
  EXPECT_EQ(lobby_.Create(player, Fight("zakum", 7)).reason,
            Refused::REASON_UNKNOWN_BOSS);
  // Written down, not built.
  EXPECT_EQ(lobby_.Create(player, Fight("zakum", 1)).reason,
            Refused::REASON_UNKNOWN_BOSS);
  EXPECT_EQ(lobby_.party_count(), 0);
}

TEST_F(LobbyTest, RefusesAFightTheLevelDoesNotOpen) {
  LobbyResult refused = lobby_.Create(Player("one", 119), Fight("hilla"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_LEVEL_TOO_LOW);
  EXPECT_NE(refused.message.find("120"), std::string::npos);

  EXPECT_TRUE(lobby_.Create(Player("one", 120), Fight("hilla")).ok);
}

TEST_F(LobbyTest, JoinsAParty) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  std::string id = OnlyListed().id();

  ASSERT_TRUE(lobby_.Join(Player("two", 140), id).ok);
  Party party = OnlyListed();
  ASSERT_EQ(party.members_size(), 2);
  EXPECT_EQ(party.members(1).account_id(), "two");
  EXPECT_EQ(party.leader_account_id(), "one");
  EXPECT_EQ(lobby_.StateFor("two").id(), id);
}

TEST_F(LobbyTest, ThreeToAParty) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  std::string id = OnlyListed().id();
  ASSERT_TRUE(lobby_.Join(Player("two", 140), id).ok);
  ASSERT_TRUE(lobby_.Join(Player("three", 140), id).ok);

  LobbyResult refused = lobby_.Join(Player("four", 140), id);
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_PARTY_FULL);
  EXPECT_EQ(OnlyListed().members_size(), kMaxPartySize);
}

TEST_F(LobbyTest, RefusesAPartyThatIsNotThere) {
  LobbyResult refused = lobby_.Join(Player("one", 140), "nosuchparty");
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_PARTY_GONE);
}

TEST_F(LobbyTest, RefusesTheLevelOnTheWayIn) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("hilla")).ok);

  LobbyResult refused = lobby_.Join(Player("two", 100), OnlyListed().id());
  EXPECT_EQ(refused.reason, Refused::REASON_LEVEL_TOO_LOW);
}

TEST_F(LobbyTest, StartingTakesThePartyOutOfTheList) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  std::string id = OnlyListed().id();
  ASSERT_TRUE(lobby_.Join(Player("two", 140), id).ok);

  ASSERT_TRUE(lobby_.Start("one").ok);
  EXPECT_EQ(lobby_.Listed().parties_size(), 0);
  // The party is still theirs; it is only closed to newcomers.
  EXPECT_EQ(lobby_.StateFor("two").id(), id);
  EXPECT_EQ(lobby_.Join(Player("three", 140), id).reason,
            Refused::REASON_FIGHT_STARTED);
  EXPECT_EQ(lobby_.Start("one").reason, Refused::REASON_FIGHT_STARTED);
}

TEST_F(LobbyTest, OnlyTheLeaderStarts) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  ASSERT_TRUE(lobby_.Join(Player("two", 140), OnlyListed().id()).ok);

  LobbyResult refused = lobby_.Start("two");
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_NOT_LEADER);
  EXPECT_EQ(lobby_.Listed().parties_size(), 1);
}

TEST_F(LobbyTest, WantsAPartyToActOn) {
  EXPECT_EQ(lobby_.Leave("one").reason, Refused::REASON_NOT_IN_PARTY);
  EXPECT_EQ(lobby_.Start("one").reason, Refused::REASON_NOT_IN_PARTY);
}

TEST_F(LobbyTest, TheLeadPassesOn) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  std::string id = OnlyListed().id();
  ASSERT_TRUE(lobby_.Join(Player("two", 140), id).ok);
  ASSERT_TRUE(lobby_.Join(Player("three", 140), id).ok);

  ASSERT_TRUE(lobby_.Leave("one").ok);
  Party party = OnlyListed();
  EXPECT_EQ(party.leader_account_id(), "two");
  EXPECT_EQ(party.members_size(), 2);
  EXPECT_TRUE(lobby_.StateFor("one").id().empty());
}

TEST_F(LobbyTest, TheLastOutClosesTheParty) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);

  ASSERT_TRUE(lobby_.Leave("one").ok);
  EXPECT_EQ(lobby_.party_count(), 0);
  EXPECT_EQ(lobby_.Listed().parties_size(), 0);
}

TEST_F(LobbyTest, TakesANewLevelIntoTheParty) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  ASSERT_TRUE(lobby_.Join(Player("two", 140), OnlyListed().id()).ok);
  lobby_.TakeChanged();
  lobby_.TakeListingChanged();

  lobby_.UpdatePlayer(Player("two", 141));
  EXPECT_EQ(OnlyListed().members(1).level(), 141);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));
  EXPECT_TRUE(lobby_.TakeListingChanged());

  // A player in no party has nowhere to be updated.
  lobby_.UpdatePlayer(Player("three", 30));
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());
}

TEST_F(LobbyTest, LosingAPlayerIsLeaving) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  ASSERT_TRUE(lobby_.Join(Player("two", 140), OnlyListed().id()).ok);

  lobby_.Disconnect("two");
  EXPECT_EQ(OnlyListed().members_size(), 1);
  // A player who was in no party is nobody's business.
  lobby_.Disconnect("three");
  EXPECT_EQ(lobby_.party_count(), 1);
}

TEST_F(LobbyTest, SaysWhoNeedsTelling) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140), Fight("zakum")).ok);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one"}));
  EXPECT_TRUE(lobby_.TakeListingChanged());
  // Both are cleared by the taking.
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());

  ASSERT_TRUE(lobby_.Join(Player("two", 140), OnlyListed().id()).ok);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));

  // The one leaving is told as well as the one staying, and the list has one
  // fewer member on it.
  ASSERT_TRUE(lobby_.Leave("two").ok);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));
  EXPECT_TRUE(lobby_.TakeListingChanged());

  // A refused ask changes nothing.
  EXPECT_FALSE(lobby_.Join(Player("three", 1), "nosuchparty").ok);
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());
}

}  // namespace
}  // namespace ms
