#include "src/character/progression.h"

#include <gtest/gtest.h>

#include <random>

#include "src/account.h"
#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/character/link.h"
#include "src/item/potential.h"
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
  EXPECT_FALSE(Unlocked(Feature::kEquipped, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kBag, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kUnequip, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kScrolling, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kStarForce, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kSkills, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kShop, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kCombatStats, c, account_));
  EXPECT_FALSE(Unlocked(Feature::kDamageStats, c, account_));
}

// The stat block is the one thing level alone never unlocks. A Beginner who
// delays advancing passes every threshold in the table and still has nothing to
// see, because those rows are filled by a job.
TEST_F(ProgressionTest, TheStatBlockWaitsForTheAdvancementNotTheLevel) {
  CharacterInstance late = MakeCharacter(kTrialLevelCap);
  EXPECT_FALSE(Unlocked(Feature::kCombatStats, late, account_));
  EXPECT_FALSE(Unlocked(Feature::kDamageStats, late, account_));

  CharacterInstance first = MakeAdvanced(10, JOB_SWORDMAN, 1);
  EXPECT_TRUE(Unlocked(Feature::kCombatStats, first, account_));
  EXPECT_FALSE(Unlocked(Feature::kDamageStats, first, account_));

  CharacterInstance second = MakeAdvanced(30, JOB_FIGHTER, 2);
  EXPECT_TRUE(Unlocked(Feature::kDamageStats, second, account_));
}

// The level it reports is the one its advancement is offered at: the earliest
// it can unlock, which is what a test asking "how early is this" wants.
TEST_F(ProgressionTest, TheStatBlockReportsTheAdvancementLevel) {
  EXPECT_EQ(UnlockLevel(Feature::kCombatStats), 10);
  EXPECT_EQ(UnlockLevel(Feature::kDamageStats), 30);
}

// The named level is the level it unlocks at, not the one after. Checked for
// each feature through UnlockLevel instead of against a copy of the table, so
// moving a gate doesn't require changing this test.
TEST_F(ProgressionTest, AFeatureOpensOnTheLevelItNames) {
  const Feature kLevelGated[] = {
      Feature::kEquipped, Feature::kBag,         Feature::kUnequip,
      Feature::kShop,     Feature::kScrolling,   Feature::kStarForce,
      Feature::kHammer,   Feature::kConsumables, Feature::kPotential,
  };
  for (Feature feature : kLevelGated) {
    int level = UnlockLevel(feature);
    SCOPED_TRACE(FeatureName(feature));
    EXPECT_FALSE(Unlocked(feature, MakeCharacter(level - 1), account_));
    EXPECT_TRUE(Unlocked(feature, MakeCharacter(level), account_));
  }
}

// Held back until the early game is over and meso is coming in for the spell
// traces it uses.
TEST_F(ProgressionTest, ScrollingWaitsForTheEarlyGameToBeOver) {
  EXPECT_FALSE(Unlocked(Feature::kScrolling, MakeCharacter(39), account_));
  EXPECT_TRUE(Unlocked(Feature::kScrolling, MakeCharacter(40), account_));
}

// Cubing is the last thing a player does to an item, so it unlocks after every
// other upgrade.
TEST_F(ProgressionTest, PotentialOpensAboveEveryOtherUpgrade) {
  EXPECT_EQ(UnlockLevel(Feature::kPotential), kPotentialUnlockLevel);
  EXPECT_GT(UnlockLevel(Feature::kPotential), UnlockLevel(Feature::kHammer));
}

// An upgrade unlocked above the cap could only be used from the workbench, so
// every one must unlock within it.
TEST_F(ProgressionTest, EveryUpgradeFallsInsideTheCap) {
  EXPECT_LE(UnlockLevel(Feature::kScrolling), kTrialLevelCap);
  EXPECT_LE(UnlockLevel(Feature::kStarForce), kTrialLevelCap);
  EXPECT_LE(UnlockLevel(Feature::kHammer), kTrialLevelCap);
}

// --- what the account opens ---

// The point of the account: a player who has been through the early game once
// starts their next character with all of it unlocked at level 1.
TEST_F(ProgressionTest, ASecondCharacterStartsWithTheAccountsUnlocks) {
  account_.RecordProgress(140, 4);
  CharacterInstance fresh = MakeCharacter(1);

  const Feature kAll[] = {
      Feature::kEquipped,    Feature::kBag,         Feature::kUnequip,
      Feature::kMenu,        Feature::kShop,        Feature::kScrolling,
      Feature::kStarForce,   Feature::kBoss,        Feature::kMultiplayer,
      Feature::kCombatStats, Feature::kDamageStats, Feature::kAdvancedStats,
  };
  for (Feature feature : kAll) {
    SCOPED_TRACE(FeatureName(feature));
    EXPECT_TRUE(Unlocked(feature, fresh, account_));
  }
  EXPECT_FALSE(Unlocked(Feature::kHammer, fresh, account_))
      << "an account that stopped at 140 never reached the hammer";
}

// The three features not tied to this character's level. The lobby unlocks with
// skills, well before bossing; the character select and the bank unlock last,
// and a second character has both from level 1.
TEST_F(ProgressionTest, TheLobbyOpensEarlyAndTheCharacterSelectLast) {
  EXPECT_EQ(UnlockLevel(Feature::kMultiplayer), 10);
  EXPECT_EQ(UnlockLevel(Feature::kCharacters), 210);
  EXPECT_EQ(UnlockLevel(Feature::kBank), UnlockLevel(Feature::kCharacters))
      << "shared storage is worth nothing without somebody to share with";
  EXPECT_LT(UnlockLevel(Feature::kMultiplayer), UnlockLevel(Feature::kBoss));

  EXPECT_FALSE(Unlocked(Feature::kCharacters, MakeCharacter(209), account_));
  EXPECT_FALSE(Unlocked(Feature::kBank, MakeCharacter(209), account_));
  EXPECT_TRUE(Unlocked(Feature::kCharacters, MakeCharacter(210), account_));
  EXPECT_TRUE(Unlocked(Feature::kBank, MakeCharacter(210), account_));
  account_.RecordProgress(210, 5);
  EXPECT_TRUE(Unlocked(Feature::kCharacters, MakeCharacter(1), account_));
  EXPECT_TRUE(Unlocked(Feature::kBank, MakeCharacter(1), account_));
}

// The skills tab unlocks on level alone, with or without a job, since every
// character starts with the beginner's book. So it waits until 10 for the
// account's first character and is there immediately for later ones.
TEST_F(ProgressionTest, TheSkillsTabOpensOnTheLevelAlone) {
  EXPECT_FALSE(
      Unlocked(Feature::kSkills, MakeCharacter(9, JOB_SWORDMAN), account_));
  EXPECT_TRUE(
      Unlocked(Feature::kSkills, MakeCharacter(10, JOB_BEGINNER), account_));
  account_.RecordProgress(140, 4);
  EXPECT_TRUE(Unlocked(Feature::kSkills, MakeCharacter(1), account_));
}

// The account is a minimum, not a maximum: a character who has passed the level
// the save recorded unlocks things on their own.
TEST_F(ProgressionTest, TheCharactersOwnLevelStillCounts) {
  account_.RecordProgress(3, 0);
  EXPECT_TRUE(Unlocked(Feature::kShop,
                       MakeCharacter(UnlockLevel(Feature::kShop)), account_));
}

// The corner shows the menu or the tip, never both. The menu comes with the
// account, so the tip must go with it.
TEST_F(ProgressionTest, TheHotkeysTipIsGoneForASecondCharacter) {
  CharacterInstance fresh = MakeCharacter(1);
  EXPECT_TRUE(HotkeysTipVisible(fresh, account_));

  account_.RecordProgress(HotkeysTipRetireLevel(), 1);
  EXPECT_FALSE(HotkeysTipVisible(fresh, account_));
}

// A gold trail is followed once per account, not once per character.
TEST_F(ProgressionTest, AWalkedTrailStaysWalkedForTheNextCharacter) {
  CharacterInstance first = MakeCharacter(UnlockLevel(Feature::kScrolling));
  FollowedToWeapon(first, account_);
  FollowedToAction(Feature::kScrolling, account_);

  CharacterInstance second = MakeCharacter(1);
  EXPECT_FALSE(LeadToWeapon(second, account_));
  EXPECT_FALSE(LeadToAction(Feature::kScrolling, second, account_));
}

// --- the upgrades a climb opened ---

TEST_F(ProgressionTest, NamesTheUpgradeThatOpened) {
  int level = UnlockLevel(Feature::kScrolling);
  std::vector<Feature> opened =
      UpgradesUnlockedBetween(level - 1, level, /*account_level=*/0);
  ASSERT_EQ(opened.size(), 1u);
  EXPECT_EQ(FeatureName(opened[0]), "Scrolling");
}

// Checks the whole range, not the level reached: one offline period can take a
// character past a threshold, and checking only the end would mean they are
// never told.
TEST_F(ProgressionTest, ReadsTheWholeSpanNotTheLevelLandedOn) {
  int level = UnlockLevel(Feature::kScrolling);
  EXPECT_EQ(
      UpgradesUnlockedBetween(level - 5, level + 5, /*account_level=*/0).size(),
      1u);
  EXPECT_TRUE(
      UpgradesUnlockedBetween(level, level + 5, /*account_level=*/0).empty())
      << "a climb starting on the unlock has already been through it";
  EXPECT_TRUE(UpgradesUnlockedBetween(level - 5, level - 1, /*account_level=*/0)
                  .empty());
}

// A second character passing 40 isn't getting scrolling for the first time: the
// account unlocked it, so their card has nothing to announce.
TEST_F(ProgressionTest, GroundTheAccountHasCoveredAnnouncesNothing) {
  int level = UnlockLevel(Feature::kScrolling);
  EXPECT_TRUE(
      UpgradesUnlockedBetween(level - 1, level, /*account_level=*/140).empty());
  EXPECT_EQ(
      UpgradesUnlockedBetween(1, kTrialLevelCap, /*account_level=*/50).size(),
      3u)
      << "star force, the hammer and cubing are ahead of an account that "
         "stopped at 50";
}

// Panels and tabs highlight themselves in gold when they unlock; only item menu
// upgrades need the card to name them.
TEST_F(ProgressionTest, OnlyTheItemMenuUpgradesAreAnnounced) {
  EXPECT_TRUE(UpgradesUnlockedBetween(1, UnlockLevel(Feature::kShop),
                                      /*account_level=*/0)
                  .empty());
  EXPECT_EQ(
      UpgradesUnlockedBetween(1, kTrialLevelCap, /*account_level=*/0).size(),
      4u)
      << "scrolling, star force, the hammer and cubing, in the order they "
         "arrive";
}

TEST_F(ProgressionTest, EveryFeatureHasAName) {
  const Feature kAll[] = {
      Feature::kEquipped,   Feature::kBag,       Feature::kUnequip,
      Feature::kScrolling,  Feature::kStarForce, Feature::kHammer,
      Feature::kPotential,  Feature::kSkills,    Feature::kShop,
      Feature::kLinkSkills,
  };
  for (Feature feature : kAll) {
    EXPECT_FALSE(FeatureName(feature).empty());
  }
}

// The trail appears the first time the account reaches the last threshold, for
// whichever character is being played.
TEST_F(ProgressionTest, TheLinkTrailWaitsForTheAccountsTopRung) {
  EXPECT_EQ(UnlockLevel(Feature::kLinkSkills), kLinkSkillsLevel);
  account_.RecordProgress(kLinkSkillsLevel, 4);

  EXPECT_TRUE(Unlocked(Feature::kLinkSkills, MakeCharacter(1), account_))
      << "a Beginner has the row too";
  EXPECT_TRUE(Unlocked(Feature::kLinkSkills, MakeAdvanced(10, JOB_SWORDMAN, 1),
                       account_));

  AccountInstance fresh;
  EXPECT_FALSE(
      Unlocked(Feature::kLinkSkills, MakeAdvanced(209, JOB_HERO, 4), fresh))
      << "nobody on the account has paid the last rung";
}

// --- the gold trail ---

// Three markers, each shown until the player passes it, and none before the
// system unlocks.
TEST_F(ProgressionTest, TheLinkTrailIsWalkedOneStepAtATime) {
  const LinkTrailStep kSteps[] = {LinkTrailStep::kSkillsTab,
                                  LinkTrailStep::kBeginnerPage,
                                  LinkTrailStep::kLinkRow};
  CharacterInstance hero = MakeAdvanced(10, JOB_SWORDMAN, 1);
  for (LinkTrailStep step : kSteps) {
    EXPECT_FALSE(LeadToLinkSkills(step, hero, account_))
        << "led before the account opened them";
  }

  account_.RecordProgress(kLinkSkillsLevel, 4);
  for (LinkTrailStep step : kSteps) {
    EXPECT_TRUE(LeadToLinkSkills(step, hero, account_));
  }
  FollowedToLinkSkills(LinkTrailStep::kSkillsTab, account_);
  EXPECT_FALSE(LeadToLinkSkills(LinkTrailStep::kSkillsTab, hero, account_));
  EXPECT_TRUE(LeadToLinkSkills(LinkTrailStep::kBeginnerPage, hero, account_))
      << "one step walked is not the next";
  // The keys belong to the account and are distinct, so no step can clear
  // another.
  EXPECT_NE(LinkTrailKey(LinkTrailStep::kBeginnerPage),
            LinkTrailKey(LinkTrailStep::kLinkRow));
}

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

// Two steps, each cleared separately: opening the menu clears the weapon's
// gold, and only pressing the entry clears the entry's.
TEST_F(ProgressionTest, EachStepGoesOutOnItsOwn) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kScrolling));
  FollowedToWeapon(c, account_);
  EXPECT_FALSE(LeadToWeapon(c, account_));
  EXPECT_TRUE(LeadToAction(Feature::kScrolling, c, account_));

  FollowedToAction(Feature::kScrolling, account_);
  EXPECT_FALSE(LeadToAction(Feature::kScrolling, c, account_));
}

// This is why each upgrade has its own keys: a player led to scrolling at 40
// must be led to star force when it unlocks. Star force only highlights the
// menu entry: by 120 the player has opened the item menu a hundred times, and a
// gold weapon name would only distract from the row that matters.
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

// A player who never opened the menu at 40 still sees the weapon's gold at 120:
// a step stays highlighted until the player follows it, and star force doesn't
// change that either way.
TEST_F(ProgressionTest, AnUnwalkedFirstStepOutlastsTheNextUpgrade) {
  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kStarForce));
  EXPECT_TRUE(LeadToWeapon(c, account_));
  FollowedToWeapon(c, account_);
  EXPECT_FALSE(LeadToWeapon(c, account_));
  EXPECT_TRUE(LeadToAction(Feature::kStarForce, c, account_))
      << "opening the menu is not pressing the entry";
}

// The third item menu upgrade, after star force. It only highlights its entry,
// for the same reason star force does.
TEST_F(ProgressionTest, TheHammerIsTheThirdUpgradeAndLightsItsEntry) {
  EXPECT_GT(UnlockLevel(Feature::kHammer), UnlockLevel(Feature::kStarForce));

  CharacterInstance c = MakeCharacter(UnlockLevel(Feature::kHammer));
  FollowedToWeapon(c, account_);
  EXPECT_TRUE(LeadToAction(Feature::kHammer, c, account_));
  EXPECT_FALSE(LeadToWeapon(c, account_)) << "the hammer lit the weapon";
  FollowedToAction(Feature::kHammer, account_);
  EXPECT_FALSE(LeadToAction(Feature::kHammer, c, account_));
}

// Only upgrades have trails. A tab that highlights itself when it unlocks isn't
// being led to.
TEST_F(ProgressionTest, AFeatureWithoutATrailIsNeverGold) {
  CharacterInstance c = MakeCharacter(kTrialLevelCap);
  EXPECT_FALSE(LeadToAction(Feature::kShop, c, account_));
  EXPECT_FALSE(LeadToAction(Feature::kBag, c, account_));
}

// Taking something off needs somewhere to put it, so the two unlock together.
TEST_F(ProgressionTest, UnequipOpensWithTheBag) {
  EXPECT_EQ(UnlockLevel(Feature::kUnequip), UnlockLevel(Feature::kBag));
}

// Hyper Stat points come from this character's own levels, so the account's
// progress doesn't unlock the tab for a new character.
TEST_F(ProgressionTest, HyperStatsWaitForThisCharactersOwnLevel) {
  const int level = UnlockLevel(Feature::kHyperStats);
  account_.RecordProgress(kTrialLevelCap, /*job_stage=*/4);
  EXPECT_FALSE(
      Unlocked(Feature::kHyperStats, MakeCharacter(level - 1), account_));
  EXPECT_TRUE(Unlocked(Feature::kHyperStats, MakeCharacter(level), account_));
}

// No feature depends on which job the character took.
TEST_F(ProgressionTest, NoFeatureCaresAboutTheJob) {
  EXPECT_TRUE(Unlocked(
      Feature::kScrolling,
      MakeCharacter(UnlockLevel(Feature::kScrolling), JOB_BEGINNER), account_));
  EXPECT_TRUE(Unlocked(Feature::kShop,
                       MakeCharacter(UnlockLevel(Feature::kShop), JOB_BEGINNER),
                       account_));
}

// --- GameSpeedFactor ---

TEST_F(ProgressionTest, TheFirstBandIsTheFastestTheGameEverRuns) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(1), 2.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(9), 2.0);
}

TEST_F(ProgressionTest, EachBandStartsOnTheLevelItNames) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(10), 3.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(29), 3.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(30), 4.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(59), 4.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(60), 5.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(99), 5.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(100), 6.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(139), 6.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(140), 7.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(199), 7.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(200), 8.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(229), 8.0);
  EXPECT_DOUBLE_EQ(GameSpeedFactor(230), 10.0);
}

// Nothing above the last band and nothing below the first: the table must give
// an answer for levels on either side of its range.
TEST_F(ProgressionTest, TheLastBandRunsToTheTop) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(300), 10.0);
}

TEST_F(ProgressionTest, ALevelBelowTheTableGetsTheFirstBand) {
  EXPECT_DOUBLE_EQ(GameSpeedFactor(0), 2.0);
}

// The game only gets slower. A band that dipped would make a level-up speed the
// game up, the opposite of the ladder's purpose.
TEST_F(ProgressionTest, ThePaceNeverQuickens) {
  for (int level = 2; level <= 300; ++level) {
    EXPECT_GE(GameSpeedFactor(level), GameSpeedFactor(level - 1))
        << "at level " << level;
  }
}

// --- the hotkeys tip ---

// The one thing that goes away instead of unlocking, so both directions matter:
// every level it should be shown at, and every level after it goes.
TEST_F(ProgressionTest, TheTipStandsUntilItRetires) {
  for (int level = 1; level < HotkeysTipRetireLevel(); ++level) {
    CharacterInstance c = MakeCharacter(level);
    EXPECT_TRUE(HotkeysTipVisible(c, account_))
        << "should still be up at " << level;
  }
  CharacterInstance retired = MakeCharacter(HotkeysTipRetireLevel());
  EXPECT_FALSE(HotkeysTipVisible(retired, account_));
  CharacterInstance later = MakeCharacter(HotkeysTipRetireLevel() + 20);
  EXPECT_FALSE(HotkeysTipVisible(later, account_));
}

// It explains the panels unlocking around it, so it must stay until after the
// last of them instead of leaving while one is still new.
TEST_F(ProgressionTest, TheHotkeysTipOutlastsEveryPanelItExplains) {
  EXPECT_GT(HotkeysTipRetireLevel(), UnlockLevel(Feature::kEquipped));
  EXPECT_GT(HotkeysTipRetireLevel(), UnlockLevel(Feature::kBag));
}

}  // namespace
}  // namespace ms
