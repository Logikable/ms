#include "src/frontend/screens/boss_fight_panel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// Wide enough for "Zakum's Arm" with room around it, and the same on both
// sides so the two columns balance.
constexpr int kMobPanelWidth = 20;
constexpr int kMobPanelHeight = 3;
constexpr int kPlayerPanelWidth = 20;
// The gap between the player and the monsters flanking them. Fixed rather
// than flexed: on a wide terminal a filler would push the arms out to the
// edges of the screen, where they read as two separate lists rather than as
// what is standing around the player.
constexpr int kArenaGap = 8;

// A whole percent, rounded down so a bar with anything left never reads 0.
int Percent(double fraction) {
  int pct = static_cast<int>(fraction * 100.0);
  if (pct == 0 && fraction > 0.0) {
    return 1;
  }
  return std::clamp(pct, 0, 100);
}

ftxui::Element MobBar(const BossSlot& slot) {
  ftxui::Element bar =
      ProgressBar(static_cast<float>(slot.hp_fraction), kRed,
                  std::to_string(Percent(slot.hp_fraction)) + "%");
  return ThemedWindow(" " + slot.name + " ", std::move(bar)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kMobPanelWidth);
}

// The room a bar took, once its monster has gone. The slot is not filled
// again, so the bars beside it stay where the player watched them.
ftxui::Element EmptySlot() {
  return ftxui::text("") |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kMobPanelWidth) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kMobPanelHeight);
}

ftxui::Element SlotColumn(const std::vector<BossSlot>& slots, int from,
                          int to) {
  ftxui::Elements column;
  for (int i = from; i < to && i < static_cast<int>(slots.size()); ++i) {
    column.push_back(slots[i].visible ? MobBar(slots[i]) : EmptySlot());
  }
  return ftxui::vbox(std::move(column));
}

// The player's own panel: their name, then whatever they are winding up. No
// HP -- nothing in a boss fight hits back yet.
ftxui::Element PlayerPanel(const BossRun& run) {
  std::string label = run.attack_name();
  if (run.state() == BossRunState::kCountdown) {
    // The count-in stands where the swing name will: it is the one thing on
    // screen that is about to change, so it belongs where the eye already is.
    label = std::to_string(
        static_cast<int>(std::ceil(std::max(0.0, run.countdown_left()))));
  }
  ftxui::Element bar =
      ProgressBar(static_cast<float>(run.attack_fraction()), kTheme, label);
  return ThemedWindow(" You ", std::move(bar)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kPlayerPanelWidth);
}

// The clock, in a box of its own under the heading.
ftxui::Element ClockPanel(const BossRun& run) {
  return ThemedWindow("", CenteredRow(FightClock(run.seconds_left()))) |
         ftxui::center;
}

}  // namespace

std::string FightClock(double seconds_left) {
  int total = static_cast<int>(std::ceil(std::max(0.0, seconds_left)));
  int minutes = total / 60;
  int seconds = total % 60;
  std::string text = std::to_string(minutes) + ":";
  if (seconds < 10) {
    text += "0";
  }
  return text + std::to_string(seconds);
}

std::string FightHeading(const BossRun& run) {
  switch (run.state()) {
    case BossRunState::kWon:
      return run.title() + " - Cleared";
    case BossRunState::kTimedOut:
      return run.title() + " - Out of Time";
    case BossRunState::kAborted:
      return run.title() + " - Left";
    default:
      return run.title() + " - P" + std::to_string(run.phase()) + " - " +
             std::to_string(Percent(run.phase_hp_fraction())) + "%";
  }
}

ftxui::Element BossFightPanel(const BossRun& run) {
  const std::vector<BossSlot>& slots = run.slots();
  ftxui::Elements middle;
  // One monster stands over the player rather than off to one side: Zakum's
  // body is the fight, and the columns are for the arms that come in a crowd.
  if (slots.size() == 1) {
    middle.push_back(slots[0].visible ? MobBar(slots[0]) : EmptySlot());
  }
  middle.push_back(PlayerPanel(run));

  int half = (static_cast<int>(slots.size()) + 1) / 2;
  ftxui::Elements arena;
  ftxui::Element gap = ftxui::text(std::string(kArenaGap, ' '));
  if (slots.size() > 1) {
    arena.push_back(SlotColumn(slots, 0, half));
    arena.push_back(gap);
  }
  // Centred against the columns beside it, so the player stands in the middle
  // of the ring rather than at the top of it.
  arena.push_back(ftxui::vbox(
      {ftxui::filler(), ftxui::vbox(std::move(middle)), ftxui::filler()}));
  if (slots.size() > 1) {
    arena.push_back(gap);
    arena.push_back(SlotColumn(slots, half, static_cast<int>(slots.size())));
  }

  return ftxui::vbox({
      ProgressBar(static_cast<float>(run.phase_hp_fraction()), kRed,
                  FightHeading(run)),
      ClockPanel(run),
      ftxui::filler(),
      ftxui::hbox(std::move(arena)) | ftxui::center,
      ftxui::filler(),
  });
}

}  // namespace ms
