#include "src/frontend/screens/boss_clear_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/boss_run.h"
#include "src/frontend/widgets/screen_text.h"

namespace ms {
namespace {

ftxui::Screen RenderCard(const BossReward& reward, bool show_honor = true,
                         double seconds = 167.0) {
  ftxui::Element card = BossClearPanel("Normal Zakum", seconds, reward,
                                       ftxui::text("[ Continue ]"), show_honor);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

// Which row `needle` is on, or -1 for a card that does not say it.
int RowOf(const ftxui::Screen& screen, const std::string& needle) {
  std::vector<std::string> rows = ScreenRows(screen);
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find(needle) != std::string::npos) {
      return i;
    }
  }
  return -1;
}

bool AnyRowHas(const ftxui::Screen& screen, const std::string& needle) {
  return RowOf(screen, needle) >= 0;
}

BossReward FullReward() {
  BossReward reward;
  reward.meso = 3062500;
  reward.exp = 4611597;
  reward.honor = 150;
  reward.items.push_back({"Condensed Power Crystal", 1, /*prize=*/true, 1.0});
  reward.items.push_back({"Zakum's Soul Shard", 3, /*prize=*/false, 1.0});
  return reward;
}

TEST(BossClearPanelTest, NamesTheFightThePurseTheExpTheHonorAndEveryDrop) {
  ftxui::Screen screen = RenderCard(FullReward());
  EXPECT_TRUE(AnyRowHas(screen, "Normal Zakum in 2:47"));
  EXPECT_TRUE(AnyRowHas(screen, "3,062,500 meso"));
  EXPECT_TRUE(AnyRowHas(screen, "4,611,597 EXP"));
  EXPECT_TRUE(AnyRowHas(screen, "150 Honor"));
  EXPECT_TRUE(AnyRowHas(screen, "Condensed Power Crystal"));
  EXPECT_TRUE(AnyRowHas(screen, "[ Continue ]"));
}

// One of a thing is just the thing. A count is written only where there is a
// count to read.
TEST(BossClearPanelTest, OnlyMoreThanOneCarriesACount) {
  ftxui::Screen screen = RenderCard(FullReward());
  EXPECT_TRUE(AnyRowHas(screen, "Zakum's Soul Shard x3"));
  EXPECT_FALSE(AnyRowHas(screen, "Condensed Power Crystal x1"));
}

// The clear pays its honor whatever the level, but a player who cannot spend
// it yet is not told about a currency.
TEST(BossClearPanelTest, TheHonorWaitsForInnerAbility) {
  ftxui::Screen screen = RenderCard(FullReward(), /*show_honor=*/false);
  EXPECT_FALSE(AnyRowHas(screen, "Honor"));
  EXPECT_TRUE(AnyRowHas(screen, "3,062,500 meso"));
}

// The prizes are ruled off from the meso, the EXP, the honor and the shard,
// and sit under them.
TEST(BossClearPanelTest, ARuleDividesWhatWasPaidFromTheGear) {
  ftxui::Screen screen = RenderCard(FullReward());
  int shard = RowOf(screen, "Zakum's Soul Shard");
  int gear = RowOf(screen, "Condensed Power Crystal");
  ASSERT_GT(shard, 0);
  ASSERT_EQ(gear, shard + 2);
  std::vector<std::string> rows = ScreenRows(screen);
  EXPECT_NE(rows[shard + 1].find("\u2500"), std::string::npos);
}

// Nothing to rule off from: a clear that paid only gear draws no rule above
// the one line it has.
TEST(BossClearPanelTest, GearAloneIsNotRuledOffFromNothing) {
  BossReward reward;
  reward.items.push_back({"Condensed Power Crystal", 1, /*prize=*/true, 1.0});
  ftxui::Screen screen = RenderCard(reward);
  // Straight under the card's own rule, with nothing ruled off above it.
  EXPECT_EQ(RowOf(screen, "Condensed Power Crystal"),
            RowOf(screen, "Normal Zakum") + 2);
}

// The rarest thing a clear paid is the line the player is looking for, so it
// leads its group -- the reverse of the Fight panel, which lists what might
// fall rather than what did.
TEST(BossClearPanelTest, TheRarestPrizeLeadsTheGroup) {
  BossReward reward;
  reward.meso = 3062500;
  reward.items.push_back({"Common Ring", 1, /*prize=*/true, 0.5});
  reward.items.push_back({"Rare Earring", 1, /*prize=*/true, 0.05});
  reward.items.push_back({"Middling Belt", 1, /*prize=*/true, 0.2});
  ftxui::Screen screen = RenderCard(reward);

  EXPECT_LT(RowOf(screen, "Rare Earring"), RowOf(screen, "Middling Belt"));
  EXPECT_LT(RowOf(screen, "Middling Belt"), RowOf(screen, "Common Ring"));
}

// A token buys a piece of gear, so it is one of the prizes: under the rule,
// ordered among them.
TEST(BossClearPanelTest, ATokenStandsWithTheGear) {
  BossReward reward;
  reward.meso = 10300000;
  reward.items.push_back({"Cygnus's Soul Shard", 1, /*prize=*/false, 1.0});
  reward.items.push_back({"Cygnus Shoulder Token", 1, /*prize=*/true, 1.0});
  ftxui::Screen screen = RenderCard(reward);

  int shard = RowOf(screen, "Cygnus's Soul Shard");
  int token = RowOf(screen, "Cygnus Shoulder Token");
  ASSERT_EQ(token, shard + 2);
  EXPECT_NE(ScreenRows(screen)[shard + 1].find("\u2500"), std::string::npos);
}

// A clear that rolled nothing still says so rather than leaving a gap where
// the rewards would be.
TEST(BossClearPanelTest, AClearThatPaidNothingSaysSo) {
  ftxui::Screen screen = RenderCard(BossReward());
  EXPECT_TRUE(AnyRowHas(screen, "no rewards"));
  EXPECT_TRUE(AnyRowHas(screen, "[ Continue ]"));
  EXPECT_FALSE(AnyRowHas(screen, "meso"));
  EXPECT_FALSE(AnyRowHas(screen, "EXP"));
}

}  // namespace
}  // namespace ms
