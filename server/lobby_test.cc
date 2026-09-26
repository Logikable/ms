#include "server/lobby.h"

#include <gtest/gtest.h>

#include <cstdint>
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

// Returns `player` with a clear of one boss recorded at `when`.
PlayerInfo Cleared(PlayerInfo player, const std::string& boss_key,
                   const std::string& difficulty, int64_t when) {
  BossClear* clear = player.add_boss_clears();
  clear->set_boss(boss_key);
  clear->set_difficulty(difficulty);
  clear->set_cleared_unix_seconds(when);
  return player;
}

StartFight Fight(const std::string& boss_key, int difficulty_index = 0,
                 bool practice = false) {
  StartFight request;
  request.set_boss_key(boss_key);
  request.set_difficulty_index(difficulty_index);
  request.mutable_options()->set_practice(practice);
  return request;
}

// Returns `player` with the Practice option on.
PlayerInfo Practising(PlayerInfo player) {
  player.mutable_boss_options()->set_practice(true);
  return player;
}

// Two test bosses. Zakum's second difficulty is marked coming soon. Hilla's
// two difficulties both need level 120 and reset daily.
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
  normal->set_reset(RESET_PERIOD_DAILY);
  BossDifficulty* hard = hilla.add_difficulties();
  hard->set_name("Hard");
  hard->set_unlock_level(120);
  hard->set_reset(RESET_PERIOD_DAILY);
  return bosses;
}

class LobbyTest : public ::testing::Test {
 protected:
  LobbyTest() : bosses_(Bosses()), lobby_(bosses_, 5) {
  }

  // Returns the single listed party, and fails if there is not exactly one.
  Party OnlyListed() {
    PartyList list = lobby_.Listed();
    EXPECT_EQ(list.parties_size(), 1);
    return list.parties_size() == 1 ? list.parties(0) : Party();
  }

  // Makes a party of `count` players named one, two and three, all level 140
  // and ready.
  std::string PartyOf(int count) {
    EXPECT_TRUE(lobby_.Create(Player("one", 140)).ok);
    std::string id = OnlyListed().id();
    const char* names[] = {"one", "two", "three"};
    for (int i = 1; i < count; ++i) {
      EXPECT_TRUE(lobby_.Join(Player(names[i], 140), id).ok);
    }
    // Set ready after everyone joins, since each join clears ready flags.
    for (int i = 1; i < count; ++i) {
      EXPECT_TRUE(lobby_.SetReady(names[i], true).ok);
    }
    return id;
  }

  // Starts the fight.
  LobbyResult Start(const std::string& account_id, const StartFight& request) {
    return lobby_.Start(account_id, request);
  }

  // Returns whether `account_id` is ready in the listed party.
  bool ReadyOf(const std::string& account_id) {
    Party party = OnlyListed();
    for (const PartyMember& member : party.members()) {
      if (member.player().account_id() == account_id) {
        return member.ready();
      }
    }
    return false;
  }

  // A Thursday afternoon, well after the 4am daily reset.
  static constexpr int64_t kNow = 1755792000;

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

  // The leader is always ready, so setting it is refused.
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
  // A leader must leave rather than kick themselves.
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
  // The old leader stays in the party.
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

  ASSERT_TRUE(Start("one", Fight("zakum")).ok);
  EXPECT_EQ(lobby_.Listed().parties_size(), 0);
  // The party still exists but is closed to newcomers.
  EXPECT_EQ(lobby_.StateFor("two").id(), id);
  EXPECT_EQ(lobby_.Join(Player("three", 140), id).reason,
            Refused::REASON_FIGHT_STARTED);
  EXPECT_EQ(Start("one", Fight("zakum")).reason, Refused::REASON_FIGHT_STARTED);
}

TEST_F(LobbyTest, OnlyTheLeaderStarts) {
  PartyOf(2);

  LobbyResult refused = Start("two", Fight("zakum"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_NOT_LEADER);
  EXPECT_EQ(lobby_.Listed().parties_size(), 1);
}

TEST_F(LobbyTest, RefusesAFightItCannotRun) {
  PartyOf(1);

  EXPECT_EQ(Start("one", Fight("balrog")).reason, Refused::REASON_UNKNOWN_BOSS);
  EXPECT_EQ(Start("one", Fight("zakum", 7)).reason,
            Refused::REASON_UNKNOWN_BOSS);
  // Coming soon.
  EXPECT_EQ(Start("one", Fight("zakum", 1)).reason,
            Refused::REASON_UNKNOWN_BOSS);
  EXPECT_EQ(lobby_.Listed().parties_size(), 1);
}

TEST_F(LobbyTest, EveryMemberHasToBeHighEnough) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140)).ok);
  ASSERT_TRUE(lobby_.Join(Player("two", 119), OnlyListed().id()).ok);
  ASSERT_TRUE(lobby_.SetReady("two", true).ok);

  // The leader is high enough. The message does not name who is too low.
  LobbyResult refused = Start("one", Fight("hilla"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_LEVEL_TOO_LOW);
  EXPECT_EQ(refused.message, "Someone doesn't meet the level requirement.");

  lobby_.UpdatePlayer(Player("two", 120));
  EXPECT_TRUE(Start("one", Fight("hilla")).ok);
}

TEST_F(LobbyTest, EveryMemberHasToBeReady) {
  PartyOf(3);
  ASSERT_TRUE(lobby_.SetReady("three", false).ok);

  LobbyResult refused = Start("one", Fight("zakum"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_NOT_READY);
  EXPECT_EQ(refused.message, "Someone is not ready.");

  ASSERT_TRUE(lobby_.SetReady("three", true).ok);
  EXPECT_TRUE(Start("one", Fight("zakum")).ok);
}

// Each game counts resets on its own clock, so the server takes a member's
// clears on trust rather than refuse on its own.
TEST_F(LobbyTest, AMembersClearDoesNotBlockTheFight) {
  PartyOf(2);
  lobby_.UpdatePlayer(
      Cleared(Player("two", 140), "hilla", "Normal", kNow - 6 * 60 * 60));
  EXPECT_TRUE(Start("one", Fight("hilla")).ok);
}

// One fight cannot reward players differently, so everyone must pick the same
// options.
TEST_F(LobbyTest, EveryMemberHasToHaveTheSameOptionsSet) {
  PartyOf(2);
  lobby_.UpdatePlayer(Practising(Player("two", 140)));

  LobbyResult refused = Start("one", Fight("zakum"));
  EXPECT_FALSE(refused.ok);
  EXPECT_EQ(refused.reason, Refused::REASON_OPTIONS_DIFFER);
  EXPECT_EQ(refused.message, "Players selected different bossing options.");
  // It is also refused when only the leader has practice on.
  EXPECT_EQ(Start("one", Fight("zakum", 0, /*practice=*/true)).reason,
            Refused::REASON_OPTIONS_DIFFER);

  lobby_.UpdatePlayer(Practising(Player("one", 140)));
  EXPECT_TRUE(Start("one", Fight("zakum", 0, /*practice=*/true)).ok);
}

// Practice still has to meet the level requirement.
TEST_F(LobbyTest, PracticeDoesNotWalkPastTheLevelGate) {
  PartyOf(2);
  lobby_.UpdatePlayer(Practising(Player("one", 140)));
  lobby_.UpdatePlayer(Practising(Player("two", 119)));
  EXPECT_EQ(Start("one", Fight("hilla", 0, /*practice=*/true)).reason,
            Refused::REASON_LEVEL_TOO_LOW);
}

TEST_F(LobbyTest, WantsAPartyToActOn) {
  EXPECT_EQ(lobby_.Leave("one").reason, Refused::REASON_NOT_IN_PARTY);
  EXPECT_EQ(Start("one", Fight("zakum")).reason, Refused::REASON_NOT_IN_PARTY);
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

  // The new leader gets the same event as an explicit promotion.
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
  // Nobody is left to promote.
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

  // Updating a player in no party changes nothing.
  lobby_.UpdatePlayer(Player("three", 30));
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());
}

TEST_F(LobbyTest, LosingAPlayerIsLeaving) {
  PartyOf(2);

  lobby_.Disconnect("two");
  EXPECT_EQ(OnlyListed().members_size(), 1);
  // Disconnecting a player in no party changes nothing.
  lobby_.Disconnect("three");
  EXPECT_EQ(lobby_.party_count(), 1);
}

TEST_F(LobbyTest, SaysWhoNeedsTelling) {
  ASSERT_TRUE(lobby_.Create(Player("one", 140)).ok);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one"}));
  EXPECT_TRUE(lobby_.TakeListingChanged());
  // Taking clears both.
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());

  ASSERT_TRUE(lobby_.Join(Player("two", 140), OnlyListed().id()).ok);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));

  // Both the leaving and staying player are updated, and so is the list.
  ASSERT_TRUE(lobby_.Leave("two").ok);
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));
  EXPECT_TRUE(lobby_.TakeListingChanged());

  // A refused request changes nothing.
  EXPECT_FALSE(lobby_.Join(Player("three", 1), "nosuchparty").ok);
  EXPECT_TRUE(lobby_.TakeChanged().empty());
  EXPECT_FALSE(lobby_.TakeListingChanged());
}

TEST_F(LobbyTest, TheKickedPlayerIsToldAsAMember) {
  PartyOf(2);
  lobby_.TakeChanged();

  ASSERT_TRUE(lobby_.Kick("one", "two").ok);
  // Both are updated: the kicked player learns they are in no party, and the
  // leader sees a smaller one.
  EXPECT_EQ(lobby_.TakeChanged(), std::vector<std::string>({"one", "two"}));
}

// The listing goes to everyone on every change and only shows the leader and
// size, so it leaves out sheets.
TEST_F(LobbyTest, TheListingCarriesNoSheets) {
  PlayerInfo player = Player("one", 140);
  player.mutable_sheet()->set_name("one");
  player.mutable_sheet()->set_level(140);
  ASSERT_TRUE(lobby_.Create(player).ok);

  ASSERT_EQ(OnlyListed().members_size(), 1);
  EXPECT_FALSE(OnlyListed().members(0).player().has_sheet());
  // The player's own party state keeps it, because Inspect reads it there.
  EXPECT_TRUE(lobby_.StateFor("one").members(0).player().has_sheet());
}

}  // namespace
}  // namespace ms
