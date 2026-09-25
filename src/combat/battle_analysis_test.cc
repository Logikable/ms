#include "src/combat/battle_analysis.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

AnalysisSample Beat(double seconds) {
  AnalysisSample sample;
  sample.seconds = seconds;
  sample.respawned = true;
  return sample;
}

AnalysisSample Tick(double seconds, double damage, int64_t kills, int64_t meso,
                    int64_t exp) {
  AnalysisSample sample;
  sample.seconds = seconds;
  sample.damage = damage;
  sample.kills = kills;
  sample.meso = meso;
  sample.exp = exp;
  return sample;
}

TEST(BattleAnalysisTest, StartsStoppedAndEmpty) {
  BattleAnalysis analysis;
  EXPECT_EQ(analysis.state(), AnalysisState::kStopped);
  EXPECT_FALSE(analysis.stops_on_press());
  EXPECT_EQ(analysis.seconds(), 0.0);
  EXPECT_EQ(analysis.damage_per_second(), 0);
  EXPECT_EQ(analysis.meso_per_hour(), 0);
}

// Nothing is counted between Start and the beat it waits for.
TEST(BattleAnalysisTest, WaitsForTheBeatToStart) {
  BattleAnalysis analysis;
  analysis.Start();
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStart);
  EXPECT_TRUE(analysis.stops_on_press());

  analysis.Advance(Tick(1.0, 500.0, 3, 400, 90));
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStart);
  EXPECT_EQ(analysis.seconds(), 0.0);
  EXPECT_EQ(analysis.damage(), 0);
  EXPECT_EQ(analysis.kills(), 0);

  analysis.Advance(Beat(1.0));
  EXPECT_EQ(analysis.state(), AnalysisState::kRunning);
  EXPECT_EQ(analysis.seconds(), 1.0);
  // The beat that starts the measurement is a boundary, not a cycle.
  EXPECT_EQ(analysis.cycles(), 0);
}

TEST(BattleAnalysisTest, TotalsAndRates) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  for (int i = 0; i < 60; ++i) {
    analysis.Advance(Tick(1.0, 1000.0, 2, 5000, 700));
  }
  EXPECT_EQ(analysis.seconds(), 60.0);
  EXPECT_EQ(analysis.damage(), 60000);
  EXPECT_EQ(analysis.kills(), 120);
  EXPECT_EQ(analysis.meso(), 300000);
  EXPECT_EQ(analysis.exp(), 42000);
  EXPECT_EQ(analysis.damage_per_second(), 1000);
  EXPECT_EQ(analysis.kills_per_hour(), 7200);
  EXPECT_EQ(analysis.meso_per_hour(), 18000000);
  EXPECT_EQ(analysis.exp_per_hour(), 2520000);
}

// Every beat after the starting one is a cycle, including the one that stops
// the measurement.
TEST(BattleAnalysisTest, CountsWholeCycles) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  for (int i = 0; i < 5; ++i) {
    analysis.Advance(Tick(1.0, 0.0, 0, 0, 0));
    analysis.Advance(Beat(1.0));
  }
  EXPECT_EQ(analysis.cycles(), 5);
}

// Stop ends the measurement on the next beat, and damage from the tick that
// carries that beat still counts.
TEST(BattleAnalysisTest, WaitsForTheBeatToStop) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  analysis.Advance(Tick(1.0, 100.0, 1, 10, 5));
  analysis.Stop();
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStop);
  // The button reads Start again, so one more press cancels the stop.
  EXPECT_FALSE(analysis.stops_on_press());

  analysis.Advance(Tick(1.0, 100.0, 1, 10, 5));
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStop);
  EXPECT_EQ(analysis.kills(), 2);

  AnalysisSample last = Beat(1.0);
  last.damage = 100.0;
  last.kills = 1;
  analysis.Advance(last);
  EXPECT_EQ(analysis.state(), AnalysisState::kStopped);
  EXPECT_EQ(analysis.cycles(), 1);
  EXPECT_EQ(analysis.kills(), 3);
  EXPECT_EQ(analysis.damage(), 300);

  // The numbers stay on screen for the player to read.
  analysis.Advance(Beat(60.0));
  EXPECT_EQ(analysis.seconds(), 3.0);
  EXPECT_EQ(analysis.kills(), 3);
}

// Pressing Start while a stop is pending cancels the stop, and the measurement
// carries on with everything it had.
TEST(BattleAnalysisTest, StartTakesBackAPendingStop) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  analysis.Advance(Tick(1.0, 100.0, 1, 10, 5));
  analysis.Stop();
  ASSERT_EQ(analysis.state(), AnalysisState::kWaitingToStop);

  analysis.Start();
  EXPECT_EQ(analysis.state(), AnalysisState::kRunning);
  EXPECT_EQ(analysis.kills(), 1);
  EXPECT_EQ(analysis.seconds(), 1.0);

  // The beat it was waiting for now counts as a cycle, not the end.
  analysis.Advance(Beat(1.0));
  EXPECT_EQ(analysis.state(), AnalysisState::kRunning);
  EXPECT_EQ(analysis.cycles(), 1);
}

// Start while a measurement is running leaves it alone instead of discarding
// it.
TEST(BattleAnalysisTest, StartWhileRunningKeepsTheMeasurement) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  analysis.Advance(Tick(1.0, 100.0, 1, 10, 5));
  analysis.Start();
  EXPECT_EQ(analysis.state(), AnalysisState::kRunning);
  EXPECT_EQ(analysis.kills(), 1);
}

// Stop before the first beat cancels the tool instead of leaving it armed.
TEST(BattleAnalysisTest, StopBeforeTheFirstBeatCancels) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Stop();
  EXPECT_EQ(analysis.state(), AnalysisState::kStopped);
  analysis.Advance(Beat(1.0));
  EXPECT_EQ(analysis.state(), AnalysisState::kStopped);
  EXPECT_EQ(analysis.seconds(), 0.0);
}

// Starting again clears the last measurement.
TEST(BattleAnalysisTest, StartClearsTheLastMeasurement) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  analysis.Advance(Tick(5.0, 900.0, 7, 800, 60));
  analysis.Stop();
  analysis.Advance(Beat(0.0));
  ASSERT_EQ(analysis.kills(), 7);

  analysis.Start();
  EXPECT_EQ(analysis.seconds(), 0.0);
  EXPECT_EQ(analysis.kills(), 0);
  EXPECT_EQ(analysis.damage(), 0);
  EXPECT_EQ(analysis.cycles(), 0);
}

// When the caller stops feeding the tool (as the boss screen does), its clock
// stops too.
TEST(BattleAnalysisTest, TimeOnlyPassesWhenItIsFed) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  analysis.Advance(Tick(2.0, 0.0, 0, 0, 0));
  EXPECT_EQ(analysis.seconds(), 2.0);
  analysis.Advance(Tick(2.0, 0.0, 0, 0, 0));
  EXPECT_EQ(analysis.seconds(), 4.0);
}

}  // namespace
}  // namespace ms
