/* When a boss can be fought again.
 *
 * A clear is banked as the moment it happened, and the clock turns over at
 * 4:00 in the morning, local time: a daily boss comes back at the next 4am, a
 * weekly one at the next 4am on a Tuesday. Local rather than fixed, so a
 * player's reset lands in their own small hours.
 *
 * Local time means the player's clock, which the player can set. That is
 * deliberate for now -- there is nobody to be unfair to in a single-player
 * game. Once the game talks to anything (multiplayer, a shared ladder), the
 * moment has to come off the network instead, or a boss is daily only for
 * whoever leaves their clock alone.
 */
#ifndef MS_SRC_CHARACTER_BOSS_RESET_H_
#define MS_SRC_CHARACTER_BOSS_RESET_H_

#include <cstdint>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {

// The hour the clock turns over at, and the weekday a weekly boss turns over
// on (0 is Sunday, so 2 is Tuesday) -- both as std::tm reads them.
inline constexpr int kBossResetHour = 4;
inline constexpr int kBossResetWeekday = 2;

// The most recent reset of `period` at or before `now`, as a Unix time. A
// clear banked before it has expired; one banked after it still stands.
int64_t LastBossReset(ResetPeriod period, int64_t now);

// The next reset of `period` after `now`, for a screen counting down to it.
int64_t NextBossReset(ResetPeriod period, int64_t now);

// Whether a boss last cleared at `cleared` may be fought again at `now`.
// Never cleared (0) is always available, and so is a period the data does not
// state -- a boss with no reset is one there is nothing to hold back.
bool BossAvailable(int64_t cleared, ResetPeriod period, int64_t now);

// Whether `boss` -- the fight named `key` in the catalog -- may be entered at
// all at `now`, given everything `clears` holds. A clear of one difficulty
// holds every other one back: what the reset gates is the boss, not the rung
// the player chose to take him at. Each clear is measured against the reset
// period of the difficulty it was taken at, since that is the clock that ran.
bool BossAvailable(const std::string& key, const Boss& boss,
                   const google::protobuf::RepeatedPtrField<BossClear>& clears,
                   int64_t now);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_BOSS_RESET_H_
