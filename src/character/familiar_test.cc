#include "src/character/familiar.h"

#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/character/stat_preset.h"
#include "src/protos/equip.pb.h"
#include "src/protos/familiar.pb.h"

namespace ms {
namespace {

// A familiar at `level` with the given lines, each at the familiar's rank.
Familiar MakeFamiliar(const std::string& name, int level,
                      const std::vector<FamiliarLineType>& types) {
  Familiar familiar;
  familiar.set_name(name);
  familiar.set_level(level);
  for (FamiliarLineType type : types) {
    FamiliarLine* line = familiar.add_lines();
    line->set_type(type);
    line->set_rank(FamiliarRank(level));
  }
  return familiar;
}

google::protobuf::RepeatedPtrField<std::string> Names(
    const std::vector<std::string>& names) {
  google::protobuf::RepeatedPtrField<std::string> out;
  for (const std::string& name : names) {
    *out.Add() = name;
  }
  return out;
}

TEST(FamiliarRosterTest, TwentyFamiliarsFromTheLowestMonsterUp) {
  ASSERT_EQ(FamiliarRoster().size(), 20u);
  EXPECT_STREQ(FamiliarRoster().front().name, "Snail");
  EXPECT_STREQ(FamiliarRoster().back().name, "Mutant Orange Mushroom");
  for (std::size_t i = 1; i < FamiliarRoster().size(); ++i) {
    EXPECT_LE(FamiliarRoster()[i - 1].mob_level, FamiliarRoster()[i].mob_level)
        << FamiliarRoster()[i].name;
  }
  EXPECT_TRUE(IsFamiliar("Jr. Cellion"));
  EXPECT_FALSE(IsFamiliar("Pink Bean"));
}

TEST(FamiliarLevelTest, EachStepIsPaidOnItsOwn) {
  FamiliarBook book;
  std::mt19937 rng(1);
  book.set_exp(999);
  EXPECT_FALSE(LevelUpFamiliar(book, "Slime", rng));
  EXPECT_EQ(book.familiars_size(), 0);

  book.set_exp(1'000 + 8'999);
  ASSERT_TRUE(LevelUpFamiliar(book, "Slime", rng));
  EXPECT_EQ(FamiliarLevel(book, "Slime"), 1);
  EXPECT_EQ(book.exp(), 8'999);
  EXPECT_FALSE(LevelUpFamiliar(book, "Slime", rng));

  book.set_exp(9'000 + 50'000 + 200'000);
  ASSERT_TRUE(LevelUpFamiliar(book, "Slime", rng));
  ASSERT_TRUE(LevelUpFamiliar(book, "Slime", rng));
  ASSERT_TRUE(LevelUpFamiliar(book, "Slime", rng));
  EXPECT_EQ(FamiliarLevel(book, "Slime"), kFamiliarMaxLevel);
  EXPECT_EQ(book.exp(), 0);

  book.set_exp(1'000'000);
  EXPECT_FALSE(LevelUpFamiliar(book, "Slime", rng));
  EXPECT_FALSE(LevelUpFamiliar(book, "Pink Bean", rng));
  EXPECT_EQ(book.exp(), 1'000'000);
}

TEST(FamiliarLevelTest, ALevelUpRollsBothLinesAtTheNewRank) {
  FamiliarBook book;
  std::mt19937 rng(2);
  book.set_exp(1'000 + 9'000 + 50'000 + 200'000);
  for (int level = 1; level <= kFamiliarMaxLevel; ++level) {
    ASSERT_TRUE(LevelUpFamiliar(book, "Yeti", rng));
    const Familiar& yeti = *FindFamiliar(book, "Yeti");
    ASSERT_EQ(yeti.lines_size(), kFamiliarLines);
    const PotentialRank rank = FamiliarRank(level);
    EXPECT_EQ(yeti.lines(0).rank(), rank);
    EXPECT_GT(FamiliarLineValue(yeti.lines(0).type(), rank), 0);
    EXPECT_GE(yeti.lines(1).rank(), std::max<int>(rank - 1, 1));
    EXPECT_LE(yeti.lines(1).rank(), rank);
  }
}

TEST(FamiliarRollTest, TheSecondLineIsPrimeOneTimeInTen) {
  std::mt19937 rng(3);
  int prime = 0;
  constexpr int kRolls = 20'000;
  for (int i = 0; i < kRolls; ++i) {
    std::vector<FamiliarLine> lines =
        RollFamiliarLines(POTENTIAL_RANK_LEGENDARY, rng);
    ASSERT_EQ(lines[0].rank(), POTENTIAL_RANK_LEGENDARY);
    if (lines[1].rank() == POTENTIAL_RANK_LEGENDARY) {
      ++prime;
    } else {
      ASSERT_EQ(lines[1].rank(), POTENTIAL_RANK_UNIQUE);
    }
  }
  EXPECT_NEAR(prime / static_cast<double>(kRolls), kFamiliarPrimeChance, 0.01);
}

TEST(FamiliarRollTest, ARareFamiliarHasBothLinesRare) {
  std::mt19937 rng(4);
  for (int i = 0; i < 200; ++i) {
    for (const FamiliarLine& line :
         RollFamiliarLines(POTENTIAL_RANK_RARE, rng)) {
      ASSERT_EQ(line.rank(), POTENTIAL_RANK_RARE);
    }
  }
}

TEST(FamiliarRollTest, EveryLineInAPoolIsWorthSomethingThere) {
  const int sizes[] = {11, 11, 12, 13};
  for (int level = 1; level <= kFamiliarMaxLevel; ++level) {
    const PotentialRank rank = FamiliarRank(level);
    const std::vector<FamiliarLineType> pool = FamiliarPool(rank);
    EXPECT_EQ(static_cast<int>(pool.size()), sizes[level - 1]) << level;
    for (FamiliarLineType type : pool) {
      EXPECT_GT(FamiliarLineValue(type, rank), 0) << type;
    }
  }
  // Ignored defence and boss damage wait for Unique.
  EXPECT_EQ(FamiliarLineValue(FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_30,
                              POTENTIAL_RANK_EPIC),
            0);
  EXPECT_EQ(FamiliarLineValue(FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40,
                              POTENTIAL_RANK_LEGENDARY),
            40);
}

TEST(FamiliarCubeTest, RerollsAtTheSameRankAndRefusesAnUnlevelledOne) {
  FamiliarBook book;
  std::mt19937 rng(5);
  EXPECT_FALSE(CubeFamiliar(book, "Rash", rng));
  *book.add_familiars() = MakeFamiliar(
      "Rash", 3, {FAMILIAR_LINE_TYPE_STR_PCT, FAMILIAR_LINE_TYPE_STR_PCT});
  bool changed = false;
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(CubeFamiliar(book, "Rash", rng));
    const Familiar& rash = *FindFamiliar(book, "Rash");
    EXPECT_EQ(rash.level(), 3);
    EXPECT_EQ(rash.lines(0).rank(), POTENTIAL_RANK_UNIQUE);
    changed = changed || rash.lines(0).type() != FAMILIAR_LINE_TYPE_STR_PCT;
  }
  EXPECT_TRUE(changed);
}

TEST(FamiliarSkillTest, SixLevelsAtTheUsersTotals) {
  const int expected[][2] = {{0, 0},  {1, 1},  {2, 1},  {3, 2},
                             {7, 2},  {8, 3},  {19, 3}, {20, 4},
                             {39, 4}, {40, 5}, {79, 5}, {80, 6}};
  for (const int* pair : expected) {
    EXPECT_EQ(FamiliarSkillLevel(pair[0]), pair[1]) << pair[0];
  }
  FamiliarBook book;
  *book.add_familiars() = MakeFamiliar("Snail", 4, {});
  *book.add_familiars() = MakeFamiliar("Slime", 2, {});
  EXPECT_EQ(TotalFamiliarLevels(book), 6);
}

TEST(FamiliarTotalsTest, BossDamageStopsAtTheCap) {
  FamiliarBook book;
  const std::vector<FamiliarLineType> two_boss = {
      FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40, FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40};
  *book.add_familiars() = MakeFamiliar("Snail", 4, two_boss);
  *book.add_familiars() = MakeFamiliar("Slime", 4, two_boss);
  EXPECT_DOUBLE_EQ(
      SummonedFamiliarTotals(book, Names({"Snail"})).lines.boss_pct, 0.80);
  EXPECT_DOUBLE_EQ(
      SummonedFamiliarTotals(book, Names({"Snail", "Slime"})).lines.boss_pct,
      kFamiliarBossDamageCap);
}

TEST(FamiliarTotalsTest, LinesLandWhereTheirPotentialTwinsDo) {
  FamiliarBook book;
  *book.add_familiars() = MakeFamiliar(
      "Snail", 4,
      {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40, FAMILIAR_LINE_TYPE_ALL_STATS_PCT});
  *book.add_familiars() = MakeFamiliar(
      "Slime", 3,
      {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_30, FAMILIAR_LINE_TYPE_ATTACK_PCT});
  *book.add_familiars() = MakeFamiliar(
      "Yeti", 1, {FAMILIAR_LINE_TYPE_ATTACK, FAMILIAR_LINE_TYPE_MAX_HP});
  const FamiliarTotals totals =
      SummonedFamiliarTotals(book, Names({"Snail", "Slime", "Yeti"}));
  EXPECT_DOUBLE_EQ(totals.lines.ied, 1.0 - 0.6 * 0.7);
  EXPECT_DOUBLE_EQ(totals.lines.str_pct, 0.03);
  EXPECT_DOUBLE_EQ(totals.lines.luk_pct, 0.03);
  EXPECT_DOUBLE_EQ(totals.lines.attack_pct, 0.03);
  EXPECT_DOUBLE_EQ(totals.lines.magic_attack_pct, 0.03);
  EXPECT_EQ(totals.attack, 3);
  EXPECT_EQ(totals.lines.flat.max_hp(), 6);
  EXPECT_FALSE(totals.boss_drop);
}

TEST(FamiliarTotalsTest, OnlyThreeSummonedFamiliarsCount) {
  FamiliarBook book;
  const std::vector<std::string> four = {"Snail", "Slime", "Yeti", "Rash"};
  for (const std::string& name : four) {
    *book.add_familiars() = MakeFamiliar(
        name, 4, {FAMILIAR_LINE_TYPE_CRIT_RATE, FAMILIAR_LINE_TYPE_CRIT_RATE});
  }
  EXPECT_DOUBLE_EQ(SummonedFamiliarTotals(book, Names(four)).lines.crit_rate,
                   0.48);
  // A name the book doesn't have takes no slot.
  EXPECT_DOUBLE_EQ(
      SummonedFamiliarTotals(book, Names({"Beetle", "Snail"})).lines.crit_rate,
      0.16);
}

TEST(FamiliarTotalsTest, TheBossDropLineIsAFlag) {
  FamiliarBook book;
  *book.add_familiars() = MakeFamiliar(
      "Snail", 4,
      {FAMILIAR_LINE_TYPE_BOSS_DROP_RATE, FAMILIAR_LINE_TYPE_BOSS_DROP_RATE});
  EXPECT_TRUE(SummonedFamiliarTotals(book, Names({"Snail"})).boss_drop);
  EXPECT_FALSE(SummonedFamiliarTotals(book, Names({})).boss_drop);
}

TEST(FamiliarPresetTest, AnUnopenedPresetIsEmpty) {
  SummonedFamiliars summoned;
  EXPECT_EQ(PresetOf(summoned, StatPreset::kThird).names_size(), 0);
  *PresetOf(summoned, StatPreset::kSecond).add_names() = "Snail";
  EXPECT_EQ(summoned.presets_size(), kNumStatPresets);
  EXPECT_EQ(PresetOf(static_cast<const SummonedFamiliars&>(summoned),
                     StatPreset::kSecond)
                .names(0),
            "Snail");
}

}  // namespace
}  // namespace ms
