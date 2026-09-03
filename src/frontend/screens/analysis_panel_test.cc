#include "src/frontend/screens/analysis_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/battle_analysis.h"
#include "src/game_state.h"

namespace ms {
namespace {

GameState EmptyState() {
  return GameState({}, {}, {}, {}, {});
}

std::string Render(const AnalysisPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, panel.Render());
  return screen.ToString();
}

// One minute of farming, so every rate is a round multiple of what it is fed.
void MeasureAMinute(BattleAnalysis& analysis) {
  analysis.Start();
  AnalysisSample beat;
  beat.respawned = true;
  analysis.Advance(beat);
  AnalysisSample tick;
  tick.seconds = 1.0;
  tick.damage = 1000.0;
  tick.kills = 2;
  tick.meso = 5000;
  tick.exp = 700;
  for (int i = 0; i < 60; ++i) {
    analysis.Advance(tick);
  }
}

TEST(AnalysisPanelTest, TheClockIsHoursMinutesSeconds) {
  EXPECT_EQ(FormatElapsed(0.0), "00:00:00");
  EXPECT_EQ(FormatElapsed(59.9), "00:00:59");
  EXPECT_EQ(FormatElapsed(3661.0), "01:01:01");
  // Hours are not wrapped: a run left overnight says so.
  EXPECT_EQ(FormatElapsed(360000.0), "100:00:00");
}

TEST(AnalysisPanelTest, ItDrawsTheTenRowsAndTheirRates) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  MeasureAMinute(analysis);
  AnalysisPanel panel(state, analysis);
  std::string out = Render(panel);

  EXPECT_NE(out.find("Battle Analysis"), std::string::npos);
  EXPECT_NE(out.find("00:01:00"), std::string::npos);
  EXPECT_NE(out.find("Respawn Cycles"), std::string::npos);
  EXPECT_NE(out.find("Damage Dealt"), std::string::npos);
  EXPECT_NE(out.find("60,000"), std::string::npos);
  EXPECT_NE(out.find("DPS"), std::string::npos);
  EXPECT_NE(out.find("Mobs Killed"), std::string::npos);
  EXPECT_NE(out.find("Mobs/h"), std::string::npos);
  EXPECT_NE(out.find("7,200"), std::string::npos);
  EXPECT_NE(out.find("Meso Earned"), std::string::npos);
  EXPECT_NE(out.find("300,000"), std::string::npos);
  EXPECT_NE(out.find("Meso/h"), std::string::npos);
  EXPECT_NE(out.find("18,000,000"), std::string::npos);
  EXPECT_NE(out.find("EXP Earned"), std::string::npos);
  EXPECT_NE(out.find("42,000"), std::string::npos);
  EXPECT_NE(out.find("EXP/h"), std::string::npos);
  EXPECT_NE(out.find("2,520,000"), std::string::npos);
  EXPECT_NE(out.find("Close"), std::string::npos);
}

TEST(AnalysisPanelTest, TheStatusRowSaysWhatTheToolIsDoing) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  AnalysisPanel panel(state, analysis);
  EXPECT_NE(Render(panel).find("Stopped"), std::string::npos);

  analysis.Start();
  EXPECT_NE(Render(panel).find("Waiting for Respawn to Start"),
            std::string::npos);

  AnalysisSample beat;
  beat.respawned = true;
  analysis.Advance(beat);
  EXPECT_NE(Render(panel).find("Running"), std::string::npos);

  analysis.Stop();
  EXPECT_NE(Render(panel).find("Waiting for Respawn to Stop"),
            std::string::npos);
}

// The slowdown row is why the damage per second reads low, so it names the
// factor the character's own level is running at.
TEST(AnalysisPanelTest, TheSlowdownRowFollowsTheLevel) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  AnalysisPanel panel(state, analysis);
  EXPECT_NE(Render(panel).find("Game Slowdown Factor"), std::string::npos);
  EXPECT_NE(Render(panel).find("2x"), std::string::npos);

  while (state.character.proto().level() < 200) {
    state.character.LevelUp();
  }
  EXPECT_NE(Render(panel).find("10x"), std::string::npos);
}

}  // namespace
}  // namespace ms
