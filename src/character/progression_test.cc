#include "src/character/progression.h"

#include <gtest/gtest.h>

#include <random>

#include "src/account.h"
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

  CharacterInstance MakeAdvanced(int level, Job job, int stage) {
    Character proto;
    proto.set_level(level);
    proto.set_job(job);
    proto.set_job_stage(stage);
    return CharacterInstance(rng_, std::move(proto));
  }

  std::mt19937 rng_{0};
  AccountInstance account_;
};

// --- Unlocked ---

TEST_F(ProgressionTest, ANewCharacterHasOnePanel) {
  CharacterInstance c = MakeCharacter(1);
  EXPECT_FALSE(Unlocked(Feature::kEquipped, c));
  EXPECT_FALSE(Unlocked(Feature::kBag, c));
  EXPECT_FALSE(Unlocked(Feature::kUnequip, c));
  EXPECT_FALSE(Unlocked(Feature::kScrolling, c));
  EXPECT_FALSE(Unlocked(Feature::kStarForce, c));
  EXPECT_FALSE(Unlocked(Feature::kSkills, c));
  EXPECT_FALSE(Unlocked(Feature::kShop, c));
  EXPECT_FALSE(Unlocked(Feature::kCombatStats, c));
  EXPECT_FALSE(Unlocked(Feature::kDamageStats, c));
}

// The stat block is the one thing a level alone never buys. A Beginner who
// puts the choice off climbs past every threshold in the table and still has
// nothing to read: what fills those rows is a job.
TEST_F(ProgressionTest, TheStatBlockWaitsForTheAdvancementNotTheLevel) {
  CharacterInstance late = MakeCharacter(kTrialLevelCap);
  EXPECT_FALSE(Unlocked(Feature::kCombatStats, late));
  EXPECT_FALSE(Unlocked(Feature::kDamageStats, late));

  CharacterInstance first = MakeAdvanced(10, JOB_SWORDMAN, 1);
  EXPECT_TRUE(Unlocked(Feature::kCombatStats, first));
  EXPECT_FALSE(Unlocked(Feature::kDamageStats, first));

  CharacterInstance second = MakeAdvanced(30, JOB_FIGHTER, 2);
  EXPECT_TRUE(Unlocked(Feature::kDamageStats, second));
}

// The level it reports is the one its advancement is offered at -- the soonest
// it can open, which is what a test asking "how early is this" wants.
TEST_F(ProgressionTest, TheStatBlockReportsTheAdvancementLevel) {
  EXPECT_EQ(UnlockLevel(Feature::kCombatStats), 10);
  EXPECT_EQ(UnlockLevel(Feature::kDamageStats), 30);
}

// The level named is the level it opens on, not the one after. Asked of each
// feature through UnlockLevel rather than against a copy of the table, so
// moving a gate does not need this test touched.
TEST_F(ProgressionTest, AFeatureOpensOnTheLevelItNames) {
  const Feature kLevelGated[] = {Feature::kEquipped,  Feature::kBag,
                                 Feature::kUnequip,   Feature::kShop,
                                 Feature::kScrolling, Feature::kStarForce};
  for (Feature feature : kLevelGated) {
    int level = UnlockLevel(feature);
    SCOPED_TRACE(FeatureName(feature));
    EXPECT_FALSE(Unlocked(feature, MakeCharacter(level - 1)));
    EXPECT_TRUE(Unlocked(feature, MakeCharacter(level)));
  }
}

// Held back until the early game is over and there is meso coming in for the
// spell traces it spends.
TEST_F(ProgressionTest, ScrollingWaitsForTheEarlyGameToBeOver) {
  EXPECT_FALSE(Unlocked(Feature::kScrolling, MakeCharacter(39)));
  EXPECT_TRUE(Unlocked(Feature::kScrolling, MakeCharacter(40)));
}

// An upgrade written above the cap is one nobody but the workbench can press,
// so every one of them has to fall inside it.
TEST_F(ProgressionTest, EveryUpgradeFallsInsideTheCap) {
  EXPECT_LE(UnlockLevel(Feature::kScrolling), kTrialLevelCap);
  EXPECT_LE(UnlockLevel(Feature::kStarForce), kTrialLevelCap);
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
  EXPECT_EQ(UpgradesUnlockedBetween(1, kTrialLevelCap).size(), 2u)
      << "scrolling and star force, in the order they arrive";
}

TEST_F(ProgressionTest, EveryFeatureHasAName) {
  const Feature kAll[] = {
      Feature::kEquipped,  Feature::kBag,       Feature::kUnequip,
      Feature::kScrolling, Feature::kStarForce, Feature::kSkills,
      Feature::kShop,
  };
  for (Feature feature : kAll) {
    EXPECT_FALSE(FeatureName(feature).empty());
  }
}

// --- the gold trail ---

TEST_F(ProgressionTest, NothingIsLedBeforeTheUpgradeOpens) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kScrolling) - 1);
  EXPECT_FALSE(LeadToWeapon(c, account_));
  EXPECT_FALSE(LeadToAction(Feature::kScrolling, c, account_));
}

TEST_F(ProgressionTest, TheUpgradeThatOpensLightsBothSignposts) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kScrolling));
  EXPECT_TRUE(LeadToWeapon(c, account_));
  EXPECT_TRUE(LeadToAction(Feature::kScrolling, c, account_));
}

// Two steps, and each is walked past on its own: opening the menu answers the
// weapon's gold, and only pressing the entry answers the entry's.
TEST_F(ProgressionTest, EachStepGoesOutOnItsOwn) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kScrolling));
  FollowedToWeapon(c, account_);
  EXPECT_FALSE(LeadToWeapon(c, account_));
  EXPECT_TRUE(LeadToAction(Feature::kScrolling, c, account_));

  FollowedToAction(Feature::kScrolling, account_);
  EXPECT_FALSE(LeadToAction(Feature::kScrolling, c, account_));
}

// The whole reason each upgrade keeps its own keys: a player led to scrolling
// at 40 has to be led to star force again when it arrives. Star force lights
// the entry alone -- by 120 the item menu is somewhere the player has been a
// hundred times, and a gold weapon name would only take the eye off the row
// that matters.
TEST_F(ProgressionTest, TheNextUpgradeLightsTheTrailAgain) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kScrolling));
  FollowedToWeapon(c, account_);
  FollowedToAction(Feature::kScrolling, account_);
  ASSERT_FALSE(LeadToWeapon(c, account_));

  while (c.proto().level() < UnlockLevel(Feature::kStarForce)) {
    c.LevelUp();
  }
  EXPECT_TRUE(LeadToAction(Feature::kStarForce, c, account_));
  EXPECT_FALSE(LeadToWeapon(c, account_))
      << "star force lit the weapon as well";
  EXPECT_FALSE(LeadToAction(Feature::kScrolling, c, account_))
      << "the one already followed stays followed";
}

// A player who never opened the menu at 40 is still owed the weapon's gold at
// 120: the step that arrives with an upgrade stays lit until it is walked, and
// star force adds nothing to it either way.
TEST_F(ProgressionTest, AnUnwalkedFirstStepOutlastsTheNextUpgrade) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kStarForce));
  EXPECT_TRUE(LeadToWeapon(c, account_));
  FollowedToWeapon(c, account_);
  EXPECT_FALSE(LeadToWeapon(c, account_));
  EXPECT_TRUE(LeadToAction(Feature::kStarForce, c, account_))
      << "opening the menu is not pressing the entry";
}

// Only the upgrades have one. A tab that lights itself gold when it arrives is
// not being led to.
TEST_F(ProgressionTest, AFeatureWithoutATrailIsNeverGold) {
  CharacterInstance c = MakeCharacter(kTrialLevelCap);
  EXPECT_FALSE(LeadToAction(Feature::kShop, c, account_));
  EXPECT_FALSE(LeadToAction(Feature::kBag, c, account_));
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
