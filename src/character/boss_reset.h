/* When a boss can be fought again.
 *
 * A clear is recorded as the time it happened, and the reset is at 4:00 in the
 * morning, local time: every boss is daily, and resets at the next 4am. Local
 * time is used so a player's reset happens in their own early morning.
 *
 * Local time means the player's clock, which the player can change. The
 * multiplayer server's clock would disagree about when the day turned over, so
 * each game tells it the instant its own lock ends (BossLockedUntil).
 */
#ifndef MS_SRC_CHARACTER_BOSS_RESET_H_
#define MS_SRC_CHARACTER_BOSS_RESET_H_

#include <cstdint>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {

// The reset hour, as std::tm uses it.
inline constexpr int kBossResetHour = 4;

// The most recent reset at or before `now`, as a Unix time. A clear recorded
// before it has expired; one recorded after it still counts.
int64_t LastBossReset(int64_t now);

// The next reset after `now`, for a screen counting down to it.
int64_t NextBossReset(int64_t now);

// Whether a boss last cleared at `cleared` can be fought again at `now`. Never
// cleared (0) is always available.
bool BossAvailable(int64_t cleared, int64_t now);

// Whether `boss` can be entered at `now`. A clear of one difficulty blocks all
// of them (the reset applies to the boss, not the difficulty); a clear of a
// difficulty the boss no longer has holds nothing.
bool BossAvailable(const std::string& key, const Boss& boss,
                   const google::protobuf::RepeatedPtrField<BossClear>& clears,
                   int64_t now);

// The instant `boss` opens again, on this machine's clock: the latest reset
// after any of its clears, or 0 if none holds it. An instant, not a date, so
// a server in another zone can compare it to its own time.
int64_t BossLockedUntil(
    const std::string& key, const Boss& boss,
    const google::protobuf::RepeatedPtrField<BossClear>& clears);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_BOSS_RESET_H_
