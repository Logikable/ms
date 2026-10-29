/* Achievements: what an account has done that is worth keeping a date for.
 *
 * Each one is either a gamble that paid (a 25-star item, a perfect flame) or
 * something only a very long wait brings (level 300, three years). Anything a
 * few weeks of play hands everyone isn't one.
 *
 * Nothing records progress towards them. Each is read off the account as it
 * stands, so a save from before achievements earns everything it already
 * shows the first time it is checked. The exception is meso, which is only
 * what is held at the moment of a check.
 */
#ifndef MS_SRC_ACHIEVEMENT_H_
#define MS_SRC_ACHIEVEMENT_H_

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "absl/types/span.h"
#include "src/game_state.h"
#include "src/protos/achievement.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// One achievement as the tab shows it.
struct AchievementInfo {
  Achievement achievement;
  const char* name;
  const char* description;
  // What its counter counts, or null for one that has no counter: either it
  // has happened or it hasn't.
  const char* unit;
};

// Every achievement, in the order the tab lists them.
absl::Span<const AchievementInfo> AllAchievements();
const AchievementInfo& InfoFor(Achievement achievement);

// Whether the achievement is a meso tier, whose counter is the purse.
bool IsMesoAchievement(Achievement achievement);

// Whether the achievement counts towards a target.
bool HasCounter(Achievement achievement);

// Each counter's target. Some are read off the catalogs (every familiar, every
// symbol, every Noblesse level), so they need the state.
std::map<Achievement, int64_t> AchievementTargets(const GameState& state);

// Where the account stands: what it meets, and every counter's value.
struct AchievementStanding {
  std::set<Achievement> met;
  std::map<Achievement, int64_t> progress;
};
AchievementStanding StandingOf(const GameState& state,
                               int64_t now_unix_seconds);

// Whether `potential` is Legendary with three Legendary lines of one effect,
// any sizes of it. A main and a bonus potential are each judged alone, and
// All Stat% is its own effect, never a part of another.
bool IsTriplePrime(const Potential& potential);

// Whether all four of `equip`'s flame lines are tier 7 and useful to one job:
// its main stat, a pair with it, All Stat%, Boss%, Damage%, and ATT, or MATT
// for a mage.
bool IsPerfectFlame(const Equip& equip);

// The achievements the account meets as it stands at `now_unix_seconds`,
// earned or not.
std::vector<Achievement> MetAchievements(const GameState& state,
                                         int64_t now_unix_seconds);

// Records every achievement met and not yet earned as earned at
// `now_unix_seconds`, and returns those, in the tab's order. Leaves the
// counters in GameState::achievement_progress.
std::vector<Achievement> CheckAchievements(GameState& state,
                                           int64_t now_unix_seconds);

}  // namespace ms

#endif  // MS_SRC_ACHIEVEMENT_H_
