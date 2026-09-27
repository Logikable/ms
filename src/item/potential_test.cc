#include "src/item/potential.h"

#include <map>
#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ms {
namespace {

using ::testing::Contains;
using ::testing::Not;

TEST(PotentialGroupTest, SlotDecidesThePool) {
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_PRIMARY_WEAPON),
            PotentialGroup::kWeaponry);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_SECONDARY), PotentialGroup::kWeaponry);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_EMBLEM), PotentialGroup::kWeaponry);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_HAT), PotentialGroup::kHat);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_GLOVES), PotentialGroup::kGloves);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_BELT), PotentialGroup::kArmor);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_HEART), PotentialGroup::kArmor);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_RING_4), PotentialGroup::kAccessory);
  EXPECT_EQ(PotentialGroupOf(EQUIP_SLOT_PENDANT_2), PotentialGroup::kAccessory);
}

TEST(PotentialGroupTest, FiveSlotsTakeNone) {
  for (EquipSlot slot :
       {EQUIP_SLOT_PROJECTILE, EQUIP_SLOT_BADGE, EQUIP_SLOT_MEDAL,
        EQUIP_SLOT_POCKET, EQUIP_SLOT_SYMBOL_ARCANA}) {
    EXPECT_EQ(PotentialGroupOf(slot), PotentialGroup::kNone) << slot;
    EXPECT_FALSE(SlotTakesPotential(slot)) << slot;
  }
  EXPECT_TRUE(SlotTakesPotential(EQUIP_SLOT_HAT));
}

TEST(PotentialValueTest, PercentLinesClimbWithRankAndLevel) {
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_STR_PCT, POTENTIAL_RANK_RARE, 30),
      1);
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_STR_PCT, POTENTIAL_RANK_RARE, 31),
      2);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_ATTACK_PCT,
                               POTENTIAL_RANK_EPIC, 120),
            6);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT,
                               POTENTIAL_RANK_UNIQUE, 150),
            9);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_DAMAGE_PCT,
                               POTENTIAL_RANK_LEGENDARY, 151),
            13);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_MAX_HP_PCT,
                               POTENTIAL_RANK_LEGENDARY, 200),
            13);
}

// All Stats % gives what a single stat gives one rank lower, to balance
// covering all four.
TEST(PotentialValueTest, AllStatsPercentIsOneRankBehind) {
  for (PotentialRank rank :
       {POTENTIAL_RANK_EPIC, POTENTIAL_RANK_UNIQUE, POTENTIAL_RANK_LEGENDARY}) {
    EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_ALL_STATS_PCT, rank, 120),
              PotentialLineValue(POTENTIAL_LINE_TYPE_STR_PCT,
                                 PreviousPotentialRank(rank), 120))
        << rank;
  }
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_ALL_STATS_PCT,
                               POTENTIAL_RANK_RARE, 120),
            0);
}

TEST(PotentialValueTest, FlatLinesFollowTheLevelBand) {
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_STR, POTENTIAL_RANK_RARE, 0),
            2);
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_DEX, POTENTIAL_RANK_RARE, 100),
      12);
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_LUK, POTENTIAL_RANK_RARE, 160),
      13);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_ALL_STATS,
                               POTENTIAL_RANK_RARE, 100),
            5);
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_MAX_HP, POTENTIAL_RANK_RARE, 45),
      50);
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_MAX_HP, POTENTIAL_RANK_RARE, 200),
      125);
  // Nothing flat rolls above Rare.
  EXPECT_EQ(
      PotentialLineValue(POTENTIAL_LINE_TYPE_STR, POTENTIAL_RANK_EPIC, 100), 0);
}

TEST(PotentialValueTest, TheOneOffLines) {
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15,
                               POTENTIAL_RANK_EPIC, 100),
            15);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40,
                               POTENTIAL_RANK_LEGENDARY, 150),
            40);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_COOLDOWN_2,
                               POTENTIAL_RANK_LEGENDARY, 150),
            2);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT,
                               POTENTIAL_RANK_LEGENDARY, 55),
            5);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT,
                               POTENTIAL_RANK_LEGENDARY, 150),
            8);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_MESO_RATE,
                               POTENTIAL_RANK_LEGENDARY, 150),
            20);
  // A rank the line doesn't roll at is worth nothing.
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_ITEM_DROP_RATE,
                               POTENTIAL_RANK_UNIQUE, 150),
            0);
}

TEST(PotentialPoolTest, EachSpecialLineSitsInOneGroup) {
  const std::vector<PotentialLineType> hat = PotentialPool(
      PotentialTrack::kMain, PotentialGroup::kHat, POTENTIAL_RANK_LEGENDARY);
  const std::vector<PotentialLineType> gloves = PotentialPool(
      PotentialTrack::kMain, PotentialGroup::kGloves, POTENTIAL_RANK_LEGENDARY);
  const std::vector<PotentialLineType> accessory =
      PotentialPool(PotentialTrack::kMain, PotentialGroup::kAccessory,
                    POTENTIAL_RANK_LEGENDARY);
  EXPECT_THAT(hat, Contains(POTENTIAL_LINE_TYPE_COOLDOWN_2));
  EXPECT_THAT(gloves, Not(Contains(POTENTIAL_LINE_TYPE_COOLDOWN_2)));
  EXPECT_THAT(gloves, Contains(POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT));
  EXPECT_THAT(hat, Not(Contains(POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT)));
  EXPECT_THAT(accessory, Contains(POTENTIAL_LINE_TYPE_MESO_RATE));
  EXPECT_THAT(accessory, Contains(POTENTIAL_LINE_TYPE_ITEM_DROP_RATE));
  EXPECT_THAT(hat, Not(Contains(POTENTIAL_LINE_TYPE_MESO_RATE)));
}

TEST(PotentialPoolTest, WeaponLinesStayOnWeaponry) {
  const std::vector<PotentialLineType> weapon =
      PotentialPool(PotentialTrack::kMain, PotentialGroup::kWeaponry,
                    POTENTIAL_RANK_LEGENDARY);
  EXPECT_THAT(weapon, Contains(POTENTIAL_LINE_TYPE_ATTACK_PCT));
  EXPECT_THAT(weapon, Contains(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40));
  EXPECT_THAT(weapon, Not(Contains(POTENTIAL_LINE_TYPE_STR)));
  const std::vector<PotentialLineType> armor = PotentialPool(
      PotentialTrack::kMain, PotentialGroup::kArmor, POTENTIAL_RANK_LEGENDARY);
  EXPECT_THAT(armor, Not(Contains(POTENTIAL_LINE_TYPE_ATTACK_PCT)));
  EXPECT_THAT(armor, Not(Contains(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35)));
}

TEST(PotentialPoolTest, FlatLinesAreRareAlone) {
  EXPECT_THAT(PotentialPool(PotentialTrack::kMain, PotentialGroup::kArmor,
                            POTENTIAL_RANK_RARE),
              Contains(POTENTIAL_LINE_TYPE_STR));
  EXPECT_THAT(PotentialPool(PotentialTrack::kMain, PotentialGroup::kArmor,
                            POTENTIAL_RANK_EPIC),
              Not(Contains(POTENTIAL_LINE_TYPE_STR)));
  EXPECT_THAT(PotentialPool(PotentialTrack::kMain, PotentialGroup::kArmor,
                            POTENTIAL_RANK_EPIC),
              Contains(POTENTIAL_LINE_TYPE_ALL_STATS_PCT));
}

TEST(PotentialPoolTest, ASlotWithNoPotentialDrawsNothing) {
  EXPECT_TRUE(PotentialPool(PotentialTrack::kMain, PotentialGroup::kNone,
                            POTENTIAL_RANK_RARE)
                  .empty());
}

TEST(PotentialPoolTest, BonusPoolSizes) {
  struct Case {
    PotentialGroup group;
    int sizes[4];
  };
  const Case cases[] = {
      {PotentialGroup::kWeaponry, {7, 8, 9, 9}},
      {PotentialGroup::kArmor, {5, 6, 10, 10}},
      {PotentialGroup::kGloves, {5, 6, 10, 10}},
      {PotentialGroup::kAccessory, {5, 6, 10, 10}},
      {PotentialGroup::kHat, {5, 6, 10, 11}},
  };
  for (const Case& c : cases) {
    for (int i = 0; i < 4; ++i) {
      const PotentialRank rank =
          static_cast<PotentialRank>(POTENTIAL_RANK_RARE + i);
      EXPECT_EQ(PotentialPool(PotentialTrack::kBonus, c.group, rank).size(),
                c.sizes[i])
          << static_cast<int>(c.group) << " " << rank;
    }
  }
}

TEST(PotentialPoolTest, BonusWeaponryRollsTheMainLinesAndNoneOfItsOwn) {
  const std::vector<PotentialLineType> weapon =
      PotentialPool(PotentialTrack::kBonus, PotentialGroup::kWeaponry,
                    POTENTIAL_RANK_LEGENDARY);
  EXPECT_THAT(weapon, Contains(POTENTIAL_LINE_TYPE_ATTACK_PCT));
  EXPECT_THAT(weapon, Contains(POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE));
  EXPECT_THAT(weapon, Not(Contains(POTENTIAL_LINE_TYPE_BONUS_STR_PCT)));
  EXPECT_THAT(weapon, Not(Contains(POTENTIAL_LINE_TYPE_STR_PER_9_LEVELS)));
  EXPECT_THAT(weapon, Not(Contains(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40)));
  const std::vector<PotentialLineType> hat = PotentialPool(
      PotentialTrack::kBonus, PotentialGroup::kHat, POTENTIAL_RANK_LEGENDARY);
  EXPECT_THAT(hat, Contains(POTENTIAL_LINE_TYPE_COOLDOWN_1));
  EXPECT_THAT(hat, Not(Contains(POTENTIAL_LINE_TYPE_COOLDOWN_2)));
  EXPECT_THAT(hat, Not(Contains(POTENTIAL_LINE_TYPE_STR_PCT)));
}

// PotentialLineValue doesn't know the track, so a line both tracks roll must
// be worth something at every rank either one rolls it at.
TEST(PotentialPoolTest, EveryRolledLineHasAValue) {
  for (PotentialTrack track : {PotentialTrack::kMain, PotentialTrack::kBonus}) {
    for (PotentialGroup group :
         {PotentialGroup::kWeaponry, PotentialGroup::kHat,
          PotentialGroup::kGloves, PotentialGroup::kArmor,
          PotentialGroup::kAccessory}) {
      for (int r = POTENTIAL_RANK_RARE; r <= POTENTIAL_RANK_LEGENDARY; ++r) {
        const PotentialRank rank = static_cast<PotentialRank>(r);
        for (PotentialLineType type : PotentialPool(track, group, rank)) {
          EXPECT_GT(PotentialLineValue(type, rank, 160), 0)
              << type << " at " << rank;
        }
      }
    }
  }
}

// GMS's ItemOption.img at item level 160, and past 200 where most bonus lines
// off weaponry step up once more.
TEST(PotentialValueTest, BonusLinesByRankAndLevel) {
  struct Case {
    PotentialLineType type;
    int at_160[4];
    int at_201[4];
  };
  const Case cases[] = {
      {POTENTIAL_LINE_TYPE_BONUS_STR_PCT, {3, 5, 6, 8}, {3, 5, 7, 9}},
      {POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT, {0, 3, 5, 6}, {0, 3, 6, 7}},
      {POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT, {3, 6, 8, 11}, {3, 6, 9, 12}},
      {POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE, {0, 0, 12, 18}, {0, 0, 14, 20}},
      {POTENTIAL_LINE_TYPE_STR_PER_9_LEVELS, {0, 0, 1, 2}, {0, 0, 1, 2}},
  };
  for (const Case& c : cases) {
    for (int i = 0; i < 4; ++i) {
      const PotentialRank rank =
          static_cast<PotentialRank>(POTENTIAL_RANK_RARE + i);
      EXPECT_EQ(PotentialLineValue(c.type, rank, 160), c.at_160[i])
          << c.type << " at " << rank;
      EXPECT_EQ(PotentialLineValue(c.type, rank, 201), c.at_201[i])
          << c.type << " at " << rank;
    }
  }
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_BONUS_STR_PCT,
                               POTENTIAL_RANK_LEGENDARY, 20),
            3);
  EXPECT_EQ(PotentialLineValue(POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT,
                               POTENTIAL_RANK_LEGENDARY, 90),
            7);
}

TEST(AddPotentialTest, BonusLinesLandWithTheirMainKin) {
  Potential potential;
  potential.set_rank(POTENTIAL_RANK_LEGENDARY);
  for (PotentialLineType type : {POTENTIAL_LINE_TYPE_BONUS_STR_PCT,
                                 POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT,
                                 POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT,
                                 POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE,
                                 POTENTIAL_LINE_TYPE_LUK_PER_9_LEVELS}) {
    PotentialLine* line = potential.add_lines();
    line->set_type(type);
    line->set_rank(POTENTIAL_RANK_LEGENDARY);
  }
  PotentialTotals totals;
  AddPotential(potential, 160, totals);
  EXPECT_DOUBLE_EQ(totals.str_pct, 0.08 + 0.06);
  EXPECT_DOUBLE_EQ(totals.dex_pct, 0.06);
  EXPECT_DOUBLE_EQ(totals.max_hp_pct, 0.11);
  EXPECT_DOUBLE_EQ(totals.boss_pct, 0.18);
  EXPECT_EQ(totals.per_9_levels.luk(), 2);
  EXPECT_EQ(totals.flat.luk(), 0);
}

TEST(RollPotentialTest, ThreeLinesAndTheFirstCarriesTheRank) {
  std::mt19937 rng(7);
  for (int i = 0; i < 200; ++i) {
    const Potential rolled = RollPotential(
        CubeType::kRed, PotentialGroup::kWeaponry, POTENTIAL_RANK_UNIQUE, rng);
    ASSERT_EQ(rolled.lines_size(), kPotentialLines);
    EXPECT_EQ(rolled.rank(), POTENTIAL_RANK_UNIQUE);
    EXPECT_EQ(rolled.lines(0).rank(), POTENTIAL_RANK_UNIQUE);
    for (const PotentialLine& line : rolled.lines()) {
      EXPECT_TRUE(line.rank() == POTENTIAL_RANK_UNIQUE ||
                  line.rank() == POTENTIAL_RANK_EPIC);
      EXPECT_GT(PotentialLineValue(line.type(), line.rank(), 150), 0);
    }
  }
}

// There's nothing below Rare, so every line of a Rare potential is Rare.
TEST(RollPotentialTest, RareLinesStayRare) {
  std::mt19937 rng(11);
  for (int i = 0; i < 50; ++i) {
    const Potential rolled = RollPotential(CubeType::kRed, PotentialGroup::kHat,
                                           POTENTIAL_RANK_RARE, rng);
    for (const PotentialLine& line : rolled.lines()) {
      EXPECT_EQ(line.rank(), POTENTIAL_RANK_RARE);
    }
  }
}

TEST(RollPotentialTest, PrimeOddsAreTenPercentAndOne) {
  std::mt19937 rng(3);
  int second = 0;
  int third = 0;
  constexpr int kRuns = 20000;
  for (int i = 0; i < kRuns; ++i) {
    const Potential rolled =
        RollPotential(CubeType::kRed, PotentialGroup::kAccessory,
                      POTENTIAL_RANK_LEGENDARY, rng);
    second += rolled.lines(1).rank() == POTENTIAL_RANK_LEGENDARY;
    third += rolled.lines(2).rank() == POTENTIAL_RANK_LEGENDARY;
  }
  EXPECT_NEAR(static_cast<double>(second) / kRuns, 0.10, 0.01);
  EXPECT_NEAR(static_cast<double>(third) / kRuns, 0.01, 0.005);
}

TEST(CubePotentialTest, TheFirstCubeIsAlwaysRare) {
  std::mt19937 rng(5);
  for (int i = 0; i < 100; ++i) {
    const Potential first =
        CubePotential({}, CubeType::kRed, PotentialGroup::kGloves, rng);
    EXPECT_EQ(first.rank(), POTENTIAL_RANK_RARE);
    EXPECT_EQ(first.lines_size(), kPotentialLines);
  }
}

TEST(CubePotentialTest, RankClimbsAtTheStatedOddsAndNeverFalls) {
  const std::map<PotentialRank, double> kExpected = {
      {POTENTIAL_RANK_RARE, 1.0 / 7.0},
      {POTENTIAL_RANK_EPIC, 0.06},
      {POTENTIAL_RANK_UNIQUE, 0.024},
      {POTENTIAL_RANK_LEGENDARY, 0.0},
  };
  std::mt19937 rng(13);
  constexpr int kRuns = 20000;
  for (const std::pair<const PotentialRank, double>& entry : kExpected) {
    Potential held;
    held.set_rank(entry.first);
    int climbed = 0;
    for (int i = 0; i < kRuns; ++i) {
      const Potential next =
          CubePotential(held, CubeType::kRed, PotentialGroup::kArmor, rng);
      EXPECT_GE(next.rank(), entry.first);
      climbed += next.rank() > entry.first;
    }
    EXPECT_NEAR(static_cast<double>(climbed) / kRuns, entry.second, 0.01)
        << entry.first;
  }
}

// Every cube in the shop is found by CubeOf, and the Red Cube costs kCubeCost.
TEST(CubeShelfTest, EveryCubeIsFoundByType) {
  for (const Cube& cube : kCubes) {
    EXPECT_EQ(CubeOf(cube.type).cost, cube.cost);
  }
  EXPECT_EQ(CubeOf(CubeType::kRed).cost, kCubeCost);
  EXPECT_EQ(CubeOf(CubeType::kRed).track, PotentialTrack::kMain);
}

}  // namespace
}  // namespace ms
