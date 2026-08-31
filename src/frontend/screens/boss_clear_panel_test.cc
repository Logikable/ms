#include "src/frontend/screens/boss_clear_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/boss_run.h"

namespace ms {
namespace {

ftxui::Screen RenderCard(const BossReward& reward, bool show_honor = true) {
  ftxui::Element card = BossClearPanel("Normal Zakum", reward,
                                       ftxui::text("[ Continue ]"), show_honor);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

std::vector<std::string> Rows(const ftxui::Screen& screen) {
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.empty() ? " " : cell;
    }
    rows.push_back(row);
  }
  return rows;
}

bool AnyRowHas(const ftxui::Screen& screen, const std::string& needle) {
  for (const std::string& row : Rows(screen)) {
    if (row.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

BossReward FullReward() {
  BossReward reward;
  reward.meso = 3062500;
  reward.exp = 4611597;
  reward.honor = 150;
  reward.items.push_back({"Condensed Power Crystal", 1});
  reward.items.push_back({"Zakum's Soul Shard", 3});
  return reward;
}

TEST(BossClearPanelTest, NamesTheFightThePurseTheExpTheHonorAndEveryDrop) {
  ftxui::Screen screen = RenderCard(FullReward());
  EXPECT_TRUE(AnyRowHas(screen, "Normal Zakum"));
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
