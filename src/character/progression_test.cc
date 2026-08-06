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

TEST_F(ProgressionTest, ANewCharacterHasNothingButTheCharacterPanel) {
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
  EXPECT_FALSE(Unlocked(Feature::kStarForce, MakeCharacter(59)));
  EXPECT_TRUE(Unlocked(Feature::kStarForce, MakeCharacter(60)));
  EXPECT_FALSE(Unlocked(Feature::kRecovery, MakeCharacter(139)));
  EXPECT_TRUE(Unlocked(Feature::kRecovery, MakeCharacter(140)));
}

// Scrolling works, but spell traces -- the half a player would reach for --
// are not written yet, so it is held back until the early game is behind them.
TEST_F(ProgressionTest, ScrollingWaitsForTheEarlyGameToBeOver) {
  EXPECT_FALSE(Unlocked(Feature::kScrolling, MakeCharacter(29)));
  EXPECT_TRUE(Unlocked(Feature::kScrolling, MakeCharacter(30)));
}

// Star force opens on the trial's last level and not past it. Recovery is
// deliberately out of reach -- the star force levels that destroy an item in
// the first place need equipment this game does not have -- so it is not held
// to this.
TEST_F(ProgressionTest, StarForceIsReachableWithinTheTrial) {
  EXPECT_LE(UnlockLevel(Feature::kStarForce), kTrialLevelCap);
  EXPECT_GT(UnlockLevel(Feature::kRecovery), kTrialLevelCap);
}

// Taking something off needs somewhere to put it, so the two move together.
TEST_F(ProgressionTest, UnequipOpensWithTheBag) {
  EXPECT_EQ(UnlockLevel(Feature::kUnequip), UnlockLevel(Feature::kBag));
}

// The one gate that is not level alone. A Beginner at 10 is being offered an
// advancement; the skills belong to the job they pick.
TEST_F(ProgressionTest, SkillsWaitForAnAdvancementAsWellAsTheLevel) {
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

// The one thing that expires rather than opens, so it is checked in both
// directions: every level it should be up for, and the level it goes away on.
TEST_F(ProgressionTest, TheHotkeysTipStandsUntilItsRetirementLevel) {
  for (int level = 1; level < HotkeysTipRetireLevel(); ++level) {
    CharacterInstance c = MakeCharacter(level);
    EXPECT_TRUE(HotkeysTipVisible(c)) << "still up at level " << level;
  }
}

TEST_F(ProgressionTest, TheHotkeysTipIsGoneFromItsRetirementLevelOn) {
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
