#include "src/character/boss_reset.h"

#include <cstdint>
#include <ctime>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

constexpr int64_t kSecondsPerDay = 24 * 60 * 60;

// `now` as local calendar time with the clock wound back to the reset hour.
// That may land after `now` -- before 4am the day's reset has not happened yet
// -- which the caller settles by stepping back a day.
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

int64_t LastBossReset(ResetPeriod period, int64_t now) {
  std::tm local = ResetHourOfDay(now);
  int64_t today = ToUnix(local);
  if (period == RESET_PERIOD_WEEKLY) {
    // Back to the reset weekday, then back another week if that lands after
    // `now` -- which it does on reset day itself before 4am.
    int days_since = (local.tm_wday - kBossResetWeekday + 7) % 7;
    int64_t at = today - days_since * kSecondsPerDay;
    return at <= now ? at : at - 7 * kSecondsPerDay;
  }
  return today <= now ? today : today - kSecondsPerDay;
}

int64_t NextBossReset(ResetPeriod period, int64_t now) {
  int64_t last = LastBossReset(period, now);
  int64_t span =
      period == RESET_PERIOD_WEEKLY ? 7 * kSecondsPerDay : kSecondsPerDay;
  // Built by adding to the previous reset rather than by winding the calendar
  // forward, so a daylight-saving change moves the hour by an hour rather than
  // dropping or repeating a whole reset.
  return last + span;
}

bool BossAvailable(int64_t cleared, ResetPeriod period, int64_t now) {
  if (cleared <= 0 || period == RESET_PERIOD_UNSPECIFIED) {
    return true;
  }
  return cleared < LastBossReset(period, now);
}

bool BossAvailable(const std::string& key, const Boss& boss,
                   const google::protobuf::RepeatedPtrField<BossClear>& clears,
                   int64_t now) {
  for (const BossClear& clear : clears) {
    if (clear.boss() != key) {
      continue;
    }
    for (const BossDifficulty& difficulty : boss.difficulties()) {
      if (difficulty.name() == clear.difficulty() &&
          !BossAvailable(clear.cleared_unix_seconds(), difficulty.reset(),
                         now)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace ms
