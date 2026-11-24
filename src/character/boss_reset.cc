#include "src/character/boss_reset.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

constexpr int64_t kSecondsPerDay = 24 * 60 * 60;

// `now` in local calendar time, with the clock set back to the reset hour. The
// result may be after `now` (before 4am, today's reset hasn't happened yet);
// the caller handles that by stepping back a day.
std::tm ResetHourOfDay(int64_t now) {
  std::time_t when = static_cast<std::time_t>(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &when);
#else
  localtime_r(&when, &local);
#endif
  local.tm_hour = kBossResetHour;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;  // let mktime work out whether the date is in DST
  return local;
}

int64_t ToUnix(std::tm local) {
  return static_cast<int64_t>(std::mktime(&local));
}

}  // namespace

int64_t LastBossReset(int64_t now) {
  int64_t today = ToUnix(ResetHourOfDay(now));
  return today <= now ? today : today - kSecondsPerDay;
}

int64_t NextBossReset(int64_t now) {
  // Computed by adding to the previous reset instead of moving the calendar
  // forward, so a daylight saving change shifts the hour by an hour instead of
  // skipping or repeating a whole reset.
  return LastBossReset(now) + kSecondsPerDay;
}

bool BossAvailable(int64_t cleared, int64_t now) {
  return cleared <= 0 || cleared < LastBossReset(now);
}

namespace {

// Whether `boss` still has the difficulty `clear` was at.
bool HasDifficulty(const Boss& boss, const BossClear& clear) {
  for (const BossDifficulty& difficulty : boss.difficulties()) {
    if (difficulty.name() == clear.difficulty()) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool BossAvailable(const std::string& key, const Boss& boss,
                   const google::protobuf::RepeatedPtrField<BossClear>& clears,
                   int64_t now) {
  for (const BossClear& clear : clears) {
    if (clear.boss() == key && HasDifficulty(boss, clear) &&
        !BossAvailable(clear.cleared_unix_seconds(), now)) {
      return false;
    }
  }
  return true;
}

int64_t BossLockedUntil(
    const std::string& key, const Boss& boss,
    const google::protobuf::RepeatedPtrField<BossClear>& clears) {
  int64_t until = 0;
  for (const BossClear& clear : clears) {
    if (clear.boss() == key && clear.cleared_unix_seconds() > 0 &&
        HasDifficulty(boss, clear)) {
      until = std::max(until, NextBossReset(clear.cleared_unix_seconds()));
    }
  }
  return until;
}

}  // namespace ms
