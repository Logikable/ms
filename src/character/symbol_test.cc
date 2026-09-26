#include "src/character/symbol.h"

#include <gtest/gtest.h>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class SymbolTest : public testing::Test {
 protected:
  static EquipPrototype VanishingJourney() {
    EquipPrototype proto;
    proto.mutable_arcane_symbol()->set_meso_cost_base(8);
    return proto;
  }
  static EquipPrototype Sacred(double base) {
    EquipPrototype proto;
    proto.mutable_sacred_symbol()->set_meso_cost_base(base);
    return proto;
  }
  // The ladder from level 1 to the max, in duplicates.
  static int Ladder(const EquipPrototype& proto) {
    int total = 0;
    for (int level = 1; level < SymbolMaxLevel(proto); ++level) {
      total += SymbolExpToNextLevel(proto, level);
    }
    return total;
  }
};

class ArcaneSymbolTest : public SymbolTest {};
class SacredSymbolTest : public SymbolTest {};

TEST_F(SymbolTest, KindComesFromTheBlock) {
  EXPECT_TRUE(IsArcaneSymbol(VanishingJourney()));
  EXPECT_FALSE(IsSacredSymbol(VanishingJourney()));
  EXPECT_TRUE(IsSacredSymbol(Sacred(13.2)));
  EXPECT_TRUE(IsSymbol(Sacred(13.2)));
  EXPECT_FALSE(IsSymbol(EquipPrototype()));
  EXPECT_EQ(SymbolMaxLevel(EquipPrototype()), 0);
}

// A new drop sets nothing, so its zero must read as level 1.
TEST_F(SymbolTest, AFreshCopyIsLevelOne) {
  Equip item;
  EXPECT_EQ(SymbolLevel(item), 1);
  EXPECT_FALSE(SymbolCanLevelUp(VanishingJourney(), item));
  EXPECT_FALSE(SymbolMaxed(VanishingJourney(), item));
}

TEST_F(ArcaneSymbolTest, ForceAndStats) {
  EquipPrototype proto = VanishingJourney();
  EXPECT_EQ(SymbolForce(proto, 1), 30);
  EXPECT_EQ(SymbolForce(proto, 8), 100);
  EXPECT_EQ(SymbolForce(proto, kMaxArcaneSymbolLevel), 220);
  // Ten of the wearer's primary stat per point of Arcane Force, and nothing in
  // any other stat.
  EquipStats str = SymbolStatsFor(proto, STAT_FIELD_STR, 1);
  EXPECT_EQ(str.str(), 300);
  EXPECT_EQ(str.dex(), 0);
  EXPECT_EQ(str.attack(), 0);
  EXPECT_EQ(SymbolStatsFor(proto, STAT_FIELD_INT, kMaxArcaneSymbolLevel).int_(),
            2200);
  EXPECT_EQ(SymbolStatsFor(proto, STAT_FIELD_LUK, 5).luk(), 700);
  // A job with no stat to grant gets nothing, instead of the grant going to
  // some arbitrary stat.
  EXPECT_TRUE(SymbolStatsFor(proto, STAT_FIELD_UNSPECIFIED, 5)
                  .SerializeAsString()
                  .empty());
}

// GMS's level^2 + 11, and nothing past the max level.
TEST_F(ArcaneSymbolTest, DuplicatesAndMeso) {
  EquipPrototype proto = VanishingJourney();
  EXPECT_EQ(SymbolExpToNextLevel(proto, 1), 12);
  EXPECT_EQ(SymbolExpToNextLevel(proto, 19), 372);
  EXPECT_EQ(SymbolExpToNextLevel(proto, kMaxArcaneSymbolLevel), 0);
  EXPECT_EQ(Ladder(proto), 2679);
  // 10,000 x floor[(8 + 0.1) x 12], then x 372 at level 19.
  EXPECT_EQ(SymbolLevelUpCost(proto, 1), 970000);
  EXPECT_EQ(SymbolLevelUpCost(proto, 19), 36820000);
  EXPECT_EQ(SymbolLevelUpCost(proto, kMaxArcaneSymbolLevel), 0);
}

TEST_F(ArcaneSymbolTest, LevellingCarriesTheExcessUpToTheCap) {
  EquipPrototype proto = VanishingJourney();
  Equip item;
  item.set_symbol_exp(20);
  ASSERT_TRUE(SymbolCanLevelUp(proto, item));
  LevelUpSymbol(proto, item);
  EXPECT_EQ(SymbolLevel(item), 2);
  EXPECT_EQ(item.symbol_exp(), 8) << "20 taken, 12 spent";
  EXPECT_FALSE(SymbolCanLevelUp(proto, item)) << "level 2 needs 15";

  Equip capped;
  capped.set_symbol_level(kMaxArcaneSymbolLevel);
  capped.set_symbol_exp(9999);
  EXPECT_TRUE(SymbolMaxed(proto, capped));
  EXPECT_FALSE(SymbolCanLevelUp(proto, capped));
  LevelUpSymbol(proto, capped);
  EXPECT_EQ(SymbolLevel(capped), kMaxArcaneSymbolLevel);
}

// What a spare symbol is worth fed to another: itself, its levels, and its
// extra EXP. The packing a daily claim does must unpack to what went in: twenty
// copies make a level 2 with 7 EXP.
TEST_F(ArcaneSymbolTest, AWorthCountsTheLevelsBankedInIt) {
  EquipPrototype proto = VanishingJourney();
  EXPECT_EQ(SymbolWorth(proto, Equip()), 1);
  Equip banked;
  banked.set_symbol_exp(4);
  EXPECT_EQ(SymbolWorth(proto, banked), 5);
  Equip packed;
  packed.set_symbol_level(2);
  packed.set_symbol_exp(7);
  EXPECT_EQ(SymbolWorth(proto, packed), 20) << "1 + 12 + 7";
  Equip maxed;
  maxed.set_symbol_level(kMaxArcaneSymbolLevel);
  EXPECT_EQ(SymbolWorth(proto, maxed), 2680);
}

TEST_F(SacredSymbolTest, ForceAndStats) {
  EquipPrototype proto = Sacred(13.2);
  EXPECT_EQ(SymbolForce(proto, 1), 10);
  EXPECT_EQ(SymbolForce(proto, kMaxSacredSymbolLevel), 110);
  EXPECT_EQ(SymbolStatsFor(proto, STAT_FIELD_DEX, 1).dex(), 500);
  EXPECT_EQ(SymbolStatsFor(proto, STAT_FIELD_DEX, 2).dex(), 700);
  EXPECT_EQ(SymbolStatsFor(proto, STAT_FIELD_LUK, kMaxSacredSymbolLevel).luk(),
            2500);
}

// The wiki's tables for Cernium and Carcion, the cheapest and dearest.
TEST_F(SacredSymbolTest, DuplicatesAndMeso) {
  EquipPrototype cernium = Sacred(13.2);
  EXPECT_EQ(SymbolExpToNextLevel(cernium, 1), 29);
  EXPECT_EQ(SymbolExpToNextLevel(cernium, 10), 1100);
  EXPECT_EQ(SymbolExpToNextLevel(cernium, kMaxSacredSymbolLevel), 0);
  EXPECT_EQ(Ladder(cernium), 4565);
  EXPECT_EQ(SymbolLevelUpCost(cernium, 1), 36500000);
  EXPECT_EQ(SymbolLevelUpCost(cernium, 7), 522900000);
  EXPECT_EQ(SymbolLevelUpCost(cernium, 10), 792000000);
  EXPECT_EQ(SymbolLevelUpCost(cernium, kMaxSacredSymbolLevel), 0);
  int64_t total = 0;
  for (int level = 1; level < kMaxSacredSymbolLevel; ++level) {
    total += SymbolLevelUpCost(cernium, level);
  }
  EXPECT_EQ(total, 3930100000);
  EquipPrototype carcion = Sacred(22.2);
  EXPECT_EQ(SymbolLevelUpCost(carcion, 1), 62600000);
  EXPECT_EQ(SymbolLevelUpCost(carcion, 2), 159600000);
}

TEST_F(SacredSymbolTest, TheCapIsEleven) {
  EquipPrototype proto = Sacred(13.2);
  Equip item;
  item.set_symbol_level(10);
  item.set_symbol_exp(1100);
  EXPECT_FALSE(SymbolMaxed(proto, item));
  LevelUpSymbol(proto, item);
  EXPECT_EQ(SymbolLevel(item), kMaxSacredSymbolLevel);
  EXPECT_EQ(item.symbol_exp(), 0);
  EXPECT_TRUE(SymbolMaxed(proto, item));
  EXPECT_EQ(SymbolWorth(proto, item), 4566);
}

}  // namespace
}  // namespace ms
