#include "src/frontend/widgets/exp_bar.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/exp_table.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

std::string Draw(const Character& character) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Element bar = ExpBar(character);
  ftxui::Render(screen, bar);
  return screen.ToString();
}

// How many columns of the bar are filled, which shows the fraction.
int Filled(const Character& character) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Element bar = ExpBar(character);
  ftxui::Render(screen, bar);
  int filled = 0;
  for (int x = 0; x < screen.dimx(); ++x) {
    if (screen.PixelAt(x, 0).background_color == kTheme) {
      ++filled;
    }
  }
  return filled;
}

TEST(ExpBarTest, ReadsTheFigureAndThePercentage) {
  Character character;
  character.set_level(10);
  character.set_exp(ExpToNextLevel(10) / 2);

  std::string bar = Draw(character);
  EXPECT_NE(bar.find("50%"), std::string::npos);
  EXPECT_NE(bar.find(std::to_string(character.exp())), std::string::npos);
  // Halfway through the level, so half the bar.
  EXPECT_NEAR(Filled(character), 30, 1);
}

// More decimals at higher tiers: a level that needs hundreds of times more EXP
// moves its bar hundreds of times more slowly, and otherwise the player would
// only ever see 0%.
TEST(ExpBarTest, TheDecimalsFollowTheTier) {
  Character character;
  character.set_level(10);
  character.set_exp(ExpToNextLevel(10) / 3);
  EXPECT_NE(Draw(character).find("(33%)"), std::string::npos);

  character.set_level(150);
  character.set_exp(ExpToNextLevel(150) / 3);
  EXPECT_NE(Draw(character).find("(33.33%)"), std::string::npos);
}

// At the cap there is no next level, so the bar reads MAX and is full, not
// empty.
TEST(ExpBarTest, TheCapReadsMaxAndFillsTheBar) {
  Character character;
  character.set_level(kTrialLevelCap);
  character.set_exp(0);

  EXPECT_NE(Draw(character).find("MAX"), std::string::npos);
  EXPECT_EQ(Filled(character), 60);
}

}  // namespace
}  // namespace ms
