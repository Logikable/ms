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
  std::vector<SlotRule> rules =
      SlotRules(phase, kSlots, {true, true, true, true}, {}, 10.0, 0.0, 2, {2});
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

// Three on one Peril: all three deal half to it, and full to Kaling.
TEST(PhaseRulesTest, ACrowdedZoneHalvesItsPerilOnly) {
  BossPhase phase = LastPhase();
  std::vector<bool> alive = {true, true, true, true};
  std::vector<SlotRule> crowded =
      SlotRules(phase, kSlots, alive, {}, 0.0, 0.0, 0, {0, 1, 6});
  EXPECT_DOUBLE_EQ(crowded[1].damage_taken, 1.0);
  crowded = SlotRules(phase, kSlots, alive, {}, 0.0, 0.0, 0, {0, 1, 0});
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
      SlotRules(phase, {0}, {true}, {}, 61.0, 0.0, -1, {})[0].reachable);
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
      SlotRules(phase, {0}, {true}, {}, 302.0, 300.0, -1, {})[0].reachable);
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
      SlotRules(phase, {0}, {true}, guards, 54.0, 0.0, -1, {})[0].damage_taken,
      0.1);
  AdvanceGuards(phase, {0}, {1.0}, 56.0, guards);
  EXPECT_DOUBLE_EQ(guards[0].opened_at, 55.0);
  EXPECT_DOUBLE_EQ(
      SlotRules(phase, {0}, {true}, guards, 56.0, 0.0, -1, {})[0].damage_taken,
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
      ShieldsBetween(phase, slots, {true, true, true}, 40.0, 91.0);
  EXPECT_DOUBLE_EQ(gained[0], 0.0);
  EXPECT_DOUBLE_EQ(gained[1], 0.02);
  EXPECT_DOUBLE_EQ(gained[2], 0.0);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, slots, {false, true, true}, 40.0, 91.0)[1], 0.0);
  EXPECT_DOUBLE_EQ(
      ShieldsBetween(phase, slots, {true, true, true}, 45.0, 89.0)[1], 0.0);
}

}  // namespace
}  // namespace ms
