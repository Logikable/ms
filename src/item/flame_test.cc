#include "src/item/flame.h"

#include <map>
#include <random>
#include <set>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ms {
namespace {

using ::testing::Contains;
using ::testing::Not;

EquipPrototype Item(EquipSlot slot, int level) {
  EquipPrototype proto;
  proto.set_equip_slot(slot);
  proto.set_required_level(level);
  return proto;
}

EquipPrototype Weapon(int level, int attack, int magic_attack = 0) {
  EquipPrototype proto = Item(EQUIP_SLOT_PRIMARY_WEAPON, level);
  proto.mutable_base_stats()->set_attack(attack);
  proto.mutable_base_stats()->set_magic_attack(magic_attack);
  return proto;
}

FlameLine Line(FlameStat stat, int tier) {
  FlameLine line;
  line.set_stat(stat);
  line.set_tier(tier);
  return line;
}

TEST(FlameTest, GmsRefusedSlotsTakeNone) {
  for (EquipSlot slot :
       {EQUIP_SLOT_RING, EQUIP_SLOT_RING_4, EQUIP_SLOT_SHOULDER,
        EQUIP_SLOT_EMBLEM, EQUIP_SLOT_BADGE, EQUIP_SLOT_MEDAL,
        EQUIP_SLOT_SECONDARY, EQUIP_SLOT_HEART, EQUIP_SLOT_TOTEM,
        EQUIP_SLOT_TOTEM_3, EQUIP_SLOT_SYMBOL_ARCANA, EQUIP_SLOT_PROJECTILE}) {
    EXPECT_FALSE(SlotTakesFlame(slot)) << slot;
  }
  for (EquipSlot slot : {EQUIP_SLOT_PRIMARY_WEAPON, EQUIP_SLOT_HAT,
                         EQUIP_SLOT_PENDANT_2, EQUIP_SLOT_POCKET}) {
    EXPECT_TRUE(SlotTakesFlame(slot)) << slot;
  }
}

TEST(FlameTest, PoolsAndTheirLevelGates) {
  EXPECT_EQ(FlamePool(Weapon(200, 100)).size(), 16u);
  EXPECT_THAT(FlamePool(Weapon(80, 100)),
              Not(Contains(FLAME_STAT_BOSS_DAMAGE)));
  EXPECT_THAT(FlamePool(Weapon(80, 100)), Contains(FLAME_STAT_ALL_STAT));

  EXPECT_EQ(FlamePool(Item(EQUIP_SLOT_HAT, 200)).size(), 14u);
  EXPECT_THAT(FlamePool(Item(EQUIP_SLOT_HAT, 200)),
              Not(Contains(FLAME_STAT_DAMAGE)));
  EXPECT_THAT(FlamePool(Item(EQUIP_SLOT_HAT, 65)),
              Not(Contains(FLAME_STAT_ALL_STAT)));
  EXPECT_THAT(FlamePool(Item(EQUIP_SLOT_HAT, 65)), Contains(FLAME_STAT_ATTACK));
  EXPECT_EQ(FlamePool(Item(EQUIP_SLOT_HAT, 50)).size(), 11u);
}

// Spot values from the wiki's tables, at the band edges where they stop
// following their own slope.
TEST(FlameTest, ValuesFollowTheWikiTables) {
  const EquipPrototype hat200 = Item(EQUIP_SLOT_HAT, 200);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_STR, 7), hat200), 77);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_STR, 4), Item(EQUIP_SLOT_HAT, 230)),
            48);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_STR, 3), Item(EQUIP_SLOT_HAT, 19)),
            3);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_DEX_LUK, 7), hat200), 42);
  EXPECT_EQ(
      FlameLineValue(Line(FLAME_STAT_DEX_LUK, 7), Item(EQUIP_SLOT_HAT, 250)),
      49);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_MAX_HP, 7), hat200), 4200);
  EXPECT_EQ(
      FlameLineValue(Line(FLAME_STAT_MAX_HP, 7), Item(EQUIP_SLOT_HAT, 160)),
      3360);
  EXPECT_EQ(
      FlameLineValue(Line(FLAME_STAT_MAX_HP, 7), Item(EQUIP_SLOT_HAT, 250)),
      4900);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_ATTACK, 5), hat200), 5);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_ALL_STAT, 6), hat200), 6);
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_BOSS_DAMAGE, 6), hat200), 12);
  EXPECT_EQ(FlameLineValueText(Line(FLAME_STAT_BOSS_DAMAGE, 6), hat200),
            "+12%");
  EXPECT_EQ(FlameLineValueText(Line(FLAME_STAT_STR, 7), hat200), "+77");
}

TEST(FlameTest, WeaponAttackIsAShareOfItsOwnRoundedUp) {
  // 18% of 100 exactly, which a floating-point crumb mustn't push to 19.
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_ATTACK, 3), Weapon(200, 100)), 18);
  // 26.4% of 155 is 40.92.
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_ATTACK, 4), Weapon(200, 155)), 41);
  // 61.49% at T7.
  EXPECT_EQ(FlameLineValue(Line(FLAME_STAT_ATTACK, 7), Weapon(200, 100)), 62);
  // MATT reads the weapon's own MATT.
  EXPECT_EQ(
      FlameLineValue(Line(FLAME_STAT_MAGIC_ATTACK, 3), Weapon(200, 100, 200)),
      36);
}

TEST(FlameTest, StatsSumPairsIntoBothAndKeepPercentsApart) {
  FlameLines lines;
  *lines.Add() = Line(FLAME_STAT_STR_DEX, 5);
  *lines.Add() = Line(FLAME_STAT_STR, 4);
  *lines.Add() = Line(FLAME_STAT_ALL_STAT, 6);
  *lines.Add() = Line(FLAME_STAT_DAMAGE, 7);
  const EquipPrototype weapon = Weapon(200, 100);
  EquipStats stats = FlameStats(lines, weapon);
  EXPECT_EQ(stats.str(), 30 + 44);
  EXPECT_EQ(stats.dex(), 30);
  EXPECT_EQ(stats.int_(), 0);
  FlamePercents percents = FlamePercentsOf(lines, weapon);
  EXPECT_EQ(percents.all_stat, 6);
  EXPECT_EQ(percents.damage, 7);
}

TEST(FlameTest, RollsFourDistinctLinesInTheFlamesTiers) {
  std::mt19937 rng(7);
  const EquipPrototype weapon = Weapon(200, 100);
  const std::vector<FlameStat> pool = FlamePool(weapon);
  std::map<int, int> black_tiers;
  std::set<int> burning_tiers;
  FlameLines last;
  for (int i = 0; i < 4000; ++i) {
    FlameLines black = RollFlame(FlameType::kBlack, weapon, last, rng);
    ASSERT_EQ(black.size(), kFlameLines);
    std::set<int> stats;
    for (const FlameLine& line : black) {
      EXPECT_THAT(pool, Contains(line.stat()));
      stats.insert(line.stat());
      ++black_tiers[line.tier()];
    }
    EXPECT_EQ(stats.size(), 4u);
    for (const FlameLine& line :
         RollFlame(FlameType::kBurning, weapon, last, rng)) {
      burning_tiers.insert(line.tier());
    }
    last = black;
  }
  EXPECT_EQ(burning_tiers, (std::set<int>{3, 4, 5, 6}));
  ASSERT_EQ(black_tiers.size(), 4u);
  for (const auto& [tier, count] : black_tiers) {
    EXPECT_GE(tier, 4);
    EXPECT_LE(tier, 7);
    // 16,000 lines at 25% each: 4,000 expected, and 3,700 is over four
    // standard deviations away.
    EXPECT_NEAR(count, 4000, 300) << tier;
  }
}

// GMS never hands back the lines already on the item. The smallest pool makes
// a repeat about 1 in 84,000, so enough rolls to expect six.
TEST(FlameTest, NeverRollsTheLinesItIsGiven) {
  std::mt19937 rng(3);
  const EquipPrototype hat = Item(EQUIP_SLOT_HAT, 50);
  FlameLines current = RollFlame(FlameType::kBlack, hat, {}, rng);
  auto same = [](const FlameLines& a, const FlameLines& b) {
    std::multiset<std::pair<int, int>> x, y;
    for (const FlameLine& l : a) {
      x.insert({l.stat(), l.tier()});
    }
    for (const FlameLine& l : b) {
      y.insert({l.stat(), l.tier()});
    }
    return x == y;
  };
  int repeats = 0;
  for (int i = 0; i < 500'000; ++i) {
    repeats += same(RollFlame(FlameType::kBlack, hat, current, rng), current);
  }
  EXPECT_EQ(repeats, 0);
}

}  // namespace
}  // namespace ms
