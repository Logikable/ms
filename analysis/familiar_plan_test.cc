#include "analysis/familiar_plan.h"

#include <vector>

#include "gtest/gtest.h"
#include "src/character/character_stats.h"
#include "src/character/familiar.h"
#include "src/character/skill_placement.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/familiar.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Damage that only boss damage and ignored defence move, enough to rank lines.
double BossPower(GameState& state) {
  DerivedStats derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                         Activity::kBossing);
  return 1.0 + derived.boss_pct + derived.ied;
}

// Puts the character at `level`, where Familiars may or may not be open.
void SetLevel(GameState& state, int level) {
  Character proto = state.character.ToProto();
  proto.set_level(level);
  state.character.RestoreFrom(proto, state.equips, state.items);
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
  SetLevel(state, kFamiliarsLevel - 1);
  FamiliarBook book;
  book.set_exp(1'000'000);
  state.character.set_familiars(book);
  EXPECT_EQ(SpendFamiliars(state, BossPower, {}).levels, 0);
  EXPECT_EQ(state.character.familiars().exp(), 1'000'000);
}

// With no Familiar Bond in the catalog, every step goes to the three mains,
// which end Legendary and summoned in every preset.
TEST(SpendFamiliarsTest, TheMainsClimbToLegendary) {
  GameState state({}, {}, {}, {}, {});
  SetLevel(state, kFamiliarsLevel);
  state.character.AddFamiliarExp(3 * (1'000 + 9'000 + 50'000 + 200'000));
  FamiliarSpend spend = SpendFamiliars(state, BossPower, {});
  EXPECT_EQ(spend.levels, 12);
  EXPECT_EQ(state.character.familiars().exp(), 0);
  for (int i = 0; i < kMaxSummonedFamiliars; ++i) {
    EXPECT_EQ(
        FamiliarLevel(state.character.familiars(), FamiliarRoster()[i].name),
        kFamiliarMaxLevel);
  }
  EXPECT_EQ(state.character.summoned_familiars(StatPreset::kSecond).size(),
            kMaxSummonedFamiliars);
}

// A Familiar Bond whose fourth and fifth levels add nothing the damage reads
// and whose sixth adds ignored defence.
Skill LateBond() {
  Skill bond;
  bond.set_name("Familiar Bond");
  bond.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(bond, JOB_ADVANCEMENT_BEGINNER);
  bond.set_familiar_levels(true);
  bond.set_max_level(6);
  SkillStep* worthless = bond.add_step();
  worthless->set_from_level(4);
  worthless->mutable_base()->set_crit_rate(0.03);
  SkillStep* ied = bond.add_step();
  ied->set_from_level(6);
  ied->mutable_base()->set_ied_pct(0.15);
  return bond;
}

// Levels that are worth nothing alone are still bought on the way to one
// that is.
TEST(SpendFamiliarsTest, BondClimbsPastAWorthlessLevel) {
  GameState state({}, {}, {}, {}, {}, {{"familiar_bond", LateBond()}});
  SetLevel(state, kFamiliarsLevel);
  const int64_t legendary = 1'000 + 9'000 + 50'000 + 200'000;
  state.character.AddFamiliarExp(static_cast<int64_t>(FamiliarRoster().size()) *
                                 legendary);
  SpendFamiliars(state, BossPower, {});
  EXPECT_EQ(
      FamiliarSkillLevel(TotalFamiliarLevels(state.character.familiars())), 6);
}

// Cubes stop on lines worth the reservation value and are paid in meso.
TEST(SpendFamiliarsTest, CubesFollowTheStoppingRule) {
  GameState state({}, {}, {}, {}, {});
  SetLevel(state, kFamiliarsLevel);
  state.character.AddFamiliarExp(3 * (1'000 + 9'000 + 50'000 + 200'000));
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
