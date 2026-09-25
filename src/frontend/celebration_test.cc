#include "src/frontend/celebration.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/honor.h"
#include "src/character/inner_ability.h"
#include "src/character/progression.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The card's text, read from the screen cell by cell. Not Screen::ToString,
// which puts colour escapes in every row.
std::string CardText(const Celebration& celebration) {
  ftxui::Element card = celebration.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return ScreenText(screen);
}

class CelebrationTest : public testing::Test {
 protected:
  // Starts a level-up with the player looking at a panel that is never lit, so
  // every panel the celebration lights is one they must visit. Tests about
  // timed glows set where the player is explicitly.
  //
  // It uses a new account, so this is the first time any character has reached
  // these levels.
  void BeginAway(int from_level, int to_level, int ap = 5, int sp = 3) {
    celebration_.BeginLevelUp(from_level, to_level, ap, sp, /*hyper_sp=*/0,
                              /*account_level=*/0, kCombatPanel);
  }

  Celebration celebration_;
};

// --- the card's clock ---

TEST_F(CelebrationTest, StartsWithNoCardUp) {
  EXPECT_FALSE(celebration_.card_visible());
  EXPECT_EQ(celebration_.kind(), Celebration::Kind::kNone);
}

TEST_F(CelebrationTest, TheCardStaysUpForTheWholeDuration) {
  BeginAway(12, 13);
  ASSERT_TRUE(celebration_.card_visible());
  // One tick short of the full four seconds, in the 300ms steps the game really
  // uses.
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.card_visible());
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.card_visible());
}

// The game advances in ticks, so the clock almost never runs out exactly on a
// tick. Overshooting must take the card down, not leave it up with a negative
// countdown.
TEST_F(CelebrationTest, TheCardGoesWhenTheClockIsOvershot) {
  BeginAway(12, 13);
  celebration_.Advance(kCelebrationSeconds * 10);
  EXPECT_FALSE(celebration_.card_visible());
}

TEST_F(CelebrationTest, AdvancingWithNothingUpIsHarmless) {
  celebration_.Advance(1.0);
  EXPECT_FALSE(celebration_.card_visible());
}

TEST_F(CelebrationTest, DismissTakesTheCardDownEarly) {
  BeginAway(12, 13);
  celebration_.Dismiss();
  EXPECT_FALSE(celebration_.card_visible());
}

// A second level-up while the first card is up restarts the clock instead of
// keeping what was left. Otherwise a level gained just before the card expired
// would flash by.
TEST_F(CelebrationTest, ASecondLevelUpGetsItsOwnFourSeconds) {
  BeginAway(12, 13);
  celebration_.Advance(kCelebrationSeconds - 0.3);
  BeginAway(13, 14);
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.card_visible());
}

// --- which panels are lit ---

TEST_F(CelebrationTest, AnOrdinaryLevelUpLightsOnePanel) {
  BeginAway(12, 13);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
  EXPECT_FALSE(celebration_.Lights(kCombatPanel));
  // A panel outside the valid range can also be checked safely.
  EXPECT_FALSE(celebration_.Lights(kNumPanels));
  EXPECT_FALSE(celebration_.Lights(kNoPanel));
}

// The level that unlocks the equipped panel lights it: a card in the middle of
// the screen doesn't say where the new thing is.
TEST_F(CelebrationTest, TheLevelThatOpensTheEquippedPanelLightsIt) {
  int level = UnlockLevel(Feature::kEquipped);
  BeginAway(level - 1, level);
  EXPECT_TRUE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kCharPanel)) << "always, as well";
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

TEST_F(CelebrationTest, TheLevelThatOpensTheBagLightsIt) {
  int level = UnlockLevel(Feature::kBag);
  BeginAway(level - 1, level);
  EXPECT_TRUE(celebration_.Lights(kInventoryPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
}

// One combat tick can take a character past both unlocks. Neither may be
// skipped, or the player would have two new panels they were never pointed to.
TEST_F(CelebrationTest, AClimbPastBothUnlocksLightsBoth) {
  int first = UnlockLevel(Feature::kEquipped);
  int second = UnlockLevel(Feature::kBag);
  BeginAway(first - 1, second, /*ap=*/10, /*sp=*/0);
  EXPECT_TRUE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kInventoryPanel));
}

// A climb that starts at the unlock level has already passed it, so it isn't
// news again.
TEST_F(CelebrationTest, AClimbStartingOnAnUnlockDoesNotRelightIt) {
  int level = UnlockLevel(Feature::kEquipped);
  BeginAway(level, level + 1);
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
}

// A second character's panels were visible from level 1, so the level that
// would have unlocked them lights nothing.
TEST_F(CelebrationTest, ASecondCharacterIsSentToNoPanel) {
  int level = UnlockLevel(Feature::kBag);
  celebration_.BeginLevelUp(level - 1, level, /*ap=*/5, /*sp=*/3,
                            /*hyper_sp=*/0, /*account_level=*/140,
                            kCombatPanel);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kCharPanel)) << "the AP is still theirs";
}

// Scrolling and star force are in an item menu with no panel to light, so the
// card must name them or the player never learns they arrived.
TEST_F(CelebrationTest, TheCardNamesAnUpgradeTheClimbOpened) {
  int level = UnlockLevel(Feature::kScrolling);
  BeginAway(level - 1, level);
  EXPECT_NE(CardText(celebration_).find("Unlocked Scrolling!"),
            std::string::npos);
}

// An ordinary level names no unlock and no honor. Honor is paid from level 2
// but isn't mentioned until Inner Ability is unlocked.
TEST_F(CelebrationTest, TheCardNamesNoUpgradeOrHonorOnAnOrdinaryLevel) {
  BeginAway(11, 12);
  std::string card = CardText(celebration_);
  EXPECT_EQ(card.find("Unlocked"), std::string::npos);
  EXPECT_EQ(card.find("Honor"), std::string::npos);
}

// The card is rebuilt from the latest climb, not accumulated: an unlock
// announced once must not appear again on the next level.
TEST_F(CelebrationTest, TheNextLevelDropsTheAnnouncement) {
  int level = UnlockLevel(Feature::kScrolling);
  BeginAway(level - 1, level);
  BeginAway(level, level + 1);
  EXPECT_EQ(CardText(celebration_).find("Unlocked"), std::string::npos);
}

// Honor is shown once Inner Ability is unlocked, on this character or, for
// later characters, on the account.
TEST_F(CelebrationTest, TheCardNamesTheHonorOnceInnerAbilityIsOpen) {
  BeginAway(159, 160);
  EXPECT_NE(CardText(celebration_).find("+1,800 Honor"), std::string::npos);
}

TEST_F(CelebrationTest, ASecondCharacterIsToldAboutTheirHonor) {
  celebration_.BeginLevelUp(11, 12, /*ap=*/5, /*sp=*/3, /*hyper_sp=*/0,
                            /*account_level=*/kInnerAbilityUnlockLevel,
                            kCombatPanel);
  EXPECT_NE(CardText(celebration_).find("+700 Honor"), std::string::npos);
}

TEST_F(CelebrationTest, AnAdvancementLightsTheCharacterPanelOnly) {
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN, 1, kCombatPanel);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

TEST_F(CelebrationTest, AnAdvancementInheritsNoLitPanels) {
  int level = UnlockLevel(Feature::kBag);
  BeginAway(level - 1, level);
  ASSERT_TRUE(celebration_.Lights(kInventoryPanel));
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN, 1, kCombatPanel);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

// --- how the gold goes out ---

// This is why the gold exists: the player wasn't looking, so it waits for them
// however long that takes.
TEST_F(CelebrationTest, GoldOnAPanelYouWereNotOnOutlivesTheCard) {
  BeginAway(12, 13);
  celebration_.Advance(kCelebrationSeconds * 10);
  ASSERT_FALSE(celebration_.card_visible());
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// The other half: a panel the player is already on has been seen by the time
// the card names it, so it fades on the clock like the card.
TEST_F(CelebrationTest, GoldOnThePanelYouAreOnFadesWithTheCard) {
  celebration_.BeginLevelUp(12, 13, 5, 3, /*hyper_sp=*/0,
                            /*account_level=*/0, kCharPanel);
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// Leaving before the four seconds are up doesn't turn a seen panel back into
// one waiting to be visited.
TEST_F(CelebrationTest, LeavingAPanelDoesNotRearmItsGold) {
  celebration_.BeginLevelUp(12, 13, 5, 3, /*hyper_sp=*/0,
                            /*account_level=*/0, kCharPanel);
  celebration_.Visit(kCombatPanel);
  celebration_.Advance(kCelebrationSeconds);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// Visiting is permanent. Gold that came back every time the player tabbed away
// would stop meaning "you haven't seen this".
TEST_F(CelebrationTest, VisitingAPanelPutsItsGoldOutForGood) {
  BeginAway(12, 13);
  ASSERT_TRUE(celebration_.Lights(kCharPanel));
  celebration_.Visit(kCharPanel);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));

  celebration_.Visit(kCombatPanel);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// A visit clears only that panel. The player went to the bag, not everywhere.
TEST_F(CelebrationTest, VisitingOnePanelLeavesTheOthersGold) {
  int level = UnlockLevel(Feature::kBag);
  BeginAway(level - 1, level);
  celebration_.Visit(kInventoryPanel);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// Nothing counts as visited from the shop or map select: panel_focus_ still
// names a panel there, but the player can't see it.
TEST_F(CelebrationTest, VisitingNoPanelPutsNothingOut) {
  BeginAway(12, 13);
  celebration_.Visit(kNoPanel);
  celebration_.Visit(kNumPanels);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// Dismissing the card isn't the same as visiting what it pointed at.
TEST_F(CelebrationTest, DismissingTheCardLeavesTheGoldAlone) {
  BeginAway(12, 13);
  celebration_.Dismiss();
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// That includes timed glows, which the card's own clock would otherwise take
// with it: four seconds of gold lasts four seconds whether or not the card is
// still up.
TEST_F(CelebrationTest, DismissingTheCardKeepsTheGlow) {
  celebration_.BeginLevelUp(12, 13, 5, 3, /*hyper_sp=*/0,
                            /*account_level=*/0, kCharPanel);
  celebration_.Dismiss();
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// --- which card ---

TEST_F(CelebrationTest, ALevelUpRendersTheLevelUpCard) {
  celebration_.BeginLevelUp(12, 15, 15, 9, /*hyper_sp=*/0,
                            /*account_level=*/0, kCombatPanel);
  celebration_.Advance(0.3);
  std::string text = CardText(celebration_);
  EXPECT_NE(text.find("Level Up"), std::string::npos);
  EXPECT_NE(text.find("12  →  15"), std::string::npos);
  EXPECT_NE(text.find("+15 AP"), std::string::npos);
  EXPECT_NE(text.find("+9 SP"), std::string::npos);
}

TEST_F(CelebrationTest, AnAdvancementRendersTheAdvancementCard) {
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_MAGICIAN, 1, kCombatPanel);
  std::string text = CardText(celebration_);
  EXPECT_NE(text.find("Advancement"), std::string::npos);
  EXPECT_NE(text.find("Beginner"), std::string::npos);
  EXPECT_NE(text.find("Magician"), std::string::npos);
  EXPECT_EQ(text.find("Level Up"), std::string::npos);
}

// An advancement replaces a level-up card instead of queueing behind it: it is
// the bigger news, and the player didn't ask to see the old card again first.
TEST_F(CelebrationTest, AnAdvancementReplacesALevelUpCard) {
  BeginAway(29, 30);
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN, 1, kCombatPanel);
  EXPECT_EQ(celebration_.kind(), Celebration::Kind::kAdvancement);
  EXPECT_NE(CardText(celebration_).find("Advancement"), std::string::npos);
}

// --- dying ---

TEST_F(CelebrationTest, ADeathRendersTheDeathCard) {
  celebration_.BeginDeath();
  ASSERT_TRUE(celebration_.card_visible());
  EXPECT_EQ(celebration_.kind(), Celebration::Kind::kDeath);
  std::string text = CardText(celebration_);
  EXPECT_NE(text.find("Death"), std::string::npos);
  EXPECT_NE(text.find("You died!"), std::string::npos);
}

TEST_F(CelebrationTest, TheDeathCardGetsTheSameFourSeconds) {
  celebration_.BeginDeath();
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.card_visible());
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.card_visible());
}

TEST_F(CelebrationTest, ADeathReplacesALevelUpStillOnScreen) {
  BeginAway(12, 13);
  celebration_.BeginDeath();
  EXPECT_EQ(celebration_.kind(), Celebration::Kind::kDeath);
  EXPECT_EQ(CardText(celebration_).find("Level Up"), std::string::npos);
}

// Dying lights no panel, since it points the player nowhere, and doesn't clear
// gold from an earlier level-up. That panel still hasn't been visited.
TEST_F(CelebrationTest, ADeathNeitherLightsNorUnlightsAnything) {
  celebration_.BeginDeath();
  EXPECT_FALSE(celebration_.Lights(kCharPanel));

  BeginAway(12, 13);
  ASSERT_TRUE(celebration_.Lights(kCharPanel));
  celebration_.BeginDeath();
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

}  // namespace
}  // namespace ms
