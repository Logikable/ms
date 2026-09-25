#include "server/fight.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>

#include "src/combat/boss_timing.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// A two-phase fight: two 100 HP arms, then a 500 HP body. Each phase has four
// player spots, one more than the party needs.
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

// The same fight with drops: a stackable shard and an equip mark, since drop
// rate treats the two differently.
Boss Dropping(double shard_rate, double mark_rate = 0.0) {
  Boss boss = TwoPhases();
  BossDifficulty* normal = boss.mutable_difficulties(0);
  MobDrop* shard = normal->add_drops();
  shard->set_item("shard");
  shard->set_per_kill(shard_rate);
  MobDrop* mark = normal->add_drops();
  mark->set_equip("mark");
  mark->set_per_kill(mark_rate);
  return boss;
}

// Wins the fight by killing every mob in every phase.
void Clear(PartyFight& fight) {
  fight.Advance(kBossCountdownSeconds);
  while (!fight.over()) {
    for (int slot = 0; slot < static_cast<int>(fight.hp_fractions().size());
         ++slot) {
      fight.Hit("one", slot, 1000);
    }
    fight.Advance(kBossPhaseGapSeconds);
  }
}

// Returns the total count of every award across the party.
int64_t TotalAwards(const PartyFight& fight) {
  int64_t total = 0;
  for (const FightPlayer& player : fight.players()) {
    for (const FightAward& award : player.awards) {
      total += award.count();
    }
  }
  return total;
}

class FightTest : public ::testing::Test {
 protected:
  FightTest()
      : boss_(TwoPhases()),
        mobs_(Mobs()),
        fight_("p1-1", "zakum", boss_, 0, mobs_, PartyOf(3)) {
  }

  // Finishes the countdown so hits can land.
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

TEST_F(FightTest, AReportLandsWalksAndWindsUp) {
  CountIn();
  FightUpdate update;
  update.set_spot(3);
  update.set_attack_name("Blizzard");
  update.set_attack_fraction(0.5);
  update.set_buff_count(3);
  FightDamage* line = update.add_lines();
  line->set_slot(0);
  line->set_damage(50);

  fight_.Report("one", update);
  EXPECT_EQ(fight_.hp_fractions()[0], 0.5);
  EXPECT_EQ(fight_.players()[0].spot, 3);
  EXPECT_EQ(fight_.players()[0].attack_name, "Blizzard");
  EXPECT_EQ(fight_.players()[0].attack_fraction, 0.5);
  EXPECT_EQ(fight_.players()[0].buff_count, 3);
  // Damage lines are kept for the other players until a broadcast takes them.
  EXPECT_EQ(fight_.players()[0].lines.size(), 1u);
  fight_.TakeLines();
  EXPECT_TRUE(fight_.players()[0].lines.empty());
}

TEST_F(FightTest, AReportFromAPhaseThatHasMovedOnLandsNothing) {
  CountIn();
  FightUpdate update;
  update.set_phase(1);
  update.set_spot(3);
  update.add_lines()->set_damage(50);

  fight_.Report("one", update);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);
  EXPECT_TRUE(fight_.players()[0].lines.empty());
  // The spot is still taken even though the damage was dropped.
  EXPECT_EQ(fight_.players()[0].spot, 3);
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
  // A finished fight ignores hits.
  fight_.Hit("one", 0, 100);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);
}

TEST_F(FightTest, OneSpotHoldsOnePlayer) {
  CountIn();

  EXPECT_FALSE(fight_.MoveTo("one", 1));
  EXPECT_EQ(fight_.players()[0].spot, 0);
  EXPECT_FALSE(fight_.MoveTo("one", 4));

  // A spot frees up once its player moves away.
  ASSERT_TRUE(fight_.MoveTo("two", 3));
  EXPECT_TRUE(fight_.MoveTo("one", 1));
  EXPECT_FALSE(fight_.MoveTo("three", 1));
}

// Each report's table replaces the last, even from an earlier phase, and it is
// kept after the player leaves.
TEST_F(FightTest, TheLastBreakdownReportedIsKept) {
  CountIn();
  FightUpdate update;
  FightBreakdownRow* row = update.add_breakdown();
  row->set_skill("Blizzard");
  row->set_damage(10);
  fight_.Report("one", update);
  row->set_damage(30);
  update.set_phase(5);
  fight_.Report("one", update);
  fight_.Disconnect("one");

  ASSERT_EQ(fight_.players()[0].breakdown.size(), 1u);
  EXPECT_EQ(fight_.players()[0].breakdown[0].skill(), "Blizzard");
  EXPECT_EQ(fight_.players()[0].breakdown[0].damage(), 30);
}

TEST_F(FightTest, APlayerWhoGoesStopsHittingAndFreesTheirSpot) {
  CountIn();
  fight_.Disconnect("two");

  fight_.Hit("two", 0, 50);
  EXPECT_EQ(fight_.hp_fractions()[0], 1.0);
  EXPECT_FALSE(fight_.players()[1].present);
  EXPECT_TRUE(fight_.MoveTo("one", 1));
  // Rewards are split by who started the fight, not who stayed.
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
  // An abandoned fight skips the end pause.
  EXPECT_TRUE(fight_.done());
}

TEST_F(FightTest, AFightWithNothingToKillIsNoFight) {
  std::map<std::string, Mob> empty;
  PartyFight fight("p1-1", "zakum", boss_, 0, empty, PartyOf(1));
  EXPECT_EQ(fight.state(), PartyFightState::kAbandoned);

  PartyFight unknown("p1-2", "zakum", boss_, 4, mobs_, PartyOf(1));
  EXPECT_EQ(unknown.state(), PartyFightState::kAbandoned);
}

// The party gets one roll per drop, so a guaranteed drop goes to exactly one
// player.
TEST(FightDropsTest, ACertainDropIsDealtToExactlyOnePlayer) {
  Boss boss = Dropping(1.0);
  std::map<std::string, Mob> mobs = Mobs();
  PartyFight fight("p1-1", "zakum", boss, 0, mobs, PartyOf(3));
  Clear(fight);

  ASSERT_EQ(fight.state(), PartyFightState::kWon);
  int dealt = 0;
  for (const FightPlayer& player : fight.players()) {
    for (const FightAward& award : player.awards) {
      ++dealt;
      EXPECT_EQ(award.item(), "shard");
      EXPECT_EQ(award.count(), 1);
    }
  }
  // The mark has a zero rate, so it never drops.
  EXPECT_EQ(dealt, 1);
}

// A rate above one deals every copy.
TEST(FightDropsTest, EveryUnitOfARepeatedDropIsDealt) {
  Boss boss = Dropping(3.0);
  std::map<std::string, Mob> mobs = Mobs();
  PartyFight fight("p1-1", "zakum", boss, 0, mobs, PartyOf(3));
  Clear(fight);

  EXPECT_EQ(TotalAwards(fight), 3);
}

// The roll uses the party's best drop rate, so one player's 100% bonus turns
// a 50% drop into a sure one.
TEST(FightDropsTest, TheBestDropRateInThePartyRollsTheDrops) {
  Boss boss = Dropping(0.5);
  std::map<std::string, Mob> mobs = Mobs();
  PartyFight fight("p1-1", "zakum", boss, 0, mobs, PartyOf(3));
  FightUpdate rich;
  rich.set_item_drop_pct(1.0);
  fight.Report("three", rich);
  fight.Report("two", FightUpdate());
  Clear(fight);

  EXPECT_EQ(TotalAwards(fight), 1);
}

// Drop rate raises the chance of gear but never adds copies, so a guaranteed
// piece stays one piece.
TEST(FightDropsTest, DropRateDoesNotDoubleACertainPieceOfGear) {
  Boss boss = Dropping(0.0, 1.0);
  std::map<std::string, Mob> mobs = Mobs();
  PartyFight fight("p1-1", "zakum", boss, 0, mobs, PartyOf(3));
  FightUpdate rich;
  rich.set_item_drop_pct(2.0);
  fight.Report("one", rich);
  Clear(fight);

  EXPECT_EQ(TotalAwards(fight), 1);
}

// Stackable drops do get extra copies: 250% drop rate on a guaranteed shard
// gives two or three.
TEST(FightDropsTest, DropRateStacksACertainShard) {
  Boss boss = Dropping(1.0);
  std::map<std::string, Mob> mobs = Mobs();
  PartyFight fight("p1-1", "zakum", boss, 0, mobs, PartyOf(3));
  FightUpdate rich;
  rich.set_item_drop_pct(1.5);
  fight.Report("one", rich);
  Clear(fight);

  int total = TotalAwards(fight);
  EXPECT_GE(total, 2);
  EXPECT_LE(total, 3);
}

TEST(FightDropsTest, NothingIsDealtToAPlayerWhoHasGone) {
  Boss boss = Dropping(1.0);
  std::map<std::string, Mob> mobs = Mobs();
  PartyFight fight("p1-1", "zakum", boss, 0, mobs, PartyOf(3));
  fight.Advance(kBossCountdownSeconds);
  fight.Disconnect("two");
  fight.Disconnect("three");
  Clear(fight);

  ASSERT_EQ(fight.state(), PartyFightState::kWon);
  ASSERT_EQ(fight.players()[0].awards.size(), 1u);
  EXPECT_TRUE(fight.players()[1].awards.empty());
  EXPECT_TRUE(fight.players()[2].awards.empty());
}

}  // namespace
}  // namespace ms
