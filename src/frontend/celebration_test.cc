#include "src/frontend/celebration.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/progression.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The card's text, read off the screen cell by cell. Not Screen::ToString,
// which threads colour escapes through every row.
std::string CardText(const Celebration& celebration) {
  ftxui::Element card = celebration.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  std::string text;
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      text += cell.empty() ? " " : cell;
    }
    text += "\n";
  }
  return text;
}

class CelebrationTest : public testing::Test {
 protected:
  Celebration celebration_;
};

// --- the clock ---

TEST_F(CelebrationTest, StartsInactive) {
  EXPECT_FALSE(celebration_.active());
  EXPECT_EQ(celebration_.kind(), Celebration::Kind::kNone);
}

TEST_F(CelebrationTest, StaysUpForTheWholeDuration) {
  celebration_.BeginLevelUp(12, 13, 5, 3);
  ASSERT_TRUE(celebration_.active());
  // A tick short of the full four seconds, in the 300ms steps the game
  // actually advances in.
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.active());
  celebration_.Advance(0.3);
  EXPECT_FALSE(celebration_.active());
}

// The game advances in ticks, so the moment the clock runs out almost never
// lands on a tick boundary. Overshooting it must end the celebration, not
// leave it up on a negative countdown.
TEST_F(CelebrationTest, EndsWhenTheClockIsOvershot) {
  celebration_.BeginLevelUp(12, 13, 5, 3);
  celebration_.Advance(kCelebrationSeconds * 10);
  EXPECT_FALSE(celebration_.active());
}

TEST_F(CelebrationTest, AdvancingWithNothingUpIsHarmless) {
  celebration_.Advance(1.0);
  EXPECT_FALSE(celebration_.active());
}

TEST_F(CelebrationTest, DismissEndsItEarly) {
  celebration_.BeginLevelUp(12, 13, 5, 3);
  celebration_.Dismiss();
  EXPECT_FALSE(celebration_.active());
}

// A second level-up while the first is still up restarts the clock rather than
// inheriting what was left of it -- otherwise a level landing a moment before
// the card expired would flash by.
TEST_F(CelebrationTest, ASecondLevelUpGetsAFullFourSecondsOfItsOwn) {
  celebration_.BeginLevelUp(12, 13, 5, 3);
  celebration_.Advance(kCelebrationSeconds - 0.3);
  celebration_.BeginLevelUp(13, 14, 5, 3);
  celebration_.Advance(kCelebrationSeconds - 0.3);
  EXPECT_TRUE(celebration_.active());
}

// --- which panels are lit ---

TEST_F(CelebrationTest, AnOrdinaryLevelUpLightsOnlyTheCharacterPanel) {
  celebration_.BeginLevelUp(12, 13, 5, 3);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
  EXPECT_FALSE(celebration_.Lights(kCombatPanel));
}

// The level that hands over the equipped panel points at it: a card in the
// middle of the screen does not say where the new thing is.
TEST_F(CelebrationTest, TheLevelThatOpensTheEquippedPanelLightsIt) {
  int level = UnlockLevel(Feature::kEquipped);
  celebration_.BeginLevelUp(level - 1, level, 5, 0);
  EXPECT_TRUE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kCharPanel)) << "always, as well";
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

TEST_F(CelebrationTest, TheLevelThatOpensTheBagLightsIt) {
  int level = UnlockLevel(Feature::kBag);
  celebration_.BeginLevelUp(level - 1, level, 5, 0);
  EXPECT_TRUE(celebration_.Lights(kInventoryPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
}

// One combat tick can carry a character through both unlocks. Neither may be
// stepped over: the player would be left with two panels they were never sent
// to.
TEST_F(CelebrationTest, AClimbPastBothUnlocksLightsBoth) {
  int first = UnlockLevel(Feature::kEquipped);
  int second = UnlockLevel(Feature::kBag);
  celebration_.BeginLevelUp(first - 1, second, 10, 0);
  EXPECT_TRUE(celebration_.Lights(kEquipPanel));
  EXPECT_TRUE(celebration_.Lights(kInventoryPanel));
}

// A climb that starts at the unlock level has already been through it, so it
// is not news again.
TEST_F(CelebrationTest, AClimbStartingOnAnUnlockDoesNotRelightIt) {
  int level = UnlockLevel(Feature::kEquipped);
  celebration_.BeginLevelUp(level, level + 1, 5, 0);
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
}

TEST_F(CelebrationTest, AnAdvancementLightsTheCharacterPanelOnly) {
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_TRUE(celebration_.Lights(kCharPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

// Nothing is lit once it is over, so the caller can set every panel from
// Lights() every frame and never has to remember to put them out.
TEST_F(CelebrationTest, NothingIsLitOnceItHasExpired) {
  int level = UnlockLevel(Feature::kEquipped);
  celebration_.BeginLevelUp(level - 1, level, 5, 0);
  celebration_.Advance(kCelebrationSeconds);
  EXPECT_FALSE(celebration_.Lights(kCharPanel));
  EXPECT_FALSE(celebration_.Lights(kEquipPanel));
}

TEST_F(CelebrationTest, AnAdvancementDoesNotInheritALevelUpsLitPanels) {
  int level = UnlockLevel(Feature::kBag);
  celebration_.BeginLevelUp(level - 1, level, 5, 0);
  ASSERT_TRUE(celebration_.Lights(kInventoryPanel));
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_FALSE(celebration_.Lights(kInventoryPanel));
}

TEST_F(CelebrationTest, LightsIsSafeForAPanelOutsideTheRange) {
  celebration_.BeginLevelUp(12, 13, 5, 3);
  EXPECT_FALSE(celebration_.Lights(kNumPanels));
  EXPECT_FALSE(celebration_.Lights(static_cast<Panel>(-1)));
}

// --- which card ---

TEST_F(CelebrationTest, ALevelUpRendersTheLevelUpCard) {
  celebration_.BeginLevelUp(12, 15, 15, 9);
  celebration_.Advance(0.3);
  std::string text = CardText(celebration_);
  EXPECT_NE(text.find("Level Up"), std::string::npos);
  EXPECT_NE(text.find("12  →  15"), std::string::npos);
  EXPECT_NE(text.find("+15 AP"), std::string::npos);
  EXPECT_NE(text.find("+9 SP"), std::string::npos);
}

TEST_F(CelebrationTest, AnAdvancementRendersTheAdvancementCard) {
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_MAGICIAN);
  std::string text = CardText(celebration_);
  EXPECT_NE(text.find("Advancement"), std::string::npos);
  EXPECT_NE(text.find("Beginner"), std::string::npos);
  EXPECT_NE(text.find("Magician"), std::string::npos);
  EXPECT_EQ(text.find("Level Up"), std::string::npos);
}

// An advancement replaces a level-up rather than queueing behind it: taking
// one is the larger news, and the player did not ask to be shown the old card
// again first.
TEST_F(CelebrationTest, AnAdvancementReplacesALevelUpStillOnScreen) {
  celebration_.BeginLevelUp(29, 30, 5, 3);
  celebration_.BeginAdvancement(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_EQ(celebration_.kind(), Celebration::Kind::kAdvancement);
  EXPECT_NE(CardText(celebration_).find("Advancement"), std::string::npos);
}

}  // namespace
}  // namespace ms
