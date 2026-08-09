#include "src/character/progression.h"

#include <gtest/gtest.h>

#include <random>

#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

class ProgressionTest : public testing::Test {
 protected:
  CharacterInstance MakeCharacter(int level, Job job = JOB_BEGINNER) {
    Character proto;
    proto.set_level(level);
    proto.set_job(job);
    return CharacterInstance(rng_, std::move(proto));
  }

  std::mt19937 rng_{0};
};

// --- Unlocked ---

TEST_F(ProgressionTest, ANewCharacterHasOnePanel) {
  CharacterInstance c = MakeCharacter(1);
  EXPECT_FALSE(Unlocked(Feature::kEquipped, c));
  EXPECT_FALSE(Unlocked(Feature::kBag, c));
  EXPECT_FALSE(Unlocked(Feature::kUnequip, c));
  EXPECT_FALSE(Unlocked(Feature::kScrolling, c));
  EXPECT_FALSE(Unlocked(Feature::kStarForce, c));
  EXPECT_FALSE(Unlocked(Feature::kRecovery, c));
  EXPECT_FALSE(Unlocked(Feature::kSkills, c));
  EXPECT_FALSE(Unlocked(Feature::kShop, c));
}

// The level named is the level it opens on, not the one after.
TEST_F(ProgressionTest, AFeatureOpensOnTheLevelItNames) {
  EXPECT_FALSE(Unlocked(Feature::kEquipped, MakeCharacter(2)));
  EXPECT_TRUE(Unlocked(Feature::kEquipped, MakeCharacter(3)));
  EXPECT_FALSE(Unlocked(Feature::kBag, MakeCharacter(3)));
  EXPECT_TRUE(Unlocked(Feature::kBag, MakeCharacter(4)));
  EXPECT_FALSE(Unlocked(Feature::kShop, MakeCharacter(19)));
  EXPECT_TRUE(Unlocked(Feature::kShop, MakeCharacter(20)));
  EXPECT_FALSE(Unlocked(Feature::kStarForce, MakeCharacter(69)));
  EXPECT_TRUE(Unlocked(Feature::kStarForce, MakeCharacter(70)));
  EXPECT_FALSE(Unlocked(Feature::kRecovery, MakeCharacter(139)));
  EXPECT_TRUE(Unlocked(Feature::kRecovery, MakeCharacter(140)));
}

// Held back until the early game is over and there is meso coming in for the
// spell traces it spends.
TEST_F(ProgressionTest, ScrollingWaitsForTheEarlyGameToBeOver) {
  EXPECT_FALSE(Unlocked(Feature::kScrolling, MakeCharacter(39)));
  EXPECT_TRUE(Unlocked(Feature::kScrolling, MakeCharacter(40)));
}

// Scrolling is the one upgrade the trial hands over, so it has to fall inside
// the cap. Star force and recovery are both deliberately above it and wait for
// the cap to lift -- only the workbench reaches them.
TEST_F(ProgressionTest, ScrollingIsTheOnlyUpgradeTheTrialReaches) {
  EXPECT_LE(UnlockLevel(Feature::kScrolling), kTrialLevelCap);
  EXPECT_GT(UnlockLevel(Feature::kStarForce), kTrialLevelCap);
  EXPECT_GT(UnlockLevel(Feature::kRecovery), kTrialLevelCap);
}

// --- the upgrades a climb opened ---

TEST_F(ProgressionTest, NamesTheUpgradeThatOpened) {
  int level = UnlockLevel(Feature::kScrolling);
  std::vector<Feature> opened = UpgradesUnlockedBetween(level - 1, level);
  ASSERT_EQ(opened.size(), 1u);
  EXPECT_EQ(FeatureName(opened[0]), "Scrolling");
}

// The span, not the level landed on: one idle stretch can carry a character
// past a threshold and out the other side, and stepping over it would leave
// them never told.
TEST_F(ProgressionTest, ReadsTheWholeSpanNotTheLevelLandedOn) {
  int level = UnlockLevel(Feature::kScrolling);
  EXPECT_EQ(UpgradesUnlockedBetween(level - 5, level + 5).size(), 1u);
  EXPECT_TRUE(UpgradesUnlockedBetween(level, level + 5).empty())
      << "a climb starting on the unlock has already been through it";
  EXPECT_TRUE(UpgradesUnlockedBetween(level - 5, level - 1).empty());
}

// Panels and tabs go gold on their own when they arrive; only the item-menu
// upgrades need the card to say their names.
TEST_F(ProgressionTest, OnlyTheItemMenuUpgradesAreAnnounced) {
  EXPECT_TRUE(UpgradesUnlockedBetween(1, UnlockLevel(Feature::kShop)).empty());
  EXPECT_EQ(UpgradesUnlockedBetween(1, UnlockLevel(Feature::kRecovery)).size(),
            3u)
      << "scrolling, star force and recovery, in the order they arrive";
}

TEST_F(ProgressionTest, EveryFeatureHasAName) {
  const Feature kAll[] = {
      Feature::kEquipped,  Feature::kBag,       Feature::kUnequip,
      Feature::kScrolling, Feature::kStarForce, Feature::kRecovery,
      Feature::kSkills,    Feature::kShop,
  };
  for (Feature feature : kAll) {
    EXPECT_FALSE(FeatureName(feature).empty());
  }
}

// Taking something off needs somewhere to put it, so the two move together.
TEST_F(ProgressionTest, UnequipOpensWithTheBag) {
  EXPECT_EQ(UnlockLevel(Feature::kUnequip), UnlockLevel(Feature::kBag));
}

// The one gate that is not level alone. A Beginner at 10 is being offered an
// advancement; the skills belong to the job they pick.
TEST_F(ProgressionTest, SkillsWaitForAnAdvancementToo) {
  EXPECT_FALSE(Unlocked(Feature::kSkills, MakeCharacter(9, JOB_SWORDMAN)));
  EXPECT_FALSE(Unlocked(Feature::kSkills, MakeCharacter(10, JOB_BEGINNER)));
  EXPECT_FALSE(Unlocked(Feature::kSkills, MakeCharacter(50, JOB_BEGINNER)));
  EXPECT_TRUE(Unlocked(Feature::kSkills, MakeCharacter(10, JOB_SWORDMAN)));
}

// The job condition is the skills tab's alone; nothing else asks about it.
TEST_F(ProgressionTest, NoOtherFeatureCaresAboutTheJob) {
  EXPECT_TRUE(
      Unlocked(Feature::kScrolling,
               MakeCharacter(UnlockLevel(Feature::kScrolling), JOB_BEGINNER)));
  EXPECT_TRUE(
      Unlocked(Feature::kShop,
               MakeCharacter(UnlockLevel(Feature::kShop), JOB_BEGINNER)));
}

// --- GameSpeedFactor ---

TEST_F(ProgressionTest, TheFirstBandIsTheFastestTheGameEverRuns) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(1), 2.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(9), 2.0);
}

TEST_F(ProgressionTest, EachBandStartsOnTheLevelItNames) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(10), 3.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(29), 3.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(30), 5.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(59), 5.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(60), 8.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(99), 8.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(100), 10.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(140), 10.0);
}

// Nothing beyond the last band, and nothing below the first: the table has to
// answer for a level either side of the range it lists.
TEST_F(ProgressionTest, TheLastBandRunsToTheTop) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(300), 10.0);
}

TEST_F(ProgressionTest, ALevelBelowTheTableGetsTheFirstBand) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(0), 2.0);
}

// The game only ever slows down. A band that dipped would make a level-up
// speed the game up, which is the opposite of what the ladder is for.
TEST_F(ProgressionTest, ThePaceNeverQuickens) {
  for (int level = 2; level <= 300; ++level) {
    EXPECT_GE(GameSpeedFactor(level), GameSpeedFactor(level - 1))
        << "at level " << level;
  }
}

// --- the hotkeys tip ---

// The one thing that expires rather than opens, so both directions matter:
// every level it should be up for, and every level after it goes.
TEST_F(ProgressionTest, TheTipStandsUntilItRetires) {
  for (int level = 1; level < HotkeysTipRetireLevel(); ++level) {
    CharacterInstance c = MakeCharacter(level);
    EXPECT_TRUE(HotkeysTipVisible(c)) << "should still be up at " << level;
  }
  CharacterInstance retired = MakeCharacter(HotkeysTipRetireLevel());
  EXPECT_FALSE(HotkeysTipVisible(retired));
  CharacterInstance later = MakeCharacter(HotkeysTipRetireLevel() + 20);
  EXPECT_FALSE(HotkeysTipVisible(later));
}

// It exists to explain the panels arriving around it, so it has to outlast the
// last of them rather than leaving while one is still new.
TEST_F(ProgressionTest, TheHotkeysTipOutlastsEveryPanelItExplains) {
  EXPECT_GT(HotkeysTipRetireLevel(), UnlockLevel(Feature::kEquipped));
  EXPECT_GT(HotkeysTipRetireLevel(), UnlockLevel(Feature::kBag));
}

}  // namespace
}  // namespace ms
