#include "src/combat/clear_time.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A map of `count` one-hit monsters, felled one a second, whose own respawn
// timer would never come round.
CombatParams OneASecond(const Mob* mob, int count, double damage = 100.0) {
  CombatParams params;
  params.active = true;
  params.encounter = "field";
  params.respawn_seconds = 1e9;
  CombatType type;
  type.mob = mob;
  type.simultaneous = count;
  params.types.push_back(type);
  AttackOption attack;
  attack.max_enemies = 1;
  attack.swing_seconds = 1.0;
  attack.damage_per_hit = {damage};
  params.attacks.push_back(std::move(attack));
  return params;
}

Mob OneHitMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_max_hp(100);
  return mob;
}

// The map refills as soon as it's empty, so a 1e9-second timer still gives a
// clear every four kills.
TEST(ClearTimesTest, TheMapRefillsTheMomentItEmpties) {
  Mob mob = OneHitMob();
  std::vector<double> times = ClearTimes(OneASecond(&mob, 4), 100.0, 0.25);
  ASSERT_GE(times.size(), 15u);
  for (double t : times) {
    EXPECT_NEAR(t, 4.0, 1.0);
  }
}

TEST(ClearTimesTest, NeverClearingIsOneClearOfTheWholeRun) {
  Mob mob = OneHitMob();
  EXPECT_EQ(ClearTimes(OneASecond(&mob, 4, /*damage=*/0.0), 50.0, 0.25),
            (std::vector<double>{50.0}));
}

TEST(ClearTimesTest, NoFightNoClears) {
  EXPECT_TRUE(ClearTimes(CombatParams(), 50.0).empty());
}

TEST(ClearPercentileTest, NearestRank) {
  std::vector<double> times = {5, 1, 4, 2, 3, 6, 7, 8, 9, 10};
  EXPECT_EQ(ClearPercentile(times, 0.5), 5.0);
  EXPECT_EQ(ClearPercentile(times, 0.9), 9.0);
  EXPECT_EQ(ClearPercentile(times, 1.0), 10.0);
  EXPECT_EQ(ClearPercentile({7.0}, 0.9), 7.0);
  EXPECT_EQ(ClearPercentile({}, 0.5), 0.0);
}

}  // namespace
}  // namespace ms
