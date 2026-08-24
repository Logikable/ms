#include "server/fight.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/combat/boss_timing.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// A two-phase fight: two arms of 100 HP apiece, then a body of 500, with
// four places to stand in each phase -- one more than the party fills.
Boss TwoPhases() {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_time_limit_seconds(60);
  BossPhase* arms = normal->add_phases();
  Spawn* arm = arms->add_spawns();
  arm->set_mob("arm");
  arm->add_spots()->set_x(2);
  arm->add_spots()->set_x(4);
  for (int i = 0; i < 4; ++i) {
    arms->add_player_spots()->set_x(i);
  }
  BossPhase* body = normal->add_phases();
  body->add_spawns()->set_mob("body");
  body->mutable_spawns(0)->add_spots()->set_x(3);
  for (int i = 0; i < 4; ++i) {
    body->add_player_spots()->set_x(i);
  }
  return boss;
}

std::map<std::string, Mob> Mobs() {
  std::map<std::string, Mob> mobs;
  mobs["arm"].set_max_hp(100);
  mobs["body"].set_max_hp(500);
  return mobs;
}

Party PartyOf(int count) {
  const char* names[] = {"one", "two", "three"};
  Party party;
  party.set_id("p1");
  party.set_leader_account_id("one");
  for (int i = 0; i < count; ++i) {
    PlayerInfo* player = party.add_members()->mutable_player();
    player->set_account_id(names[i]);
    player->set_name(names[i]);
  }
  return party;
}

class FightTest : public ::testing::Test {
 protected:
  FightTest()
      : boss_(TwoPhases()),
        mobs_(Mobs()),
        fight_("zakum", boss_, 0, mobs_, PartyOf(3)) {
  }

  // Runs the countdown out, which is where every test that lands a hit
  // starts.
  void CountIn() {
    fight_.Advance(kBossCountdownSeconds);
    EXPECT_EQ(fight_.state(), PartyFightState::kFighting);
  }

  Boss boss_;
  std::map<std::string, Mob> mobs_;
  PartyFight fight_;
};

TEST_F(FightTest, StandsThePartyOnSpotsOfTheirOwn) {
  EXPECT_EQ(fight_.state(), PartyFightState::kCountdown);
  EXPECT_EQ(fight_.share_count(), 3);
  ASSERT_EQ(fight_.players().size(), 3u);
  EXPECT_EQ(fight_.players()[0].spot, 0);
  EXPECT_EQ(fight_.players()[1].spot, 1);
  EXPECT_EQ(fight_.players()[2].spot, 2);
  EXPECT_EQ(fight_.hp_fractions().size(), 2u);
}

TEST_F(FightTest, NothingLandsBeforeTheCountIsUp) {
  fight_.Hit("one", 0, 50);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);

  CountIn();
  fight_.Hit("one", 0, 50);
  EXPECT_EQ(fight_.hp_fractions()[0], 0.5);
}

TEST_F(FightTest, EverybodyHitsTheOneRoster) {
  CountIn();

  fight_.Hit("one", 0, 40);
  fight_.Hit("two", 0, 40);
  fight_.Hit("three", 0, 40);
  EXPECT_EQ(fight_.hp_fractions()[0], 0.0);
  EXPECT_EQ(fight_.hp_fractions()[1], 1.0);
}

TEST_F(FightTest, DropsDamageItCannotPlace) {
  CountIn();

  fight_.Hit("nobody", 0, 50);
  fight_.Hit("one", 7, 50);
  fight_.Hit("one", -1, 50);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);
}

TEST_F(FightTest, TheNextPhaseArrivesAfterABeat) {
  CountIn();
  fight_.Hit("one", 0, 100);
  fight_.Hit("one", 1, 100);
  fight_.Advance(0.1);
  EXPECT_EQ(fight_.state(), PartyFightState::kPhaseGap);
  EXPECT_EQ(fight_.phase(), 0);

  fight_.Advance(kBossPhaseGapSeconds);
  EXPECT_EQ(fight_.state(), PartyFightState::kFighting);
  EXPECT_EQ(fight_.phase(), 1);
  ASSERT_EQ(fight_.hp_fractions().size(), 1u);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);

  fight_.Hit("one", 0, 500);
  fight_.Advance(0.1);
  EXPECT_EQ(fight_.state(), PartyFightState::kWon);
  EXPECT_FALSE(fight_.done());
  fight_.Advance(kBossEndHoldSeconds);
  EXPECT_TRUE(fight_.done());
}

TEST_F(FightTest, TheClockRunsOut) {
  CountIn();

  fight_.Advance(60.0);
  EXPECT_EQ(fight_.state(), PartyFightState::kTimedOut);
  EXPECT_EQ(fight_.seconds_left(), 0.0);
  // A fight that is over takes nothing more.
  fight_.Hit("one", 0, 100);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);
}

TEST_F(FightTest, OneSpotHoldsOnePlayer) {
  CountIn();

  EXPECT_FALSE(fight_.MoveTo("one", 1));
  EXPECT_EQ(fight_.players()[0].spot, 0);
  EXPECT_FALSE(fight_.MoveTo("one", 4));

  // Free once the player who was on it walks off.
  ASSERT_TRUE(fight_.MoveTo("two", 3));
  EXPECT_TRUE(fight_.MoveTo("one", 1));
  EXPECT_FALSE(fight_.MoveTo("three", 1));
}

TEST_F(FightTest, APlayerWhoGoesStopsHittingAndFreesTheirSpot) {
  CountIn();
  fight_.Disconnect("two");

  fight_.Hit("two", 0, 50);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);
  EXPECT_FALSE(fight_.players()[1].present);
  EXPECT_TRUE(fight_.MoveTo("one", 1));
  // What the drops are split by is who came, not who stayed.
  EXPECT_EQ(fight_.share_count(), 3);
  EXPECT_EQ(fight_.state(), PartyFightState::kFighting);
}

TEST_F(FightTest, TheLastOneOutAbandonsTheFight) {
  CountIn();

  fight_.Disconnect("one");
  fight_.Disconnect("three");
  EXPECT_EQ(fight_.state(), PartyFightState::kFighting);
  fight_.Disconnect("two");
  EXPECT_EQ(fight_.state(), PartyFightState::kAbandoned);
  // Nothing is held for a fight nobody is watching.
  EXPECT_TRUE(fight_.done());
}

TEST_F(FightTest, AFightWithNothingToKillIsNoFight) {
  std::map<std::string, Mob> empty;
  PartyFight fight("zakum", boss_, 0, empty, PartyOf(1));
  EXPECT_EQ(fight.state(), PartyFightState::kAbandoned);

  PartyFight unknown("zakum", boss_, 4, mobs_, PartyOf(1));
  EXPECT_EQ(unknown.state(), PartyFightState::kAbandoned);
}

}  // namespace
}  // namespace ms
