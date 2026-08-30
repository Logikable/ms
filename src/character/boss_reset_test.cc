#include "src/character/boss_reset.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <random>

#include "src/character/character.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// A local time as a Unix time, so a test can say "4am on a Tuesday" without
// caring which zone it is being run in.
int64_t LocalTime(int year, int month, int day, int hour, int minute = 0) {
  std::tm local{};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&local));
}

// 2026-08-20 is a Thursday, so 2026-08-18 is the Tuesday before it.
constexpr int kThursday = 20;
constexpr int kTuesday = 18;

TEST(BossResetTest, TheDailyClockTurnsOverAtFourInTheMorning) {
  // Just before 4am the day's reset has not happened: the last one is
  // yesterday's.
  EXPECT_EQ(
      LastBossReset(RESET_PERIOD_DAILY, LocalTime(2026, 8, kThursday, 3, 59)),
      LocalTime(2026, 8, kThursday - 1, 4));
  EXPECT_EQ(LastBossReset(RESET_PERIOD_DAILY, LocalTime(2026, 8, kThursday, 4)),
            LocalTime(2026, 8, kThursday, 4));
  EXPECT_EQ(
      LastBossReset(RESET_PERIOD_DAILY, LocalTime(2026, 8, kThursday, 23, 59)),
      LocalTime(2026, 8, kThursday, 4));
}

TEST(BossResetTest, TheWeeklyClockTurnsOverOnTuesday) {
  EXPECT_EQ(
      LastBossReset(RESET_PERIOD_WEEKLY, LocalTime(2026, 8, kThursday, 12)),
      LocalTime(2026, 8, kTuesday, 4));
  // Tuesday before 4am still belongs to the week before.
  EXPECT_EQ(
      LastBossReset(RESET_PERIOD_WEEKLY, LocalTime(2026, 8, kTuesday, 3, 59)),
      LocalTime(2026, 8, kTuesday - 7, 4));
  EXPECT_EQ(LastBossReset(RESET_PERIOD_WEEKLY, LocalTime(2026, 8, kTuesday, 4)),
            LocalTime(2026, 8, kTuesday, 4));
}

TEST(BossResetTest, TheNextResetIsOnePeriodOn) {
  int64_t now = LocalTime(2026, 8, kThursday, 12);
  EXPECT_EQ(NextBossReset(RESET_PERIOD_DAILY, now),
            LastBossReset(RESET_PERIOD_DAILY, now) + 24 * 60 * 60);
  EXPECT_EQ(NextBossReset(RESET_PERIOD_WEEKLY, now),
            LastBossReset(RESET_PERIOD_WEEKLY, now) + 7 * 24 * 60 * 60);
  EXPECT_GT(NextBossReset(RESET_PERIOD_DAILY, now), now);
}

TEST(BossResetTest, AClearHoldsUntilTheNextReset) {
  int64_t cleared = LocalTime(2026, 8, kThursday, 12);
  EXPECT_FALSE(BossAvailable(cleared, RESET_PERIOD_DAILY, cleared + 60));
  EXPECT_FALSE(BossAvailable(cleared, RESET_PERIOD_DAILY,
                             LocalTime(2026, 8, kThursday + 1, 3, 59)));
  EXPECT_TRUE(BossAvailable(cleared, RESET_PERIOD_DAILY,
                            LocalTime(2026, 8, kThursday + 1, 4, 1)));
  // The same clear is still spent a week later on a weekly boss.
  EXPECT_FALSE(BossAvailable(cleared, RESET_PERIOD_WEEKLY,
                             LocalTime(2026, 8, kThursday + 1, 4, 1)));
  EXPECT_TRUE(BossAvailable(cleared, RESET_PERIOD_WEEKLY,
                            LocalTime(2026, 8, kTuesday + 7, 4, 1)));
}

TEST(BossResetTest, NeverClearedAndNoPeriodAreAlwaysAvailable) {
  int64_t now = LocalTime(2026, 8, kThursday, 12);
  EXPECT_TRUE(BossAvailable(0, RESET_PERIOD_DAILY, now));
  EXPECT_TRUE(BossAvailable(now, RESET_PERIOD_UNSPECIFIED, now));
}

TEST(BossResetTest, TheCharacterRemembersOneClearPerDifficulty) {
  std::mt19937 rng(1);
  CharacterInstance character(rng, Character());
  EXPECT_EQ(character.BossClearedAt("zakum", "Normal"), 0);

  character.RecordBossClear("zakum", "Normal", 100);
  character.RecordBossClear("zakum", "Chaos", 200);
  EXPECT_EQ(character.BossClearedAt("zakum", "Normal"), 100);
  EXPECT_EQ(character.BossClearedAt("zakum", "Chaos"), 200);

  // A second clear of the same pair replaces the first rather than piling up.
  character.RecordBossClear("zakum", "Normal", 300);
  EXPECT_EQ(character.BossClearedAt("zakum", "Normal"), 300);
  EXPECT_EQ(character.proto().boss_clears_size(), 2);
}

// Two difficulties of one boss, both daily, so a clear of either can be asked
// about the other.
Boss TwoRungBoss() {
  Boss boss;
  boss.set_name("Hilla");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  BossDifficulty* hard = boss.add_difficulties();
  hard->set_name("Hard");
  hard->set_reset(RESET_PERIOD_DAILY);
  return boss;
}

// Beating a boss at any difficulty is beating the boss: the whole ladder waits
// for the reset, and a clear of one rung is what closes the others.
TEST(BossResetTest, AClearOfOneDifficultyHoldsThemAll) {
  Boss hilla = TwoRungBoss();
  int64_t cleared = LocalTime(2026, 8, kThursday, 12);
  int64_t before = LocalTime(2026, 8, kThursday + 1, 3, 59);
  int64_t after = LocalTime(2026, 8, kThursday + 1, 4, 1);
  std::mt19937 rng(1);
  CharacterInstance character(rng, Character());
  EXPECT_TRUE(
      BossAvailable("hilla", hilla, character.proto().boss_clears(), cleared));

  character.RecordBossClear("hilla", "Hard", cleared);
  EXPECT_FALSE(
      BossAvailable("hilla", hilla, character.proto().boss_clears(), before));
  EXPECT_TRUE(
      BossAvailable("hilla", hilla, character.proto().boss_clears(), after));

  // Another boss's clear, and a difficulty this boss does not have, are both
  // somebody else's business.
  character.RecordBossClear("zakum", "Normal", cleared);
  character.RecordBossClear("hilla", "Chaos", cleared);
  EXPECT_TRUE(
      BossAvailable("hilla", hilla, character.proto().boss_clears(), after));
}

}  // namespace
}  // namespace ms
