/* AchievementsPanel is the Achievements screen: a list of every achievement
 * beside the card of the one under the cursor, opened from Characters for the
 * player's own account and from the Players list for someone else's.
 *
 * The list puts what has been earned first, newest first, then the rest in the
 * catalog's order, greyed. The card has the name, when it was earned, the
 * description and, for an achievement with a counter, a bar: gold and full once
 * earned, filling otherwise. There is room for both side by side at the
 * smallest screen the game supports, so there is no Enter to open the card.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_ACHIEVEMENTS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_ACHIEVEMENTS_PANEL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/achievement.pb.h"

namespace ms {

// The list and the card, borders included. Together with the gap between them
// they fit the 120 columns the game supports with room to spare.
inline constexpr int kAchievementListWidth = 44;
inline constexpr int kAchievementCardWidth = 48;

// One row of the list.
struct AchievementEntry {
  Achievement achievement = ACHIEVEMENT_UNSPECIFIED;
  // When it was earned, in seconds since the Unix epoch, or 0 if it wasn't.
  int64_t earned_unix_seconds = 0;
  // The counter's value, or nothing for an achievement without one and for a
  // counter the owner keeps to themselves (someone else's purse).
  std::optional<int64_t> current;
  // The counter's target, or 0 for an achievement without one.
  int64_t target = 0;
};

// Every achievement as the list orders it: earned ones newest first, ties in
// the catalog's order, then the rest in the catalog's order. `progress` and
// `targets` are keyed as AchievementTargets keys them; one missing from
// `progress` has no value to show.
std::vector<AchievementEntry> AchievementEntries(
    const google::protobuf::RepeatedPtrField<EarnedAchievement>& earned,
    const std::map<Achievement, int64_t>& progress,
    const std::map<Achievement, int64_t>& targets);

// A Unix time as the viewer's own calendar shows it: "2026-10-28", and with
// the time, "2026-10-28 17:52".
std::string LocalDate(int64_t unix_seconds);
std::string LocalDateTime(int64_t unix_seconds);

// The notification for what one check earned, in two lines: "Achievement
// Unlocked:" and the name. Several at once (an old save's first check) are
// one notice naming the first, since a new notice replaces the last.
std::vector<std::string> AchievementNotice(
    const std::vector<Achievement>& earned);

class AchievementsPanel {
 public:
  enum class Action { kNone, kClose };

  // Opens at the top of the list, under `title`.
  void Reset(const std::string& title);
  // The rows, every frame, since the counters move while the screen is open.
  void SetEntries(std::vector<AchievementEntry> entries);
  // The most rows the list may take, borders included. Zero means no limit.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  // The row under the cursor, or null with no rows.
  const AchievementEntry* selected() const;

  ftxui::Element RenderList() const;
  ftxui::Element RenderCard() const;
  Action OnEvent(const ftxui::Event& event);

 private:
  std::string title_;
  std::vector<AchievementEntry> entries_;
  int cursor_ = 0;
  int max_rows_ = 0;
  // Which achievement the cursor is on, so a reorder (one earned while the
  // screen is open) keeps the cursor on it rather than on its old row.
  Achievement on_ = ACHIEVEMENT_UNSPECIFIED;
  mutable SelectionClock name_clock_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ACHIEVEMENTS_PANEL_H_
