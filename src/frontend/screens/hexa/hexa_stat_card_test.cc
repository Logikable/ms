#include "src/frontend/screens/hexa/hexa_stat_card.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/text/colors.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

TEST(HexaStatCardTest, ShowsTheNumeralEachStatAndItsLevel) {
  HexaStatNode node;
  node.set_primary(HEXA_STAT_ATTACK);
  node.set_primary_level(4);
  node.set_first(HEXA_STAT_MAIN_STAT);
  node.set_first_level(10);
  node.set_second(HEXA_STAT_IGNORE_DEFENSE);
  node.set_second_level(6);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(12));
  ftxui::Render(screen, HexaStatCard(1, node, STAT_FIELD_INT));
  const std::string text = ScreenText(screen);
  EXPECT_NE(text.find("HEXA Stat"), std::string::npos);
  EXPECT_NE(FindOnScreen(screen, "II").y, -1);
  EXPECT_NE(text.find("M: Magic ATT"), std::string::npos);
  EXPECT_NE(text.find("A: INT"), std::string::npos);
  EXPECT_NE(text.find("A: Ignore DEF"), std::string::npos);
  EXPECT_NE(text.find("[Close]"), std::string::npos);
  EXPECT_EQ(ColorOf(screen, "M:"), kGold);
  EXPECT_EQ(ColorOf(screen, "A:"), kGreen);
  // The levels end in one column.
  const ScreenPos four = FindOnScreen(screen, "4/10");
  const ScreenPos ten = FindOnScreen(screen, "10/10");
  EXPECT_EQ(four.x + 4, ten.x + 5);
}

}  // namespace
}  // namespace ms
