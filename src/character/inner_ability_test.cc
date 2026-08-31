#include "src/character/inner_ability.h"

#include <map>
#include <random>
#include <set>

#include "gtest/gtest.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

AbilityLine MakeLine(AbilityLineType type, AbilityRank rank,
                     bool locked = false) {
  AbilityLine line;
  line.set_type(type);
  line.set_rank(rank);
  line.set_locked(locked);
  return line;
}

// A preset built by hand, whose first line carries the ability's rank the way
// a rolled one always does.
AbilityPreset MakePreset(AbilityRank rank, const AbilityLine& first,
                         const AbilityLine& second, const AbilityLine& third) {
  AbilityPreset preset;
  preset.set_rank(rank);
  *preset.add_lines() = first;
  *preset.add_lines() = second;
  *preset.add_lines() = third;
  return preset;
}

// Everything a rolled preset must satisfy: three lines, the top one at the
// ability's rank, nothing above it, no type twice, and no line at a rank its
// type does not roll at.
void ExpectWellFormed(const AbilityPreset& preset) {
  ASSERT_EQ(preset.lines_size(), kAbilityLines);
  EXPECT_EQ(preset.lines(0).rank(), preset.rank());
  std::set<AbilityLineType> seen;
  for (const AbilityLine& line : preset.lines()) {
    EXPECT_LE(line.rank(), preset.rank());
    EXPECT_GT(AbilityTypeWeight(line.type(), line.rank()), 0)
        << AbilityLineType_Name(line.type());
    EXPECT_TRUE(seen.insert(line.type()).second)
        << AbilityLineType_Name(line.type());
  }
}

TEST(InnerAbilityTest, LineValues) {
  EXPECT_EQ(AbilityLineValue(ABILITY_LINE_TYPE_STR, ABILITY_RANK_RARE), 10);
  EXPECT_EQ(AbilityLineValue(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
            40);
  EXPECT_EQ(AbilityLineValue(ABILITY_LINE_TYPE_MAX_HP, ABILITY_RANK_UNIQUE),
            450);
  EXPECT_EQ(AbilityLineValue(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_EPIC), 12);
  EXPECT_EQ(
      AbilityLineValue(ABILITY_LINE_TYPE_BUFF_DURATION, ABILITY_RANK_LEGENDARY),
      50);
  EXPECT_EQ(
      AbilityLineValue(ABILITY_LINE_TYPE_ATTACK_SPEED, ABILITY_RANK_LEGENDARY),
      1);
  EXPECT_EQ(
      AbilityLineValue(ABILITY_LINE_TYPE_UNSPECIFIED, ABILITY_RANK_LEGENDARY),
      0);
}

// A type GMS does not offer at a rank has no weight there and is worth
// nothing, so the two tables gate on exactly the same set.
TEST(InnerAbilityTest, GatedTypesAreWorthNothing) {
  for (int type = ABILITY_LINE_TYPE_STR; type < AbilityLineType_ARRAYSIZE;
       ++type) {
    for (int rank = ABILITY_RANK_RARE; rank <= ABILITY_RANK_LEGENDARY; ++rank) {
      const auto line = static_cast<AbilityLineType>(type);
      const auto at = static_cast<AbilityRank>(rank);
      EXPECT_EQ(AbilityTypeWeight(line, at) == 0,
                AbilityLineValue(line, at) == 0)
          << AbilityLineType_Name(line) << " at " << AbilityRank_Name(at);
    }
  }
  EXPECT_EQ(AbilityTypeWeight(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_RARE), 0);
  EXPECT_EQ(AbilityTypeWeight(ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_RANK_EPIC),
            0);
  EXPECT_EQ(
      AbilityTypeWeight(ABILITY_LINE_TYPE_ATTACK_SPEED, ABILITY_RANK_UNIQUE),
      0);
  EXPECT_GT(
      AbilityTypeWeight(ABILITY_LINE_TYPE_ATTACK_SPEED, ABILITY_RANK_LEGENDARY),
      0);
}

// GMS's weights are percentages of a pool this game has thinned. What must
// survive the thinning is their ratio -- STR is one and a half of All Stats at
// Epic, and two and a quarter of it at Unique.
TEST(InnerAbilityTest, WeightRatiosFollowGms) {
  EXPECT_EQ(AbilityTypeWeight(ABILITY_LINE_TYPE_STR, ABILITY_RANK_EPIC),
            AbilityTypeWeight(ABILITY_LINE_TYPE_ALL_STATS, ABILITY_RANK_EPIC) *
                3 / 2);
  EXPECT_EQ(
      AbilityTypeWeight(ABILITY_LINE_TYPE_STR, ABILITY_RANK_UNIQUE) * 4,
      AbilityTypeWeight(ABILITY_LINE_TYPE_ALL_STATS, ABILITY_RANK_UNIQUE) * 9);
  // The two attacks are one GMS line split in two, and each keeps its weight.
  for (int rank = ABILITY_RANK_RARE; rank <= ABILITY_RANK_LEGENDARY; ++rank) {
    const auto at = static_cast<AbilityRank>(rank);
    EXPECT_EQ(AbilityTypeWeight(ABILITY_LINE_TYPE_ATTACK, at),
              AbilityTypeWeight(ABILITY_LINE_TYPE_MAGIC_ATTACK, at));
  }
}

TEST(InnerAbilityTest, ResetCostAndRankUpChance) {
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_RARE, 0), 100);
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_EPIC, 0), 200);
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_UNIQUE, 0), 1500);
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_UNIQUE, 1), 3000);
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_LEGENDARY, 2), 16000);
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_LEGENDARY, 3), 0);

  EXPECT_DOUBLE_EQ(AbilityRankUpChance(ABILITY_RANK_RARE), 0.05);
  EXPECT_DOUBLE_EQ(AbilityRankUpChance(ABILITY_RANK_EPIC), 0.02);
  EXPECT_DOUBLE_EQ(AbilityRankUpChance(ABILITY_RANK_UNIQUE), 0.01);
  EXPECT_DOUBLE_EQ(AbilityRankUpChance(ABILITY_RANK_LEGENDARY), 0.0);
}

TEST(InnerAbilityTest, DefaultPresetIsThreeRareAllStats) {
  const AbilityPreset preset = DefaultAbilityPreset();
  EXPECT_EQ(preset.rank(), ABILITY_RANK_RARE);
  ASSERT_EQ(preset.lines_size(), kAbilityLines);
  for (const AbilityLine& line : preset.lines()) {
    EXPECT_EQ(line.type(), ABILITY_LINE_TYPE_ALL_STATS);
    EXPECT_EQ(line.rank(), ABILITY_RANK_RARE);
    EXPECT_FALSE(line.locked());
    EXPECT_EQ(AbilityLineValue(line.type(), line.rank()), 10);
  }
}

TEST(InnerAbilityTest, PresetOfPicksTheNamedSetup) {
  InnerAbility ability;
  ability.mutable_farming()->set_rank(ABILITY_RANK_EPIC);
  ability.mutable_bossing()->set_rank(ABILITY_RANK_LEGENDARY);
  EXPECT_EQ(PresetOf(ability, HyperPreset::kFarming).rank(), ABILITY_RANK_EPIC);
  EXPECT_EQ(PresetOf(ability, HyperPreset::kBossing).rank(),
            ABILITY_RANK_LEGENDARY);

  PresetOf(ability, HyperPreset::kFarming).set_rank(ABILITY_RANK_UNIQUE);
  EXPECT_EQ(ability.farming().rank(), ABILITY_RANK_UNIQUE);
  EXPECT_EQ(ability.bossing().rank(), ABILITY_RANK_LEGENDARY);
}

TEST(InnerAbilityTest, OnlyUniqueAndLegendaryLinesLock) {
  EXPECT_FALSE(
      AbilityLineLockable(MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_RARE)));
  EXPECT_FALSE(
      AbilityLineLockable(MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_EPIC)));
  EXPECT_TRUE(AbilityLineLockable(
      MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_UNIQUE)));
  EXPECT_TRUE(AbilityLineLockable(
      MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY)));
}

TEST(InnerAbilityTest, LockingRefusesAThirdLineAndAnEpicOne) {
  AbilityPreset preset =
      MakePreset(ABILITY_RANK_LEGENDARY,
                 MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
                 MakeLine(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_UNIQUE),
                 MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC));

  EXPECT_TRUE(SetAbilityLineLocked(preset, 0, true));
  EXPECT_TRUE(SetAbilityLineLocked(preset, 1, true));
  EXPECT_EQ(LockedAbilityLines(preset), 2);
  // The Epic line could not be held even if there were room for it.
  EXPECT_FALSE(SetAbilityLineLocked(preset, 2, true));
  EXPECT_FALSE(SetAbilityLineLocked(preset, 3, true));

  // Freeing one makes room, but not for the Epic line.
  EXPECT_TRUE(SetAbilityLineLocked(preset, 1, false));
  EXPECT_FALSE(SetAbilityLineLocked(preset, 2, true));
  EXPECT_EQ(LockedAbilityLines(preset), 1);
}

TEST(InnerAbilityTest, RerollKeepsThePresetWellFormed) {
  std::mt19937 rng(1);
  AbilityPreset preset = DefaultAbilityPreset();
  for (int i = 0; i < 500; ++i) {
    const AbilityRank before = preset.rank();
    RerollAbility(preset, rng);
    EXPECT_GE(preset.rank(), before);
    ExpectWellFormed(preset);
  }
}

// Five percent a reset carries a Rare ability up, and it can only ever climb.
TEST(InnerAbilityTest, RankOnlyClimbs) {
  std::mt19937 rng(7);
  int reached_legendary = 0;
  for (int run = 0; run < 200; ++run) {
    AbilityPreset preset = DefaultAbilityPreset();
    for (int i = 0; i < 1000; ++i) {
      RerollAbility(preset, rng);
    }
    reached_legendary += preset.rank() == ABILITY_RANK_LEGENDARY ? 1 : 0;
  }
  EXPECT_GT(reached_legendary, 0);
}

TEST(InnerAbilityTest, HeldLinesSurviveTheReroll) {
  std::mt19937 rng(11);
  for (int i = 0; i < 200; ++i) {
    AbilityPreset preset = MakePreset(
        ABILITY_RANK_LEGENDARY,
        MakeLine(ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_RANK_LEGENDARY, true),
        MakeLine(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_UNIQUE, true),
        MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC));
    RerollAbility(preset, rng);
    ExpectWellFormed(preset);
    // A held top line at the ability's rank keeps its slot, and the other
    // held line keeps its own.
    EXPECT_EQ(preset.lines(0).type(), ABILITY_LINE_TYPE_BOSS_DAMAGE);
    EXPECT_EQ(preset.lines(1).type(), ABILITY_LINE_TYPE_ATTACK);
    EXPECT_EQ(preset.lines(1).rank(), ABILITY_RANK_UNIQUE);
  }
}

// A held line stays where it is when the slots above it are free, rather than
// being pulled to the top.
TEST(InnerAbilityTest, HeldLineKeepsItsSlot) {
  std::mt19937 rng(13);
  for (int i = 0; i < 200; ++i) {
    AbilityPreset preset = MakePreset(
        ABILITY_RANK_LEGENDARY,
        MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
        MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
        MakeLine(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_UNIQUE, true));
    RerollAbility(preset, rng);
    ExpectWellFormed(preset);
    EXPECT_EQ(preset.lines(2).type(), ABILITY_LINE_TYPE_ATTACK);
    EXPECT_EQ(preset.lines(2).rank(), ABILITY_RANK_UNIQUE);
  }
}

// Ranking up with the top line held pushes it down a slot, and the rank the
// ability just reached rolls a fresh line above it.
TEST(InnerAbilityTest, RankUpPushesTheHeldTopLineDown) {
  std::mt19937 rng(3);
  int ranked_up = 0;
  for (int i = 0; i < 4000 && ranked_up < 5; ++i) {
    AbilityPreset preset = MakePreset(
        ABILITY_RANK_UNIQUE,
        MakeLine(ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_RANK_UNIQUE, true),
        MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
        MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_RARE));
    RerollAbility(preset, rng);
    ExpectWellFormed(preset);
    if (preset.rank() != ABILITY_RANK_LEGENDARY) {
      EXPECT_EQ(preset.lines(0).type(), ABILITY_LINE_TYPE_BOSS_DAMAGE);
      continue;
    }
    ++ranked_up;
    EXPECT_EQ(preset.lines(0).rank(), ABILITY_RANK_LEGENDARY);
    EXPECT_NE(preset.lines(0).type(), ABILITY_LINE_TYPE_BOSS_DAMAGE);
    EXPECT_EQ(preset.lines(1).type(), ABILITY_LINE_TYPE_BOSS_DAMAGE);
    EXPECT_EQ(preset.lines(1).rank(), ABILITY_RANK_UNIQUE);
  }
  EXPECT_EQ(ranked_up, 5);
}

// The top line of a Legendary ability rolls at the Legendary weights, so the
// types it lands on come up in the ratios the table states.
TEST(InnerAbilityTest, TopLineFollowsTheWeights) {
  std::mt19937 rng(29);
  AbilityPreset preset =
      MakePreset(ABILITY_RANK_LEGENDARY,
                 MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
                 MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
                 MakeLine(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_UNIQUE));
  std::map<AbilityLineType, int> counts;
  constexpr int kRolls = 40000;
  for (int i = 0; i < kRolls; ++i) {
    RerollAbility(preset, rng);
    ++counts[preset.lines(0).type()];
  }
  // 45 against 20 against 5, within a couple of percent over this many rolls.
  const double str = counts[ABILITY_LINE_TYPE_STR];
  const double all = counts[ABILITY_LINE_TYPE_ALL_STATS];
  const double speed = counts[ABILITY_LINE_TYPE_ATTACK_SPEED];
  EXPECT_NEAR(str / all, 45.0 / 20.0, 0.15);
  EXPECT_NEAR(all / speed, 20.0 / 5.0, 0.4);
  // Nothing gated above Legendary is missing, and nothing gated out appears.
  EXPECT_GT(counts[ABILITY_LINE_TYPE_BOSS_DAMAGE], 0);
  EXPECT_EQ(counts[ABILITY_LINE_TYPE_UNSPECIFIED], 0);
}

// The two lines under the top roll a rung down: Epic or Unique beneath a
// Legendary ability, and never Rare.
TEST(InnerAbilityTest, LowerLinesRollBelowTheAbilityRank) {
  std::mt19937 rng(31);
  AbilityPreset preset =
      MakePreset(ABILITY_RANK_LEGENDARY,
                 MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
                 MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
                 MakeLine(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_UNIQUE));
  int unique = 0;
  int lower = 0;
  constexpr int kRolls = 10000;
  for (int i = 0; i < kRolls; ++i) {
    RerollAbility(preset, rng);
    for (int slot = 1; slot < kAbilityLines; ++slot) {
      EXPECT_GE(preset.lines(slot).rank(), ABILITY_RANK_EPIC);
      EXPECT_LE(preset.lines(slot).rank(), ABILITY_RANK_UNIQUE);
      unique += preset.lines(slot).rank() == ABILITY_RANK_UNIQUE ? 1 : 0;
      ++lower;
    }
  }
  EXPECT_NEAR(static_cast<double>(unique) / lower, 0.15, 0.02);
}

// A Rare ability's lines are all Rare, since there is no rung below it.
TEST(InnerAbilityTest, RareAbilityRollsRareThroughout) {
  std::mt19937 rng(37);
  for (int i = 0; i < 200; ++i) {
    AbilityPreset preset = DefaultAbilityPreset();
    RerollAbility(preset, rng);
    if (preset.rank() != ABILITY_RANK_RARE) {
      continue;
    }
    ExpectWellFormed(preset);
    for (const AbilityLine& line : preset.lines()) {
      EXPECT_EQ(line.rank(), ABILITY_RANK_RARE);
    }
  }
}

}  // namespace
}  // namespace ms
