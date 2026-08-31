#include "src/frontend/celebration.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/honor.h"
#include "src/character/inner_ability.h"
#include "src/character/progression.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The card's text, read off the screen cell by cell. Not Screen::ToString,
// which threads colour escapes through every row.
std::string CardText(const Celebration& celebration) {
  ftxui::Element card = celebration.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return ScreenText(screen);
}

class CelebrationTest : public testing::Test {
 protected:
  // Starts a level-up with the player looking somewhere that is never lit, so
  // every panel the celebration touches is one they have to go and visit.
  // Tests about the timed half say where the player is standing explicitly.
  //
  // On a fresh account, so the climb is this player's first over that ground.
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
  // A tick short of the full four seconds, in the 300ms steps the game
  // actually advances in.
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.card_visible());
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.card_visible());
}

// The game advances in ticks, so the moment the clock runs out almost never
// lands on a tick boundary. Overshooting it must take the card down, not
// leave it up on a negative countdown.
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

// A second level-up while the first is still up restarts the clock rather than
// inheriting what was left of it -- otherwise a level landing a moment before
// the card expired would flash by.
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
  // And a panel off either end of the range is asked about safely.
  EXPECT_FALSE(celebration_.Lights(kNumPanels));
  EXPECT_FALSE(celebration_.Lights(kNoPanel));
}

// The level that hands over the equipped panel points at it: a card in the
// middle of the screen does not say where the new thing is.
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

// One combat tick can carry a character through both unlocks. Neither may be
// stepped over: the player would be left with two panels they were never sent
// to.
TEST_F(CelebrationTest, AClimbPastBothUnlocksLightsBoth) {
  int first = UnlockLevel(Feature::kEquipped);
  int second = UnlockLevel(Feature::kBag);
  BeginAway(first - 1, second, /*ap=*/10, /*sp=*/0);
  EXPECT_TRUE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kInventoryPanel));
}

// A climb that starts at the unlock level has already been through it, so it
// is not news again.
TEST_F(CelebrationTest, AClimbStartingOnAnUnlockDoesNotRelightIt) {
  int level = UnlockLevel(Feature::kEquipped);
  BeginAway(level, level + 1);
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
}

// A second character's panels were on screen from level 1, so the level that
// would have opened them points at nothing.
TEST_F(CelebrationTest, ASecondCharacterIsSentToNoPanel) {
  int level = UnlockLevel(Feature::kBag);
  celebration_.BeginLevelUp(level - 1, level, /*ap=*/5, /*sp=*/3,
                            /*hyper_sp=*/0, /*account_level=*/140,
                            kCombatPanel);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kCharPanel)) << "the AP is still theirs";
}

// Scrolling and star force live in an item menu, with no panel of their own to
// go gold, so the card has to name them or a player never learns they arrived.
TEST_F(CelebrationTest, TheCardNamesAnUpgradeTheClimbOpened) {
  int level = UnlockLevel(Feature::kScrolling);
  BeginAway(level - 1, level);
  EXPECT_NE(CardText(celebration_).find("Unlocked Scrolling!"),
            std::string::npos);
}

// An ordinary level names no unlock, and no honor either: honor is paid from
// the second level but goes unmentioned until Inner Ability is open.
TEST_F(CelebrationTest, TheCardNamesNoUpgradeOrHonorOnAnOrdinaryLevel) {
  BeginAway(11, 12);
  std::string card = CardText(celebration_);
  EXPECT_EQ(card.find("Unlocked"), std::string::npos);
  EXPECT_EQ(card.find("Honor"), std::string::npos);
}

// The card is rebuilt from the last climb, not accumulated: an unlock
// announced once must not ride along on the next level.
TEST_F(CelebrationTest, TheNextLevelDropsTheAnnouncement) {
  int level = UnlockLevel(Feature::kScrolling);
  BeginAway(level - 1, level);
  BeginAway(level, level + 1);
  EXPECT_EQ(CardText(celebration_).find("Unlocked"), std::string::npos);
}

// Honor is named once Inner Ability has been opened -- on this character or,
// for the one after them, on the account.
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
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN, kCombatPanel);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

TEST_F(CelebrationTest, AnAdvancementInheritsNoLitPanels) {
  int level = UnlockLevel(Feature::kBag);
  BeginAway(level - 1, level);
  ASSERT_TRUE(celebration_.Lights(kInventoryPanel));
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN, kCombatPanel);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

// --- how the gold goes out ---

// The whole reason the gold is there: the player was not looking, so it waits
// for them however long that takes.
TEST_F(CelebrationTest, GoldOnAPanelYouWereNotOnOutlivesTheCard) {
  BeginAway(12, 13);
  celebration_.Advance(kCelebrationSeconds * 10);
  ASSERT_FALSE(celebration_.card_visible());
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// And the other half: a panel already in front of them has been seen by the
// time the card names it, so it fades on the clock like the card does.
TEST_F(CelebrationTest, GoldOnThePanelYouAreOnFadesWithTheCard) {
  celebration_.BeginLevelUp(12, 13, 5, 3, /*hyper_sp=*/0,
                            /*account_level=*/0, kCharPanel);
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// Walking away before the four seconds are up does not turn a panel that was
// seen back into one that is waiting to be.
TEST_F(CelebrationTest, LeavingAPanelDoesNotRearmItsGold) {
  celebration_.BeginLevelUp(12, 13, 5, 3, /*hyper_sp=*/0,
                            /*account_level=*/0, kCharPanel);
  celebration_.Visit(kCombatPanel);
  celebration_.Advance(kCelebrationSeconds);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// Visiting is a latch. Gold that came back every time the player tabbed away
// would stop meaning "you have not seen this" and start meaning nothing.
TEST_F(CelebrationTest, VisitingAPanelPutsItsGoldOutForGood) {
  BeginAway(12, 13);
  ASSERT_TRUE(celebration_.Lights(kCharPanel));
  celebration_.Visit(kCharPanel);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));

  celebration_.Visit(kCombatPanel);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
}

// One visit is one panel. The player walked onto the bag, not onto everything.
TEST_F(CelebrationTest, VisitingOnePanelLeavesTheOthersGold) {
  int level = UnlockLevel(Feature::kBag);
  BeginAway(level - 1, level);
  celebration_.Visit(kInventoryPanel);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// Nothing is visited from the shop or map select: panel_focus_ still names a
// panel there, but it is not one the player can see.
TEST_F(CelebrationTest, VisitingNoPanelPutsNothingOut) {
  BeginAway(12, 13);
  celebration_.Visit(kNoPanel);
  celebration_.Visit(kNumPanels);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// Getting the card out of the way is not the same as having gone to look at
// what it was pointing at.
TEST_F(CelebrationTest, DismissingTheCardLeavesTheGoldAlone) {
  BeginAway(12, 13);
  celebration_.Dismiss();
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
}

// Even the timed half, which the card's own clock would otherwise have taken
// with it: four seconds of gold is four seconds whether or not the card is
// still sitting on top of it.
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
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_MAGICIAN, kCombatPanel);
  std::string text = CardText(celebration_);
  EXPECT_NE(text.find("Advancement"), std::string::npos);
  EXPECT_NE(text.find("Beginner"), std::string::npos);
  EXPECT_NE(text.find("Magician"), std::string::npos);
  EXPECT_EQ(text.find("Level Up"), std::string::npos);
}

// An advancement replaces a level-up rather than queueing behind it: taking
// one is the larger news, and the player did not ask to be shown the old card
// again first.
TEST_F(CelebrationTest, AnAdvancementReplacesALevelUpCard) {
  BeginAway(29, 30);
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN, kCombatPanel);
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

// Dying lights no panel of its own -- there is nowhere it is sending the
// player -- and takes down none of the gold a level-up on the way there put
// up. That signpost has still not been walked past.
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
