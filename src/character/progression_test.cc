#include "src/character/progression.h"

#include <gtest/gtest.h>

#include <random>

#include "src/character/character.h"
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
  EXPECT_FALSE(Unlocked(Feature::kEquipped, MakeCharacter(1)));
  EXPECT_TRUE(Unlocked(Feature::kEquipped, MakeCharacter(2)));
  EXPECT_FALSE(Unlocked(Feature::kBag, MakeCharacter(2)));
  EXPECT_TRUE(Unlocked(Feature::kBag, MakeCharacter(3)));
  EXPECT_FALSE(Unlocked(Feature::kShop, MakeCharacter(19)));
  EXPECT_TRUE(Unlocked(Feature::kShop, MakeCharacter(20)));
  EXPECT_FALSE(Unlocked(Feature::kStarForce, MakeCharacter(59)));
  EXPECT_TRUE(Unlocked(Feature::kStarForce, MakeCharacter(60)));
  EXPECT_FALSE(Unlocked(Feature::kRecovery, MakeCharacter(139)));
  EXPECT_TRUE(Unlocked(Feature::kRecovery, MakeCharacter(140)));
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
  EXPECT_TRUE(Unlocked(Feature::kScrolling, MakeCharacter(10, JOB_BEGINNER)));
  EXPECT_TRUE(Unlocked(Feature::kShop, MakeCharacter(20, JOB_BEGINNER)));
}

// --- TickMultiplier ---

TEST_F(ProgressionTest, TheFirstBandRunsInRealTime) {
  EXPECT_EQ(TickMultiplier(1), 1);
  EXPECT_EQ(TickMultiplier(9), 1);
}

TEST_F(ProgressionTest, EachBandStartsOnTheLevelItNames) {
  EXPECT_EQ(TickMultiplier(10), 2);
  EXPECT_EQ(TickMultiplier(29), 2);
  EXPECT_EQ(TickMultiplier(30), 3);
  EXPECT_EQ(TickMultiplier(59), 3);
  EXPECT_EQ(TickMultiplier(60), 5);
  EXPECT_EQ(TickMultiplier(99), 5);
  EXPECT_EQ(TickMultiplier(100), 7);
  EXPECT_EQ(TickMultiplier(139), 7);
  EXPECT_EQ(TickMultiplier(140), 10);
}

// Nothing beyond the last band, and nothing below the first: the table has to
// answer for a level either side of the range it lists.
TEST_F(ProgressionTest, TheLastBandRunsToTheTop) {
  EXPECT_EQ(TickMultiplier(300), 10);
}

TEST_F(ProgressionTest, ALevelBelowTheTableStillGetsRealTime) {
  EXPECT_EQ(TickMultiplier(0), 1);
}

// The clock only ever speeds up. A band that dipped would make a level-up a
// punishment.
TEST_F(ProgressionTest, TheClockNeverSlowsDown) {
  for (int level = 2; level <= 300; ++level) {
    EXPECT_GE(TickMultiplier(level), TickMultiplier(level - 1))
        << "at level " << level;
  }
}

}  // namespace
}  // namespace ms
