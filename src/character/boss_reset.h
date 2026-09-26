/* When a boss can be fought again.
 *
 * A clear is recorded as the time it happened, and the reset is at 4:00 in the
 * morning, local time: a daily boss resets at the next 4am, a weekly one at the
 * next 4am on a Tuesday. Local time is used so a player's reset happens in
 * their own early morning.
 *
 * Local time means the player's clock, which the player can change. The
 * multiplayer server does not check clears, since its clock would disagree
 * with a player's about when the day turned over.
 */
#ifndef MS_SRC_CHARACTER_BOSS_RESET_H_
#define MS_SRC_CHARACTER_BOSS_RESET_H_

#include <cstdint>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {

// The reset hour, and the weekday weekly bosses reset on (0 is Sunday, so 2 is
// Tuesday), both as std::tm uses them.
inline constexpr int kBossResetHour = 4;
inline constexpr int kBossResetWeekday = 2;

// The most recent reset of `period` at or before `now`, as a Unix time. A clear
// recorded before it has expired; one recorded after it still counts.
int64_t LastBossReset(ResetPeriod period, int64_t now);

// The next reset of `period` after `now`, for a screen counting down to it.
int64_t NextBossReset(ResetPeriod period, int64_t now);

// Whether a boss last cleared at `cleared` can be fought again at `now`. Never
// cleared (0) is always available, and so is a period the data doesn't set,
// since a boss with no reset has nothing to hold it back.
bool BossAvailable(int64_t cleared, ResetPeriod period, int64_t now);

// Whether `boss` can be entered at `now`. A clear of one difficulty blocks all
// of them (the reset applies to the boss, not the difficulty), and each clear
// is checked against the reset period of the difficulty it was at.
bool BossAvailable(const std::string& key, const Boss& boss,
                   const google::protobuf::RepeatedPtrField<BossClear>& clears,
                   int64_t now);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_BOSS_RESET_H_
