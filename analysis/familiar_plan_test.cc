#include "analysis/familiar_plan.h"

#include <vector>

#include "gtest/gtest.h"
#include "src/character/character_stats.h"
#include "src/character/familiar.h"
#include "src/game_state.h"
#include "src/protos/familiar.pb.h"

namespace ms {
namespace {

// Damage that only boss damage and ignored defence move, enough to rank lines.
double BossPower(GameState& state) {
  DerivedStats derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                         Activity::kBossing);
  return 1.0 + derived.boss_pct + derived.ied;
}

TEST(FamiliarReserveTest, FreeRollsHoldOutForTheBest) {
  EXPECT_DOUBLE_EQ(FamiliarReserve({1.0, 3.0}, {0.5, 0.5}, 0.0), 3.0);
}

// With two equal outcomes, r solves 0.5 * 3 + 0.5 * r - price = r.
TEST(FamiliarReserveTest, APriceLowersTheBar) {
  EXPECT_NEAR(FamiliarReserve({1.0, 3.0}, {0.5, 0.5}, 0.5), 2.0, 1e-9);
  EXPECT_NEAR(FamiliarReserve({1.0, 3.0}, {0.5, 0.5}, 5.0), 1.0, 1e-9)
      << "too dear to roll at all";
}

TEST(SpendFamiliarsTest, NothingBeforeFamiliarsOpen) {
  GameState state({}, {}, {}, {}, {});
  state.account.AddFamiliarExp(1'000'000);
  EXPECT_EQ(SpendFamiliars(state, BossPower, {}).levels, 0);
  EXPECT_EQ(state.account.familiars().exp(), 1'000'000);
}

// With no Familiar Bond in the catalog, every step goes to the three mains,
// which end Legendary and summoned in every preset.
TEST(SpendFamiliarsTest, TheMainsClimbToLegendary) {
  GameState state({}, {}, {}, {}, {});
  state.account.RecordProgress(kFamiliarsLevel, 4);
  state.MirrorAccount();
  state.account.AddFamiliarExp(3 * (1'000 + 9'000 + 50'000 + 200'000));
  FamiliarSpend spend = SpendFamiliars(state, BossPower, {});
  EXPECT_EQ(spend.levels, 12);
  EXPECT_EQ(state.account.familiars().exp(), 0);
  for (int i = 0; i < kMaxSummonedFamiliars; ++i) {
    EXPECT_EQ(
        FamiliarLevel(state.account.familiars(), FamiliarRoster()[i].name),
        kFamiliarMaxLevel);
  }
  EXPECT_EQ(state.character.summoned_familiars(StatPreset::kSecond).size(),
            kMaxSummonedFamiliars);
}

// Cubes stop on lines worth the reservation value and are paid in meso.
TEST(SpendFamiliarsTest, CubesFollowTheStoppingRule) {
  GameState state({}, {}, {}, {}, {});
  state.account.RecordProgress(kFamiliarsLevel, 4);
  state.MirrorAccount();
  state.account.AddFamiliarExp(3 * (1'000 + 9'000 + 50'000 + 200'000));
  state.character.AddMeso(100 * kFamiliarCubeMeso);
  FamiliarPrices prices;
  prices.power_per_meso = 0.02 / kFamiliarCubeMeso;
  FamiliarSpend spend = SpendFamiliars(state, BossPower, prices);
  EXPECT_GT(spend.cubes, 0);
  EXPECT_LT(spend.cubes, 100) << "the rule stopped before the meso ran out";
  EXPECT_EQ(spend.meso, spend.cubes * kFamiliarCubeMeso);
  const DerivedStats boss = DerivedStatsFor(state.character, state.skills, {},
                                            {}, Activity::kBossing);
  EXPECT_GT(boss.boss_pct + boss.ied, 1.0);
}

}  // namespace
}  // namespace ms
