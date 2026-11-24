#include "src/combat/boss/phase_rules.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Kaling's last phase in small: Kaling everywhere, three Perils in zones 1-3
// over spots {0,1}, {2,3}, {4,5}. Slots 0-3: Kaling, then the Perils.
BossPhase LastPhase() {
  BossPhase phase;
  Spawn* kaling = phase.add_spawns();
  kaling->set_mob("kaling");
  for (int step : {75, 50, 25}) {
    kaling->add_hp_steps(step);
  }
  for (int z = 1; z <= 3; ++z) {
    Spawn* peril = phase.add_spawns();
    peril->set_mob("peril" + std::to_string(z));
    peril->set_zone(z);
    PhaseZone* zone = phase.add_zones();
    zone->add_spots(2 * (z - 1));
    zone->add_spots(2 * (z - 1) + 1);
  }
  phase.mutable_crowding()->set_players(3);
  phase.mutable_crowding()->set_damage_pct(50);
  return phase;
}

const std::vector<int> kSlots = {0, 1, 2, 3};

TEST(PhaseRulesTest, ASpotsZoneIsTheOneListingIt) {
  BossPhase phase = LastPhase();
  EXPECT_EQ(ZoneOfSpot(phase, 0), 1);
  EXPECT_EQ(ZoneOfSpot(phase, 5), 3);
  EXPECT_EQ(ZoneOfSpot(phase, 6), 0);
}

TEST(PhaseRulesTest, APlayerReachesTheBossAndOnlyTheirZonesPeril) {
  BossPhase phase = LastPhase();
  std::vector<SlotRule> rules = SlotRules(
      phase, kSlots, {true, true, true, true}, {}, {}, 10.0, 0.0, 2, {2});
  EXPECT_TRUE(rules[0].reachable);
  EXPECT_FALSE(rules[1].reachable);
  EXPECT_TRUE(rules[2].reachable);
  EXPECT_FALSE(rules[3].reachable);
}

TEST(PhaseRulesTest, KalingsFloorDropsAStepPerDeadPeril) {
  BossPhase phase = LastPhase();
  EXPECT_DOUBLE_EQ(HpFloor(phase, kSlots, {true, true, true, true}, 0), 0.75);
  EXPECT_DOUBLE_EQ(HpFloor(phase, kSlots, {true, false, true, true}, 0), 0.5);
  EXPECT_DOUBLE_EQ(HpFloor(phase, kSlots, {true, false, false, false}, 0), 0.0);
  EXPECT_DOUBLE_EQ(HpFloor(phase, kSlots, {true, true, true, true}, 1), 0.0);
}

TEST(PhaseRulesTest, AnArmLeavesWhenTheBodyFallsToItsMark) {
  BossPhase phase;
  phase.add_spawns()->set_mob("body");
  Spawn* arm = phase.add_spawns();
  arm->set_mob("arm");
  arm->mutable_vanish()->set_mob("body");
  arm->mutable_vanish()->set_hp_pct(20);
  const std::vector<int> slots = {0, 1, 1};
  EXPECT_TRUE(HasSlotRules(phase));
  EXPECT_EQ(Vanishing(phase, slots, {0.21, 1.0, 1.0}),
            (std::vector<bool>{false, false, false}));
  EXPECT_EQ(Vanishing(phase, slots, {0.2, 1.0, 0.5}),
            (std::vector<bool>{false, true, true}));
  // A dead arm has nothing left to leave; a dead body sends the rest away.
  EXPECT_EQ(Vanishing(phase, slots, {0.0, 0.0, 0.5}),
            (std::vector<bool>{false, false, true}));
}

// Three on one Peril: all three deal half to it, and full to Kaling.
TEST(PhaseRulesTest, ACrowdedZoneHalvesItsPerilOnly) {
  BossPhase phase = LastPhase();
  std::vector<bool> alive = {true, true, true, true};
  std::vector<SlotRule> crowded =
      SlotRules(phase, kSlots, alive, {}, {}, 0.0, 0.0, 0, {0, 1, 6});
  EXPECT_DOUBLE_EQ(crowded[1].damage_taken, 1.0);
  crowded = SlotRules(phase, kSlots, alive, {}, {}, 0.0, 0.0, 0, {0, 1, 0});
  EXPECT_DOUBLE_EQ(crowded[1].damage_taken, 0.5);
  EXPECT_DOUBLE_EQ(crowded[0].damage_taken, 1.0);
}

// Hundun hides for 12s of every minute, from the first minute on.
TEST(PhaseRulesTest, AnInvulnerableWindowOpensEachIntervalForItsLength) {
  Spawn hundun;
  hundun.mutable_invulnerable()->set_interval_ms(60000);
  hundun.mutable_invulnerable()->set_open_ms(12000);
  EXPECT_FALSE(Invulnerable(hundun, 5.0, 0.0));
  EXPECT_TRUE(Invulnerable(hundun, 60.0, 0.0));
  EXPECT_TRUE(Invulnerable(hundun, 71.9, 0.0));
  EXPECT_FALSE(Invulnerable(hundun, 72.0, 0.0));
  EXPECT_TRUE(Invulnerable(hundun, 125.0, 0.0));

  BossPhase phase;
  *phase.add_spawns() = hundun;
  EXPECT_FALSE(
      SlotRules(phase, {0}, {true}, {}, {}, 61.0, 0.0, -1, {})[0].reachable);
}

// The First Adversary's Power Cycle starts over with each phase.
TEST(PhaseRulesTest, AnInvulnerableWindowCountsFromThePhasesStart) {
  Spawn adversary;
  adversary.mutable_invulnerable()->set_interval_ms(138000);
  adversary.mutable_invulnerable()->set_open_ms(2880);
  EXPECT_FALSE(Invulnerable(adversary, 138.0, 100.0));
  EXPECT_TRUE(Invulnerable(adversary, 238.0, 100.0));
  EXPECT_TRUE(Invulnerable(adversary, 240.8, 100.0));
  EXPECT_FALSE(Invulnerable(adversary, 240.9, 100.0));
}

// Hard Lucid can't be hit while she enters her last phase, from whenever it
// began on the fight's clock.
TEST(PhaseRulesTest, AnEntryWindowCountsFromThePhasesStart) {
  Spawn lucid;
  lucid.set_entry_invulnerable_ms(4520);
  EXPECT_TRUE(Invulnerable(lucid, 300.0, 300.0));
  EXPECT_TRUE(Invulnerable(lucid, 304.5, 300.0));
  EXPECT_FALSE(Invulnerable(lucid, 304.53, 300.0));
  EXPECT_FALSE(Invulnerable(Spawn(), 300.0, 300.0));

  BossPhase phase;
  *phase.add_spawns() = lucid;
  EXPECT_TRUE(HasSlotRules(phase));
  EXPECT_FALSE(
      SlotRules(phase, {0}, {true}, {}, {}, 302.0, 300.0, -1, {})[0].reachable);
}

// Gloom's guard: up but for 30s from each strike, which comes every 55s from
// the last one and at once when its HP falls to 70% and 40%.
BossPhase GuardedPhase() {
  BossPhase phase;
  SpawnGuard* guard = phase.add_spawns()->mutable_guard();
  guard->set_damage_pct(10);
  guard->set_interval_ms(55000);
  guard->set_open_ms(30000);
  guard->add_hp_opens(70);
  guard->add_hp_opens(40);
  return phase;
}

TEST(PhaseRulesTest, AGuardCutsHitsUntilItsIntervalOpensIt) {
  BossPhase phase = GuardedPhase();
  std::vector<GuardState> guards;
  AdvanceGuards(phase, {0}, {1.0}, 54.0, guards);
  EXPECT_DOUBLE_EQ(
      SlotRules(phase, {0}, {true}, guards, {}, 54.0, 0.0, -1, {})[0]
          .damage_taken,
      0.1);
  AdvanceGuards(phase, {0}, {1.0}, 56.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 55.0);
  EXPECT_DOUBLE_EQ(
      SlotRules(phase, {0}, {true}, guards, {}, 56.0, 0.0, -1, {})[0]
          .damage_taken,
      1.0);
  EXPECT_TRUE(GuardOpen(phase.spawns(0).guard(), 55.0, 84.9));
  EXPECT_FALSE(GuardOpen(phase.spawns(0).guard(), 55.0, 85.0));
  AdvanceGuards(phase, {0}, {1.0}, 220.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 220.0) << "every 55s after";
}

TEST(PhaseRulesTest, AGuardOpensAtItsHpMarksAndRestartsItsInterval) {
  BossPhase phase = GuardedPhase();
  std::vector<GuardState> guards;
  AdvanceGuards(phase, {0}, {0.69}, 40.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 40.0);
  EXPECT_EQ(guards[0].hp_opens_spent, 1);
  AdvanceGuards(phase, {0}, {0.69}, 94.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 40.0) << "55s from the strike, not 0";
  AdvanceGuards(phase, {0}, {0.69}, 95.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 95.0);
  // Already open at 40%: the mark is spent and changes nothing.
  AdvanceGuards(phase, {0}, {0.39}, 100.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 95.0);
  EXPECT_EQ(guards[0].hp_opens_spent, 2);
}

// What the fight is told so a burst can wait: before the first opening, while
// open, and after.
TEST(PhaseRulesTest, AGuardSaysWhenItNextOpens) {
  const SpawnGuard& guard = GuardedPhase().spawns(0).guard();
  GuardTimes before = GuardTimesAt(guard, -1.0, 40.0);
  EXPECT_DOUBLE_EQ(before.cut, 0.1);
  EXPECT_DOUBLE_EQ(before.open_left, 0.0);
  EXPECT_DOUBLE_EQ(before.opens_in, 15.0);
  EXPECT_DOUBLE_EQ(before.open_seconds, 30.0);
  EXPECT_DOUBLE_EQ(before.interval_seconds, 55.0);
  GuardTimes open = GuardTimesAt(guard, 55.0, 60.0);
  EXPECT_DOUBLE_EQ(open.open_left, 25.0);
  EXPECT_DOUBLE_EQ(open.opens_in, 50.0);
  GuardTimes shut = GuardTimesAt(guard, 55.0, 100.0);
  EXPECT_DOUBLE_EQ(shut.open_left, 0.0);
  EXPECT_DOUBLE_EQ(shut.opens_in, 10.0);
  BossPhase phase = GuardedPhase();
  std::vector<GuardState> guards(1);
  guards[0].opened_at = 55.0;
  EXPECT_DOUBLE_EQ(
      SlotRules(phase, {0}, {true}, guards, {}, 100.0, 0.0, -1, {})[0]
          .guard.opens_in,
      10.0);
}

TEST(PhaseRulesTest, TheFirstGuardedSlotSaysWhenThePhaseOpened) {
  BossPhase phase = GuardedPhase();
  *phase.add_spawns() = Spawn();
  std::swap(*phase.mutable_spawns(0), *phase.mutable_spawns(1));
  std::vector<GuardState> guards(2);
  guards[1].opened_at = 55.0;
  EXPECT_DOUBLE_EQ(PhaseGuardOpenedAt(phase, {0, 1}, guards), 55.0);
  EXPECT_DOUBLE_EQ(PhaseGuardOpenedAt(BossPhase(), {}, {}), -1.0);
}

TEST(PhaseRulesTest, ALivingGiverShieldsTheMobsItNamesOnItsClock) {
  BossPhase phase;
  Spawn* taowu = phase.add_spawns();
  taowu->set_mob("taowu");
  taowu->mutable_shield()->set_interval_ms(45000);
  taowu->mutable_shield()->set_percent(1);
  taowu->mutable_shield()->add_mobs("hundun");
  phase.add_spawns()->set_mob("hundun");
  phase.add_spawns()->set_mob("qiongqi");
  const std::vector<int> slots = {0, 1, 2};

  std::vector<double> gained =
      ShieldsBetween(phase, slots, {true, true, true}, {}, 0.0, 40.0, 91.0);
  EXPECT_DOUBLE_EQ(gained[0], 0.0);
  EXPECT_DOUBLE_EQ(gained[1], 0.02);
  EXPECT_DOUBLE_EQ(gained[2], 0.0);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, slots, {false, true, true}, {}, 0.0, 40.0, 91.0)[1],
      0.0);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, slots, {true, true, true}, {}, 0.0, 45.0, 89.0)[1],
      0.0);
}

// Lotus: 0.5% every 40s for 20s, hits cut to 10% while it holds.
TEST(PhaseRulesTest, ASelfShieldRisesOnItsClockAndCutsHitsWhileItHolds) {
  BossPhase phase;
  SelfShield* shield = phase.add_spawns()->mutable_self_shield();
  shield->set_percent(0.5);
  shield->set_interval_ms(40000);
  shield->set_duration_ms(20000);
  shield->set_damage_pct(10);
  EXPECT_DOUBLE_EQ(ShieldsBetween(phase, {0}, {true}, {0.0}, 0.0, 0.0, 39.0)[0],
                   0.0);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {true}, {0.0}, 0.0, 39.0, 41.0)[0], 0.005);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {true}, {0.0}, 0.0, 79.0, 81.0)[0], 0.005);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {false}, {0.0}, 0.0, 79.0, 81.0)[0], 0.0);
  EXPECT_FALSE(SelfShieldsEnding(phase, {0}, 0.0, 41.0, 59.0)[0]);
  EXPECT_TRUE(SelfShieldsEnding(phase, {0}, 0.0, 59.0, 61.0)[0]);
  EXPECT_DOUBLE_EQ(
      SlotRules(phase, {0}, {true}, {}, {0.004}, 45.0, 0.0, -1, {})[0]
          .damage_taken,
      0.1);
  EXPECT_DOUBLE_EQ(
      SlotRules(phase, {0}, {true}, {}, {0.0}, 45.0, 0.0, -1, {})[0]
          .damage_taken,
      1.0)
      << "broken";
}

// Seren: 10% as Dawn begins, growing while it holds, ended with Dawn.
TEST(PhaseRulesTest, AFormsShieldFollowsThePhasesOwnClock) {
  BossPhase phase;
  for (const auto& [name, seconds] :
       std::vector<std::pair<std::string, int>>{{"Noon", 120}, {"Dawn", 130}}) {
    PhaseForm* form = phase.add_forms();
    form->set_name(name);
    form->set_seconds(seconds);
  }
  SelfShield* shield = phase.add_spawns()->mutable_self_shield();
  shield->set_percent(10);
  shield->set_form("Dawn");
  shield->set_percent_per_second(0.02);
  // The phase began at 100, so Dawn holds from 220 to 350, then 470 to 600.
  std::vector<ShieldSpan> spans =
      SelfShieldSpans(phase, *shield, 100.0, 0.0, 500.0);
  ASSERT_EQ(spans.size(), 2u);
  EXPECT_DOUBLE_EQ(spans[0].start, 220.0);
  EXPECT_DOUBLE_EQ(spans[0].end, 350.0);
  EXPECT_DOUBLE_EQ(spans[1].start, 470.0);
  EXPECT_NEAR(ShieldsBetween(phase, {0}, {true}, {0.0}, 100.0, 219.0, 221.0)[0],
              0.1 + 0.0002, 1e-12);
  EXPECT_NEAR(
      ShieldsBetween(phase, {0}, {true}, {0.05}, 100.0, 300.0, 310.0)[0], 0.002,
      1e-12);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {true}, {0.0}, 100.0, 300.0, 310.0)[0], 0.0)
      << "a broken shield stops growing";
  EXPECT_TRUE(SelfShieldsEnding(phase, {0}, 100.0, 349.0, 351.0)[0]);
}

// Black Mage's last phase: 3T of 47.9T every 30s, held until broken, a new one
// topping up what is left rather than adding to it.
TEST(PhaseRulesTest, AShieldHeldUntilBrokenIsToppedUpNotStacked) {
  BossPhase phase;
  SelfShield* shield = phase.add_spawns()->mutable_self_shield();
  shield->set_percent(6);
  shield->set_interval_ms(30000);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {true}, {0.0}, 0.0, 29.0, 31.0)[0], 0.06);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {true}, {0.04}, 0.0, 59.0, 61.0)[0], 0.02)
      << "topped up to 6%";
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, {0}, {true}, {0.06}, 0.0, 89.0, 91.0)[0], 0.0);
  EXPECT_FALSE(SelfShieldsEnding(phase, {0}, 0.0, 0.0, 600.0)[0])
      << "it never runs out";
}

// The knights' shield runs out after 15s and is simply gone, where Lotus's
// leftover heals him.
TEST(PhaseRulesTest, AVanishingShieldSaysSo) {
  BossPhase phase;
  SelfShield* lotus = phase.add_spawns()->mutable_self_shield();
  lotus->set_percent(0.5);
  lotus->set_interval_ms(40000);
  lotus->set_duration_ms(20000);
  SelfShield* knight = phase.add_spawns()->mutable_self_shield();
  *knight = *lotus;
  knight->set_vanishes(true);
  EXPECT_FALSE(ShieldVanishes(phase, {0, 1}, 0));
  EXPECT_TRUE(ShieldVanishes(phase, {0, 1}, 1));
  EXPECT_TRUE(SelfShieldsEnding(phase, {0, 1}, 0.0, 59.0, 61.0)[1]);
}

// The knights share one HP, so each is the other's partner, and a third
// monster is neither's.
TEST(PhaseRulesTest, MonstersSharingHpArePartners) {
  BossPhase phase;
  phase.add_spawns()->set_mob("aeonian_rise");
  Spawn* ruin = phase.add_spawns();
  ruin->set_mob("tanadian_ruin");
  ruin->set_shares_hp("aeonian_rise");
  phase.add_spawns()->set_mob("bystander");
  EXPECT_EQ(HpPartners(phase, {0, 1, 2}, 0), std::vector<int>({1}));
  EXPECT_EQ(HpPartners(phase, {0, 1, 2}, 1), std::vector<int>({0}));
  EXPECT_TRUE(HpPartners(phase, {0, 1, 2}, 2).empty());
}

}  // namespace
}  // namespace ms
