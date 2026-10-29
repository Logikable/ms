#include "src/frontend/screens/achievements_panel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/achievement.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/achievement.pb.h"

namespace ms {
namespace {

// Borders, the header and its rule: everything but the rows.
constexpr int kChromeRows = 4;
constexpr int kDateWidth = 10;
// Borders, the cursor, the gap before the date, the date and a blank column
// before the right border.
constexpr int kNameWidth = kAchievementListWidth - 2 - 2 - 1 - kDateWidth - 1;

std::tm Local(int64_t unix_seconds) {
  std::time_t when = static_cast<std::time_t>(unix_seconds);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &when);
#else
  localtime_r(&when, &local);
#endif
  return local;
}

std::string Format(int64_t unix_seconds, const char* format) {
  std::tm local = Local(unix_seconds);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), format, &local);
  return buffer;
}

// " 3 / 25 stars", with the value raised to the target once it is earned: the
// stars may be gone since, but the achievement isn't.
std::string CounterText(const AchievementEntry& entry) {
  const bool earned = entry.earned_unix_seconds != 0;
  if (!earned && !entry.current.has_value()) {
    return "Hidden";
  }
  int64_t shown = entry.current.value_or(entry.target);
  if (earned) {
    shown = std::max(shown, entry.target);
  }
  return FormatWithCommas(shown) + " / " + FormatWithCommas(entry.target) +
         " " + InfoFor(entry.achievement).unit;
}

float CounterFraction(const AchievementEntry& entry) {
  if (entry.earned_unix_seconds != 0) {
    return 1.0f;
  }
  if (!entry.current.has_value() || entry.target <= 0) {
    return 0.0f;
  }
  return static_cast<float>(std::clamp(
      static_cast<double>(*entry.current) / static_cast<double>(entry.target),
      0.0, 1.0));
}

}  // namespace

std::vector<AchievementEntry> AchievementEntries(
    const google::protobuf::RepeatedPtrField<EarnedAchievement>& earned,
    const std::map<Achievement, int64_t>& progress,
    const std::map<Achievement, int64_t>& targets) {
  std::map<Achievement, int64_t> when;
  for (const EarnedAchievement& one : earned) {
    when[one.achievement()] = one.earned_unix_seconds();
  }
  std::vector<AchievementEntry> entries;
  for (const AchievementInfo& info : AllAchievements()) {
    AchievementEntry entry;
    entry.achievement = info.achievement;
    auto found = when.find(info.achievement);
    if (found != when.end()) {
      entry.earned_unix_seconds = found->second;
    }
    if (HasCounter(info.achievement)) {
      auto value = progress.find(info.achievement);
      if (value != progress.end()) {
        entry.current = value->second;
      }
      auto target = targets.find(info.achievement);
      if (target != targets.end()) {
        entry.target = target->second;
      }
    }
    entries.push_back(std::move(entry));
  }
  // Stable, so ties (all of an old save's, earned on its first check) keep
  // the catalog's order.
  std::stable_sort(entries.begin(), entries.end(),
                   [](const AchievementEntry& a, const AchievementEntry& b) {
                     const bool a_earned = a.earned_unix_seconds != 0;
                     const bool b_earned = b.earned_unix_seconds != 0;
                     if (a_earned != b_earned) {
                       return a_earned;
                     }
                     return a.earned_unix_seconds > b.earned_unix_seconds;
                   });
  return entries;
}

std::string LocalDate(int64_t unix_seconds) {
  return Format(unix_seconds, "%Y-%m-%d");
}

std::string LocalDateTime(int64_t unix_seconds) {
  return Format(unix_seconds, "%Y-%m-%d %H:%M");
}

void AchievementsPanel::Reset(const std::string& title) {
  title_ = title;
  cursor_ = 0;
  on_ = ACHIEVEMENT_UNSPECIFIED;
}

void AchievementsPanel::SetEntries(std::vector<AchievementEntry> entries) {
  entries_ = std::move(entries);
  if (on_ != ACHIEVEMENT_UNSPECIFIED) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].achievement == on_) {
        cursor_ = i;
      }
    }
  }
  cursor_ = std::clamp(cursor_, 0,
                       std::max(0, static_cast<int>(entries_.size()) - 1));
  if (!entries_.empty()) {
    on_ = entries_[cursor_].achievement;
  }
}

const AchievementEntry* AchievementsPanel::selected() const {
  if (entries_.empty()) {
    return nullptr;
  }
  return &entries_[cursor_];
}

ftxui::Element AchievementsPanel::RenderList() const {
  name_clock_.Follow(cursor_);
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
    const AchievementEntry& entry = entries_[i];
    const bool on_cursor = i == cursor_;
    const bool earned = entry.earned_unix_seconds != 0;
    const std::string name = ScrollingWindow(
        InfoFor(entry.achievement).name, kNameWidth,
        on_cursor ? name_clock_.Elapsed()
                  : std::chrono::steady_clock::duration::zero());
    const std::string date =
        earned ? LocalDate(entry.earned_unix_seconds) : std::string();
    ftxui::Element row =
        ftxui::text(std::string(on_cursor ? "> " : "  ") + name + " " +
                    PadRight(date, kDateWidth) + " ");
    if (!earned) {
      row = std::move(row) | ftxui::color(kGray);
    }
    row = HighlightRow(std::move(row), on_cursor);
    rows.push_back(on_cursor ? std::move(row) | ftxui::focus : std::move(row));
  }
  ftxui::Element list =
      ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator | ftxui::yframe;
  if (max_rows_ > 0) {
    list = std::move(list) | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN,
                                         std::max(1, max_rows_ - kChromeRows));
  }
  const std::string header = "  " + PadRight("Achievement", kNameWidth) + " " +
                             PadRight("Earned", kDateWidth) + " ";
  return ThemedWindow(title_,
                      ftxui::vbox({
                          ftxui::text(header),
                          ThemedSeparator(),
                          std::move(list) | ftxui::flex_shrink,
                      }),
                      /*focused=*/true) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kAchievementListWidth);
}

ftxui::Element AchievementsPanel::RenderCard() const {
  const AchievementEntry* entry = selected();
  if (entry == nullptr) {
    return ThemedWindow(" Inspect ", EmptyState("no achievement"), false) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kAchievementCardWidth);
  }
  const AchievementInfo& info = InfoFor(entry->achievement);
  const bool earned = entry->earned_unix_seconds != 0;
  std::vector<ftxui::Element> rows = {
      CenteredRow(ftxui::text(info.name) | ftxui::bold),
      earned
          ? CenteredRow(ftxui::text(LocalDateTime(entry->earned_unix_seconds)) |
                        ftxui::color(kGray))
          : ftxui::text(""),
      ThemedSeparator(),
  };
  rows.push_back(ftxui::hbox({
      ftxui::text(" "),
      ftxui::paragraph(info.description) | ftxui::flex,
      ftxui::text(" "),
  }));
  if (HasCounter(entry->achievement)) {
    rows.push_back(ftxui::text(""));
    rows.push_back(ftxui::hbox({
        ftxui::text(" "),
        ProgressBar(CounterFraction(*entry),
                    earned ? kGold : ftxui::Color::GrayLight, "") |
            ftxui::flex,
        ftxui::text(" "),
    }));
    rows.push_back(CenteredRow(CounterText(*entry)));
  }
  return ThemedWindow(" Inspect ", ftxui::vbox(std::move(rows)), false) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kAchievementCardWidth);
}

AchievementsPanel::Action AchievementsPanel::OnEvent(
    const ftxui::Event& event) {
  if (IsBack(event)) {
    return Action::kClose;
  }
  if ((event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) &&
      !entries_.empty()) {
    cursor_ = StepCursor(cursor_, event == ftxui::Event::ArrowUp ? -1 : 1,
                         static_cast<int>(entries_.size()));
    on_ = entries_[cursor_].achievement;
  }
  return Action::kNone;
}

}  // namespace ms
