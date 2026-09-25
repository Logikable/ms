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

// A preset built by hand, whose first line has the ability's rank, as a rolled
// one always does.
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
// ability's rank, none above it, no repeated type, and no line at a rank its
// type can't roll at.
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

// A type GMS doesn't offer at a rank has no weight and no value there, so both
// tables gate on exactly the same pairings.
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

// GMS's weights are percentages of a pool this game has trimmed. The ratios
// must survive the trimming: STR is 1.5 times All Stats at Epic, and 2.25 times
// at Unique.
TEST(InnerAbilityTest, WeightRatiosFollowGms) {
  EXPECT_EQ(AbilityTypeWeight(ABILITY_LINE_TYPE_STR, ABILITY_RANK_EPIC),
            AbilityTypeWeight(ABILITY_LINE_TYPE_ALL_STATS, ABILITY_RANK_EPIC) *
                3 / 2);
  EXPECT_EQ(
      AbilityTypeWeight(ABILITY_LINE_TYPE_STR, ABILITY_RANK_UNIQUE) * 4,
      AbilityTypeWeight(ABILITY_LINE_TYPE_ALL_STATS, ABILITY_RANK_UNIQUE) * 9);
  // The two attack types are one GMS line split in two, and each keeps its
  // weight.
  for (int rank = ABILITY_RANK_RARE; rank <= ABILITY_RANK_LEGENDARY; ++rank) {
    const auto at = static_cast<AbilityRank>(rank);
    EXPECT_EQ(AbilityTypeWeight(ABILITY_LINE_TYPE_ATTACK, at),
              AbilityTypeWeight(ABILITY_LINE_TYPE_MAGIC_ATTACK, at));
  }
}

TEST(InnerAbilityTest, ResetCostAndRankUpChance) {
  // The whole table, by rank and then by locked lines. Unique and Legendary are
  // GMS's, and the cost of a lock doubles between them: +1500/+2500 becomes
  // +3000/+5000. The two lower rows halve that ladder twice, rounded to
  // recognisable numbers.
  const int64_t want[4][kMaxLockedAbilityLines + 1] = {
      {100, 500, 1100},
      {200, 1000, 2200},
      {1500, 3000, 5500},
      {8000, 11000, 16000},
  };
  for (int rank = ABILITY_RANK_RARE; rank <= ABILITY_RANK_LEGENDARY; ++rank) {
    for (int locked = 0; locked <= kMaxLockedAbilityLines; ++locked) {
      EXPECT_EQ(AbilityResetCost(static_cast<AbilityRank>(rank), locked),
                want[rank - ABILITY_RANK_RARE][locked])
          << "rank " << rank << " holding " << locked;
    }
  }
  // A lock count no reset can have costs nothing.
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_LEGENDARY, 3), 0);
  EXPECT_EQ(AbilityResetCost(ABILITY_RANK_LEGENDARY, -1), 0);

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

TEST(InnerAbilityTest, PresetOfPicksTheNamedSlot) {
  InnerAbility ability;
  PresetOf(ability, StatPreset::kFirst).set_rank(ABILITY_RANK_EPIC);
  PresetOf(ability, StatPreset::kThird).set_rank(ABILITY_RANK_LEGENDARY);
  EXPECT_EQ(ability.presets_size(), kNumStatPresets);
  EXPECT_EQ(PresetOf(ability, StatPreset::kFirst).rank(), ABILITY_RANK_EPIC);
  EXPECT_EQ(PresetOf(ability, StatPreset::kSecond).rank(),
            ABILITY_RANK_UNSPECIFIED);
  EXPECT_EQ(PresetOf(ability, StatPreset::kThird).rank(),
            ABILITY_RANK_LEGENDARY);
}

// A save from before the third slot existed.
TEST(InnerAbilityTest, TheOldTwoSetupsBecomeTheFirstTwoSlots) {
  InnerAbility ability;
  ability.mutable_legacy_farming()->set_rank(ABILITY_RANK_EPIC);
  ability.mutable_legacy_bossing()->set_rank(ABILITY_RANK_LEGENDARY);

  MigrateInnerAbility(ability);
  EXPECT_FALSE(ability.has_legacy_farming());
  EXPECT_FALSE(ability.has_legacy_bossing());
  ASSERT_EQ(ability.presets_size(), kNumStatPresets);
  EXPECT_EQ(ability.presets(0).rank(), ABILITY_RANK_EPIC);
  EXPECT_EQ(ability.presets(1).rank(), ABILITY_RANK_LEGENDARY);
  EXPECT_EQ(ability.presets(2).rank(), ABILITY_RANK_UNSPECIFIED);

  // Running it again leaves what it already wrote unchanged.
  MigrateInnerAbility(ability);
  ASSERT_EQ(ability.presets_size(), kNumStatPresets);
  EXPECT_EQ(ability.presets(0).rank(), ABILITY_RANK_EPIC);
}

// A line of any rank can be locked; only a third lock is refused.
TEST(InnerAbilityTest, LockingRefusesOnlyAThirdLine) {
  AbilityPreset preset =
      MakePreset(ABILITY_RANK_LEGENDARY,
                 MakeLine(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
                 MakeLine(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_UNIQUE),
                 MakeLine(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC));

  EXPECT_TRUE(SetAbilityLineLocked(preset, 0, true));
  EXPECT_TRUE(SetAbilityLineLocked(preset, 1, true));
  EXPECT_EQ(LockedAbilityLines(preset), 2);
  EXPECT_FALSE(SetAbilityLineLocked(preset, 2, true)) << "two is the most";
  EXPECT_FALSE(SetAbilityLineLocked(preset, 3, true)) << "no such line";

  // Unlocking one makes room to lock the Epic line below it.
  EXPECT_TRUE(SetAbilityLineLocked(preset, 1, false));
  EXPECT_TRUE(SetAbilityLineLocked(preset, 2, true));
  EXPECT_EQ(LockedAbilityLines(preset), 2);
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

// A reset has a five percent chance to raise a Rare ability, and the rank can
// only go up.
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
    // A locked top line at the ability's rank keeps its slot, and the other
    // locked line keeps its own.
    EXPECT_EQ(preset.lines(0).type(), ABILITY_LINE_TYPE_BOSS_DAMAGE);
    EXPECT_EQ(preset.lines(1).type(), ABILITY_LINE_TYPE_ATTACK);
    EXPECT_EQ(preset.lines(1).rank(), ABILITY_RANK_UNIQUE);
  }
}

// A locked line stays in place when the slots above it are free, instead of
// moving to the top.
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

// Ranking up with the top line locked pushes it down a slot, and a new line at
// the new rank is rolled above it.
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

// The top line of a Legendary ability rolls with the Legendary weights, so its
// types come up in the ratios the table gives.
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
  // 45 to 20 to 5, within a couple of percent over this many rolls.
  const double str = counts[ABILITY_LINE_TYPE_STR];
  const double all = counts[ABILITY_LINE_TYPE_ALL_STATS];
  const double speed = counts[ABILITY_LINE_TYPE_ATTACK_SPEED];
  EXPECT_NEAR(str / all, 45.0 / 20.0, 0.15);
  EXPECT_NEAR(all / speed, 20.0 / 5.0, 0.4);
  // Nothing allowed at Legendary is missing, and nothing gated out appears.
  EXPECT_GT(counts[ABILITY_LINE_TYPE_BOSS_DAMAGE], 0);
  EXPECT_EQ(counts[ABILITY_LINE_TYPE_UNSPECIFIED], 0);
}

// The two lines under the top roll a rank lower: Epic or Unique under a
// Legendary ability, never Rare.
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

// A Rare ability's lines are all Rare, since there's no rank below it.
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
