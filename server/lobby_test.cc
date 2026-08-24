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

StartFight Fight(const std::string& boss_key, int difficulty_index = 0) {
  StartFight request;
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

  // A party of `count` players, named one, two, three, all at level 140.
  std::string PartyOf(int count) {
    EXPECT_TRUE(lobby_.Create(Player("one", 140)).ok);
    std::string id = OnlyListed().id();
    const char* names[] = {"one", "two", "three"};
    for (int i = 1; i < count; ++i) {
      EXPECT_TRUE(lobby_.Join(Player(names[i], 140), id).ok);
    }
    return id;
  }

  // Whether `account_id` shows as ready in the listed party.
  bool ReadyOf(const std::string& account_id) {
    Party party = OnlyListed();
    for (const PartyMember& member : party.members()) {
      if (member.player().account_id() == account_id) {
        return member.ready();
      }
    }
    return false;
  }

  std::map<std::string, Boss> bosses_;
  Lobby lobby_;
};

TEST_F(LobbyTest, MakesAParty) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140)).ok);

  Party party = OnlyListed();
  EXPECT_FALSE(party.id().empty());
  EXPECT_EQ(party.leader_account_id(), "one");
  ASSERT_EQ(party.members_size(), 1);
  EXPECT_EQ(party.members(0).player().account_id(), "one");
  EXPECT_EQ(lobby_.StateFor("one").id(), party.id());
  EXPECT_TRUE(lobby_.StateFor("two").id().empty());
}

TEST_F(LobbyTest, OnePartyAtATime) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140)).ok);

  LobbyResult again = lobby_.Create(Player("one", 140));
  EXPECT_FALSE(again.ok);
  EXPECT_EQ(again.reason, Refused::REASON_ALREADY_IN_PARTY);
  EXPECT_EQ(lobby_.party_count(), 1);
}

TEST_F(LobbyTest, JoinsAParty) {
  std::string id = PartyOf(2);

  Party party = OnlyListed();
  ASSERT_EQ(party.members_size(), 2);
  EXPECT_EQ(party.members(1).player().account_id(), "two");
  EXPECT_EQ(party.leader_account_id(), "one");
  EXPECT_EQ(lobby_.StateFor("two").id(), id);
}

TEST_F(LobbyTest, ThreeToAParty) {
  std::string id = PartyOf(3);

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

TEST_F(LobbyTest, SaysWhoIsReady) {
  PartyOf(2);

  ASSERT_TRUE(lobby_.SetReady("two", true).ok);
  EXPECT_TRUE(ReadyOf("two"));
  ASSERT_TRUE(lobby_.SetReady("two", false).ok);
  EXPECT_FALSE(ReadyOf("two"));

  // The leader is ready by being the leader, so there is nothing to say.
  EXPECT_EQ(lobby_.SetReady("one", true).reason, Refused::REASON_NOT_A_MEMBER);
  EXPECT_EQ(lobby_.SetReady("nobody", true).reason,
            Refused::REASON_NOT_IN_PARTY);
}

TEST_F(LobbyTest, ChangingThePartyClearsReady) {
  std::string id = PartyOf(2);
  ASSERT_TRUE(lobby_.SetReady("two", true).ok);

  // Somebody joining.
  ASSERT_TRUE(lobby_.Join(Player("three", 140), id).ok);
  EXPECT_FALSE(ReadyOf("two"));

  // Somebody leaving.
  ASSERT_TRUE(lobby_.SetReady("two", true).ok);
  ASSERT_TRUE(lobby_.SetReady("three", true).ok);
  ASSERT_TRUE(lobby_.Leave("three").ok);
  EXPECT_FALSE(ReadyOf("two"));

  // The party changing hands.
  ASSERT_TRUE(lobby_.SetReady("two", true).ok);
  ASSERT_TRUE(lobby_.Promote("one", "two").ok);
  EXPECT_FALSE(ReadyOf("one"));
}

TEST_F(LobbyTest, KicksAMember) {
  PartyOf(2);
  lobby_.TakeEvents();

  ASSERT_TRUE(lobby_.Kick("one", "two").ok);
  EXPECT_EQ(OnlyListed().members_size(), 1);
  EXPECT_TRUE(lobby_.StateFor("two").id().empty());

  std::vector<LobbyEvent> events = lobby_.TakeEvents();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].account_id, "two");
  EXPECT_EQ(events[0].event.kind(), PartyEvent::KICKED);
  EXPECT_FALSE(events[0].event.message().empty());
}

TEST_F(LobbyTest, OnlyTheLeaderKicks) {
  PartyOf(3);

  EXPECT_EQ(lobby_.Kick("two", "three").reason, Refused::REASON_NOT_LEADER);
  // Leaving is how a leader gets out, not kicking themselves.
  EXPECT_EQ(lobby_.Kick("one", "one").reason, Refused::REASON_NOT_A_MEMBER);
  EXPECT_EQ(lobby_.Kick("one", "nobody").reason, Refused::REASON_NOT_A_MEMBER);
  EXPECT_EQ(lobby_.Kick("nobody", "one").reason, Refused::REASON_NOT_IN_PARTY);
  EXPECT_EQ(OnlyListed().members_size(), 3);
}

TEST_F(LobbyTest, HandsThePartyOn) {
  PartyOf(2);
  lobby_.TakeEvents();

  ASSERT_TRUE(lobby_.Promote("one", "two").ok);
  EXPECT_EQ(OnlyListed().leader_account_id(), "two");
  // The one who handed it over stays in the party.
  EXPECT_EQ(OnlyListed().members_size(), 2);

  std::vector<LobbyEvent> events = lobby_.TakeEvents();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].account_id, "two");
  EXPECT_EQ(events[0].event.kind(), PartyEvent::PROMOTED);
}

TEST_F(LobbyTest, OnlyTheLeaderPromotes) {
  PartyOf(3);

  EXPECT_EQ(lobby_.Promote("two", "three").reason, Refused::REASON_NOT_LEADER);
  EXPECT_EQ(lobby_.Promote("one", "one").reason, Refused::REASON_NOT_A_MEMBER);
  EXPECT_EQ(lobby_.Promote("one", "nobody").reason,
            Refused::REASON_NOT_A_MEMBER);
  EXPECT_EQ(OnlyListed().leader_account_id(), "one");
}

TEST_F(LobbyTest, StartingTakesThePartyOutOfTheList) {
  std::string id = PartyOf(2);

  ASSERT_TRUE(lobby_.Start("one", Fight("zakum")).ok);
  EXPECT_EQ(lobby_.Listed().parties_size(), 0);
  // The party is still theirs; it is only closed to newcomers.
  EXPECT_EQ(lobby_.StateFor("two").id(), id);
  EXPECT_EQ(lobby_.Join(Player("three", 140), id).reason,
            Refused::REASON_FIGHT_STARTED);
  EXPECT_EQ(lobby_.Start("one", Fight("zakum")).reason,
            Refused::REASON_FIGHT_STARTED);
}

TEST_F(LobbyTest, OnlyTheLeaderStarts) {
  PartyOf(2);

  LobbyResult refused = lobby_.Start("two", Fight("zakum"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_NOT_LEADER);
  EXPECT_EQ(lobby_.Listed().parties_size(), 1);
}

TEST_F(LobbyTest, RefusesAFightItCannotRun) {
  PartyOf(1);

  EXPECT_EQ(lobby_.Start("one", Fight("balrog")).reason,
            Refused::REASON_UNKNOWN_BOSS);
  EXPECT_EQ(lobby_.Start("one", Fight("zakum", 7)).reason,
            Refused::REASON_UNKNOWN_BOSS);
  // Written down, not built.
  EXPECT_EQ(lobby_.Start("one", Fight("zakum", 1)).reason,
            Refused::REASON_UNKNOWN_BOSS);
  EXPECT_EQ(lobby_.Listed().parties_size(), 1);
}

TEST_F(LobbyTest, EveryMemberHasToBeHighEnough) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140)).ok);
  ASSERT_TRUE(lobby_.Join(Player("two", 119), OnlyListed().id()).ok);

  // The leader is high enough; the one who is not is the one named.
  LobbyResult refused = lobby_.Start("one", Fight("hilla"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_LEVEL_TOO_LOW);
  EXPECT_NE(refused.message.find("two"), std::string::npos);
  EXPECT_NE(refused.message.find("120"), std::string::npos);

  lobby_.UpdatePlayer(Player("two", 120));
  EXPECT_TRUE(lobby_.Start("one", Fight("hilla")).ok);
}

TEST_F(LobbyTest, WantsAPartyToActOn) {
  EXPECT_EQ(lobby_.Leave("one").reason, Refused::REASON_NOT_IN_PARTY);
  EXPECT_EQ(lobby_.Start("one", Fight("zakum")).reason,
            Refused::REASON_NOT_IN_PARTY);
  EXPECT_EQ(lobby_.Promote("one", "two").reason, Refused::REASON_NOT_IN_PARTY);
}

TEST_F(LobbyTest, TheLeadPassesOn) {
  PartyOf(3);
  lobby_.TakeEvents();

  ASSERT_TRUE(lobby_.Leave("one").ok);
  Party party = OnlyListed();
  EXPECT_EQ(party.leader_account_id(), "two");
  EXPECT_EQ(party.members_size(), 2);
  EXPECT_TRUE(lobby_.StateFor("one").id().empty());

  // The one who inherits it is told, the same way an explicit promotion tells
  // them.
  std::vector<LobbyEvent> events = lobby_.TakeEvents();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].account_id, "two");
  EXPECT_EQ(events[0].event.kind(), PartyEvent::PROMOTED);
}

TEST_F(LobbyTest, TheLastOutClosesTheParty) {
  PartyOf(1);

  ASSERT_TRUE(lobby_.Leave("one").ok);
  EXPECT_EQ(lobby_.party_count(), 0);
  EXPECT_EQ(lobby_.Listed().parties_size(), 0);
  // Nobody is left to be handed a party that is gone.
  EXPECT_TRUE(lobby_.TakeEvents().empty());
}

TEST_F(LobbyTest, TakesANewLevelIntoTheParty) {
  PartyOf(2);
  lobby_.TakeChanged();
  lobby_.TakeListingChanged();

  lobby_.UpdatePlayer(Player("two", 141));
  EXPECT_EQ(OnlyListed().members(1).player().level(), 141);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));
  EXPECT_TRUE(lobby_.TakeListingChanged());

  // A player in no party has nowhere to be updated.
  lobby_.UpdatePlayer(Player("three", 30));
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());
}

TEST_F(LobbyTest, LosingAPlayerIsLeaving) {
  PartyOf(2);

  lobby_.Disconnect("two");
  EXPECT_EQ(OnlyListed().members_size(), 1);
  // A player who was in no party is nobody's business.
  lobby_.Disconnect("three");
  EXPECT_EQ(lobby_.party_count(), 1);
}

TEST_F(LobbyTest, SaysWhoNeedsTelling) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140)).ok);
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

TEST_F(LobbyTest, TheKickedPlayerIsToldAsAMember) {
  PartyOf(2);
  lobby_.TakeChanged();

  ASSERT_TRUE(lobby_.Kick("one", "two").ok);
  // Both of them: the one removed learns they are in nothing, and the one who
  // stays sees a shorter party.
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));
}

// The listing goes out to everyone whenever any party changes, and it draws a
// leader and a capacity. A sheet is for the party you are in.
TEST_F(LobbyTest, TheListingCarriesNoSheets) {
  PlayerInfo player = Player("one", 140);
  player.mutable_sheet()->set_name("one");
  player.mutable_sheet()->set_level(140);
  ASSERT_TRUE(lobby_.Create(player).ok);

  ASSERT_EQ(OnlyListed().members_size(), 1);
  EXPECT_FALSE(OnlyListed().members(0).player().has_sheet());
  // The party the player is in keeps it: that is where Inspect reads from.
  EXPECT_TRUE(lobby_.StateFor("one").members(0).player().has_sheet());
}

}  // namespace
}  // namespace ms
