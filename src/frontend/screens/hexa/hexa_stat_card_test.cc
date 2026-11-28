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

HexaStatNode Node(int primary, int first, int second) {
  HexaStatNode node;
  node.set_primary(HEXA_STAT_ATTACK);
  node.set_primary_level(primary);
  node.set_first(HEXA_STAT_CRIT_DAMAGE);
  node.set_first_level(first);
  node.set_second(HEXA_STAT_IGNORE_DEFENSE);
  node.set_second_level(second);
  return node;
}

ftxui::Screen Render(const HexaStatNode& node) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(70),
                                               ftxui::Dimension::Fixed(12));
  ftxui::Render(screen, HexaStatCard(1, node, STAT_FIELD_INT));
  return screen;
}

TEST(HexaStatCardTest, ShowsEachStatItsAmountChanceAndLevel) {
  const ftxui::Screen screen = Render(Node(8, 9, 0));
  const std::string text = ScreenText(screen);
  EXPECT_NE(text.find("HEXA Stat II"), std::string::npos);
  EXPECT_NE(text.find("Stat"), std::string::npos);
  EXPECT_NE(text.find("Chance"), std::string::npos);
  EXPECT_NE(text.find("Level"), std::string::npos);
  EXPECT_NE(text.find("M: Magic ATT +65"), std::string::npos);
  EXPECT_NE(text.find("A: Crit Damage +3.15%"), std::string::npos);
  // Level 0 adds nothing, so the row is the name alone.
  EXPECT_NE(text.find("A: Ignore DEF "), std::string::npos);
  EXPECT_EQ(text.find("Ignore DEF +"), std::string::npos);
  // The primary at 8 takes 10%; the other 90% splits between the two.
  EXPECT_NE(text.find("10.0%"), std::string::npos);
  EXPECT_NE(text.find("45.0%"), std::string::npos);
  EXPECT_NE(text.find("[Close]"), std::string::npos);
  EXPECT_EQ(ColorOf(screen, "M:"), kGold);
  EXPECT_EQ(ColorOf(screen, "A:"), kGreen);
  // The levels end in one column.
  const ScreenPos eight = FindOnScreen(screen, "8/10");
  const ScreenPos nine = FindOnScreen(screen, "9/10");
  const ScreenPos zero = FindOnScreen(screen, "0/10");
  EXPECT_EQ(eight.x, nine.x);
  EXPECT_EQ(eight.x, zero.x);
}

TEST(HexaStatCardTest, AStatAtTenReadsNoChance) {
  const std::string text = ScreenText(Render(Node(10, 4, 3)));
  EXPECT_NE(text.find("0.0%"), std::string::npos);
  EXPECT_NE(text.find("50.0%"), std::string::npos);
}

TEST(HexaStatCardTest, AFullNodeShowsDashes) {
  const std::string text = ScreenText(Render(Node(10, 5, 5)));
  EXPECT_EQ(text.find(".0%"), std::string::npos);
  EXPECT_NE(text.find("—"), std::string::npos);
}

}  // namespace
}  // namespace ms
