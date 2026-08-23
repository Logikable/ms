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
  EXPECT_FALSE(analysis.started());
  EXPECT_EQ(analysis.seconds(), 0.0);
  EXPECT_EQ(analysis.damage_per_second(), 0);
  EXPECT_EQ(analysis.meso_per_hour(), 0);
}

// Nothing is counted between Start and the beat it waits for.
TEST(BattleAnalysisTest, WaitsForTheBeatToStart) {
  BattleAnalysis analysis;
  analysis.Start();
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStart);
  EXPECT_TRUE(analysis.started());

  analysis.Advance(Tick(1.0, 500.0, 3, 400, 90));
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStart);
  EXPECT_EQ(analysis.seconds(), 0.0);
  EXPECT_EQ(analysis.damage(), 0);
  EXPECT_EQ(analysis.kills(), 0);

  analysis.Advance(Beat(1.0));
  EXPECT_EQ(analysis.state(), AnalysisState::kRunning);
  EXPECT_EQ(analysis.seconds(), 1.0);
  // The beat that started it is a boundary, not a cycle.
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

// Every beat after the one that started it is a cycle, including the one that
// stops the measurement.
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

// Stop rounds the measurement off on the next beat, and what the tick that
// carries the beat paid still counts.
TEST(BattleAnalysisTest, WaitsForTheBeatToStop) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Advance(Beat(0.0));
  analysis.Advance(Tick(1.0, 100.0, 1, 10, 5));
  analysis.Stop();
  EXPECT_EQ(analysis.state(), AnalysisState::kWaitingToStop);
  EXPECT_TRUE(analysis.started());

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

  // The numbers stay put for the player to read.
  analysis.Advance(Beat(60.0));
  EXPECT_EQ(analysis.seconds(), 3.0);
  EXPECT_EQ(analysis.kills(), 3);
}

// Pressing Stop before the first beat puts the tool away rather than leaving
// it armed.
TEST(BattleAnalysisTest, StopBeforeTheFirstBeatCancels) {
  BattleAnalysis analysis;
  analysis.Start();
  analysis.Stop();
  EXPECT_EQ(analysis.state(), AnalysisState::kStopped);
  analysis.Advance(Beat(1.0));
  EXPECT_EQ(analysis.state(), AnalysisState::kStopped);
  EXPECT_EQ(analysis.seconds(), 0.0);
}

// Starting again clears what the last measurement left.
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

// A caller that stops feeding the tool -- the boss screen -- stops its clock.
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
