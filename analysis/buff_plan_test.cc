#include "analysis/buff_plan.h"

#include <gtest/gtest.h>

#include <vector>

#include "absl/types/span.h"
#include "analysis/meso_rate.h"
#include "src/character/consumables.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A level 240 character on a map of one level 264 mob paying meso alone, about
// 7,000 a kill bare and 12,000 under the Wealth potion, which the plan switches
// on first in every case here.
class BuffPlanTest : public ::testing::Test {
 protected:
  BuffPlanTest() : state_({}, {}, {}, {}, {}) {
    Character grown = state_.character.ToProto();
    grown.set_level(240);
    grown.set_meso(1'000'000);
    state_.character.RestoreFrom(grown, state_.equips, state_.items);
    Mob mob;
    mob.set_level(264);
    yield_.crowd.mobs = {mob};
    yield_.crowd.drops = {0.0};
    yield_.crowd.kills_per_second = {bare_[0]};
  }

  // Plans with the kill rates on the bare map, the Wild Totem's beat and the
  // Frenzy Totem's.
  void Plan(double bare, double wild, double frenzy) {
    bare_ = {bare};
    wild_ = {wild};
    frenzy_ = {frenzy};
    yield_.crowd.kills_per_second = bare_;
    yield_.kills_without_totem = absl::MakeConstSpan(bare_);
    yield_.totems = {
        {CONSUMABLE_TYPE_WILD_TOTEM, absl::MakeConstSpan(wild_)},
        {CONSUMABLE_TYPE_FRENZY_TOTEM, absl::MakeConstSpan(frenzy_)}};
    BuffPolicy policy;
    policy.mode = BuffMode::kRent;
    BuffSpend spend;
    PlanBuffs(state_, policy, yield_, &spend);
  }

  bool On(ConsumableType type) const {
    return state_.character.ConsumableActive(type);
  }

  GameState state_;
  BuffYield yield_;
  std::vector<double> bare_ = {0.0};
  std::vector<double> wild_;
  std::vector<double> frenzy_;
};

// Killing all the Frenzy Totem spawns pays far past its 20,000 a second.
TEST_F(BuffPlanTest, ACappedClearTakesTheFrenzyTotem) {
  Plan(1.0, 2.0, 10.0);
  EXPECT_TRUE(On(CONSUMABLE_TYPE_FRENZY_TOTEM));
  EXPECT_FALSE(On(CONSUMABLE_TYPE_WILD_TOTEM));
}

// A few more kills cover the Wild Totem's 2,000 but not the Frenzy Totem's
// rent, even though the Frenzy Totem kills more.
TEST_F(BuffPlanTest, AModestGainTakesTheWildTotem) {
  Plan(0.5, 1.0, 1.8);
  EXPECT_TRUE(On(CONSUMABLE_TYPE_WILD_TOTEM));
  EXPECT_FALSE(On(CONSUMABLE_TYPE_FRENZY_TOTEM));
}

// A character who wasn't waiting on spawns kills no more with either.
TEST_F(BuffPlanTest, ADamageBoundClearTakesNoTotem) {
  Plan(1.0, 1.0, 1.0);
  EXPECT_FALSE(On(CONSUMABLE_TYPE_WILD_TOTEM));
  EXPECT_FALSE(On(CONSUMABLE_TYPE_FRENZY_TOTEM));
}

// An owned Wild Totem costs nothing to run, so it beats a rented Frenzy Totem
// whose extra kills don't cover the rent.
TEST_F(BuffPlanTest, AnOwnedWildTotemBeatsARentedFrenzyThatDoesntPay) {
  state_.character.AddMeso(
      ConsumableInfoFor(CONSUMABLE_TYPE_WILD_TOTEM)->permanent_price);
  ASSERT_TRUE(state_.character.BuyConsumable(CONSUMABLE_TYPE_WILD_TOTEM));
  Plan(0.5, 0.6, 1.5);
  EXPECT_TRUE(On(CONSUMABLE_TYPE_WILD_TOTEM));
  EXPECT_FALSE(On(CONSUMABLE_TYPE_FRENZY_TOTEM));
}

}  // namespace
}  // namespace ms
