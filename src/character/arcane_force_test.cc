#include "src/character/arcane_force.h"

#include <gtest/gtest.h>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

EquipPrototype VanishingJourney() {
  EquipPrototype proto;
  proto.mutable_arcane_symbol()->set_meso_cost_base(8);
  return proto;
}

TEST(ArcaneForceTest, ASymbolClimbsTenAForceALevel) {
  EXPECT_EQ(SymbolArcaneForce(1), 30);
  EXPECT_EQ(SymbolArcaneForce(8), 100);
  EXPECT_EQ(SymbolArcaneForce(kMaxSymbolLevel), 220);
}

// GMS's level^2 + 11, and nothing past the cap.
TEST(ArcaneForceTest, DuplicatesPerLevel) {
  EXPECT_EQ(SymbolExpToNextLevel(1), 12);
  EXPECT_EQ(SymbolExpToNextLevel(19), 372);
  EXPECT_EQ(SymbolExpToNextLevel(kMaxSymbolLevel), 0);
  int total = 0;
  for (int level = 1; level < kMaxSymbolLevel; ++level) {
    total += SymbolExpToNextLevel(level);
  }
  EXPECT_EQ(total, 2679) << "the whole ladder from 1 to 20";
}

TEST(ArcaneForceTest, LevelUpCostRisesWithTheLevel) {
  // 10,000 x floor[(8 + 0.1) x 12].
  EXPECT_EQ(SymbolLevelUpCost(VanishingJourney(), 1), 970000);
  // 10,000 x floor[(8 + 1.9) x 372].
  EXPECT_EQ(SymbolLevelUpCost(VanishingJourney(), 19), 36820000);
  EXPECT_EQ(SymbolLevelUpCost(VanishingJourney(), kMaxSymbolLevel), 0);
  // The area is what makes a late symbol expensive: Esfera pays the same
  // duplicates at more than twice the price.
  EquipPrototype esfera;
  esfera.mutable_arcane_symbol()->set_meso_cost_base(18);
  EXPECT_GT(SymbolLevelUpCost(esfera, 1),
            SymbolLevelUpCost(VanishingJourney(), 1));
}

// A fresh drop writes nothing, so the zero it leaves has to read as level 1.
TEST(ArcaneForceTest, AFreshCopyIsLevelOne) {
  Equip item;
  EXPECT_EQ(SymbolLevel(item), 1);
  EXPECT_FALSE(SymbolCanLevelUp(item));
}

TEST(ArcaneForceTest, LevellingCarriesTheExcess) {
  Equip item;
  item.set_symbol_exp(20);
  ASSERT_TRUE(SymbolCanLevelUp(item));
  LevelUpSymbol(item);
  EXPECT_EQ(SymbolLevel(item), 2);
  EXPECT_EQ(item.symbol_exp(), 8) << "20 taken, 12 spent";
  // Level 2 asks for 15, which the 8 left over does not cover.
  EXPECT_FALSE(SymbolCanLevelUp(item));
}

TEST(ArcaneForceTest, TheCapRefusesAnotherLevel) {
  Equip item;
  item.set_symbol_level(kMaxSymbolLevel);
  item.set_symbol_exp(9999);
  EXPECT_FALSE(SymbolCanLevelUp(item));
  LevelUpSymbol(item);
  EXPECT_EQ(SymbolLevel(item), kMaxSymbolLevel);
}

// Ten of the wearer's primary stat per point of Arcane Force, and nothing in
// any other stat.
TEST(ArcaneForceTest, SymbolGrantsThePrimaryStat) {
  EquipStats str = SymbolStatsFor(STAT_FIELD_STR, 1);
  EXPECT_EQ(str.str(), 300);
  EXPECT_EQ(str.dex(), 0);
  EXPECT_EQ(str.attack(), 0);
  EXPECT_EQ(SymbolStatsFor(STAT_FIELD_INT, kMaxSymbolLevel).int_(), 2200);
  EXPECT_EQ(SymbolStatsFor(STAT_FIELD_LUK, 5).luk(), 700);
  // A job with no stat to grant walks away with nothing rather than with the
  // grant landing somewhere arbitrary.
  EXPECT_TRUE(
      SymbolStatsFor(STAT_FIELD_UNSPECIFIED, 5).SerializeAsString().empty());
}

// A map outside Arcane River asks for nothing, and nothing is what the
// factors do to it.
TEST(ArcaneForceTest, NoRequirementLeavesTheFightAlone) {
  ArcaneFactors none = ArcaneFactorsFor(0, 0);
  EXPECT_DOUBLE_EQ(none.damage_dealt, 1.0);
  EXPECT_DOUBLE_EQ(none.damage_taken, 1.0);
}

TEST(ArcaneForceTest, TheFactorTableStepsWithThePercentageMet) {
  struct Case {
    int owned;
    double dealt;
    double taken;
  };
  // Against a requirement of 100, so owned reads as the percentage met.
  const Case cases[] = {
      {0, 0.10, 2.8},   {9, 0.10, 2.8},   {10, 0.30, 2.4},  {29, 0.30, 2.4},
      {30, 0.60, 1.8},  {50, 0.70, 1.6},  {70, 0.80, 1.4},  {99, 0.80, 1.4},
      {100, 1.00, 1.0}, {109, 1.00, 1.0}, {110, 1.10, 0.8}, {130, 1.30, 0.4},
      {150, 1.50, 0.0}, {900, 1.50, 0.0},
  };
  for (const Case& c : cases) {
    ArcaneFactors factors = ArcaneFactorsFor(c.owned, 100);
    EXPECT_DOUBLE_EQ(factors.damage_dealt, c.dealt) << c.owned;
    EXPECT_DOUBLE_EQ(factors.damage_taken, c.taken) << c.owned;
  }
}

// The percentage is rounded down, so the point before a step buys nothing.
TEST(ArcaneForceTest, ThePercentageRoundsDown) {
  EXPECT_DOUBLE_EQ(ArcaneFactorsFor(38, 130).damage_dealt, 0.30);
  EXPECT_DOUBLE_EQ(ArcaneFactorsFor(39, 130).damage_dealt, 0.60);
  // A level-1 symbol is exactly what the first Vanishing Journey map asks.
  EXPECT_DOUBLE_EQ(ArcaneFactorsFor(SymbolArcaneForce(1), 30).damage_dealt,
                   1.00);
}

}  // namespace
}  // namespace ms
