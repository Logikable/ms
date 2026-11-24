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

// 2026-08-20, a Thursday.
constexpr int kThursday = 20;

TEST(BossResetTest, TheClockTurnsOverAtFourInTheMorning) {
  // Just before 4am the day's reset has not happened: the last one is
  // yesterday's.
  EXPECT_EQ(LastBossReset(LocalTime(2026, 8, kThursday, 3, 59)),
            LocalTime(2026, 8, kThursday - 1, 4));
  EXPECT_EQ(LastBossReset(LocalTime(2026, 8, kThursday, 4)),
            LocalTime(2026, 8, kThursday, 4));
  EXPECT_EQ(LastBossReset(LocalTime(2026, 8, kThursday, 23, 59)),
            LocalTime(2026, 8, kThursday, 4));
}

TEST(BossResetTest, TheNextResetIsADayOn) {
  int64_t now = LocalTime(2026, 8, kThursday, 12);
  EXPECT_EQ(NextBossReset(now), LastBossReset(now) + 24 * 60 * 60);
  EXPECT_GT(NextBossReset(now), now);
}

TEST(BossResetTest, AClearHoldsUntilTheNextReset) {
  int64_t cleared = LocalTime(2026, 8, kThursday, 12);
  EXPECT_FALSE(BossAvailable(cleared, cleared + 60));
  EXPECT_FALSE(
      BossAvailable(cleared, LocalTime(2026, 8, kThursday + 1, 3, 59)));
  EXPECT_TRUE(BossAvailable(cleared, LocalTime(2026, 8, kThursday + 1, 4, 1)));
  EXPECT_TRUE(BossAvailable(0, cleared)) << "never cleared";
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

// Two difficulties of one boss, so a clear of either can be asked about the
// other.
Boss TwoRungBoss() {
  Boss boss;
  boss.set_name("Hilla");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  BossDifficulty* hard = boss.add_difficulties();
  hard->set_name("Hard");
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

// What a game tells the server: the instant the lock ends, the later reset
// when two clears hold it, and nothing for a boss it never cleared.
TEST(BossResetTest, TheLockEndsAtTheNextResetAfterTheLatestClear) {
  Boss hilla = TwoRungBoss();
  std::mt19937 rng(1);
  CharacterInstance character(rng, Character());
  EXPECT_EQ(BossLockedUntil("hilla", hilla, character.proto().boss_clears()),
            0);

  int64_t cleared = LocalTime(2026, 8, kThursday, 12);
  character.RecordBossClear("hilla", "Normal", cleared);
  character.RecordBossClear("zakum", "Normal", cleared);
  EXPECT_EQ(BossLockedUntil("hilla", hilla, character.proto().boss_clears()),
            LocalTime(2026, 8, kThursday + 1, 4));

  character.RecordBossClear("hilla", "Hard",
                            LocalTime(2026, 8, kThursday + 1, 12));
  EXPECT_EQ(BossLockedUntil("hilla", hilla, character.proto().boss_clears()),
            LocalTime(2026, 8, kThursday + 2, 4));
}

}  // namespace
}  // namespace ms
