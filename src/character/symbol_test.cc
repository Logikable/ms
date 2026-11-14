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
  static EquipPrototype Grand(double base) {
    EquipPrototype proto = Sacred(base);
    proto.mutable_sacred_symbol()->set_grand(true);
    return proto;
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

}  // namespace
}  // namespace ms
