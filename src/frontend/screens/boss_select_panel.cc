#include "src/frontend/screens/boss_select_panel.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/boss_reset.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

constexpr int kBossNameWidth = 16;
constexpr int kDifficultyWidth = 10;
// The detail rows are a label and a right-aligned value, so the panel does not
// breathe as a number gains a digit.
constexpr int kLabelWidth = 12;
constexpr int kValueWidth = 14;
constexpr int kDetailWidth = kLabelWidth + kValueWidth + 1;

std::string ResetName(ResetPeriod period) {
  switch (period) {
    case RESET_PERIOD_DAILY:
      return "Daily";
    case RESET_PERIOD_WEEKLY:
      return "Weekly";
    default:
      return "-";
  }
}

// mm:ss, which is what a five-minute clock wants and what the fight screen
// counts down in.
std::string Clock(int seconds) {
  int minutes = seconds / 60;
  int rest = seconds % 60;
  std::string text = std::to_string(minutes) + ":";
  if (rest < 10) {
    text += "0";
  }
  return text + std::to_string(rest);
}

ftxui::Element DetailRow(const std::string& label, const std::string& value) {
  return ftxui::text(" " + PadRight(label, kLabelWidth) +
                     PadLeft(value, kValueWidth));
}

}  // namespace

int64_t PhaseHp(const GameState& state, const BossPhase& phase) {
  int64_t hp = 0;
  for (const Spawn& spawn : phase.spawns()) {
    std::map<std::string, Mob>::const_iterator it =
        state.mobs.find(spawn.mob());
    if (it != state.mobs.end()) {
      hp += static_cast<int64_t>(spawn.count()) * it->second.max_hp();
    }
  }
  return hp;
}

int BossLevel(const GameState& state, const BossDifficulty& difficulty) {
  int level = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    for (const Spawn& spawn : phase.spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          state.mobs.find(spawn.mob());
      if (it != state.mobs.end()) {
        level = std::max(level, it->second.level());
      }
    }
  }
  return level;
}

namespace {

// The stiffest defence anything in the fight stands behind. One number for
// what an Ignore DEF lever is worth here, which is what the player is reading
// the row for.
int BossPdr(const GameState& state, const BossDifficulty& difficulty) {
  int pdr = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    for (const Spawn& spawn : phase.spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          state.mobs.find(spawn.mob());
      if (it != state.mobs.end()) {
        pdr = std::max(pdr, it->second.pdr());
      }
    }
  }
  return pdr;
}

}  // namespace

BossSelectPanel::BossSelectPanel(const GameState& state) : state_(state) {
  std::vector<std::pair<std::pair<int, std::string>, std::string>> sorted;
  for (const std::pair<const std::string, Boss>& entry : state_.bosses) {
    int level = entry.second.difficulties_size() > 0
                    ? BossLevel(state_, entry.second.difficulties(0))
                    : 0;
    sorted.push_back({{level, entry.second.name()}, entry.first});
  }
  std::sort(sorted.begin(), sorted.end());
  for (const std::pair<std::pair<int, std::string>, std::string>& entry :
       sorted) {
    bosses_.push_back(entry.second);
  }
  difficulties_.assign(bosses_.size(), 0);
}

void BossSelectPanel::Reset() {
  selected_ = 0;
}

void BossSelectPanel::MoveCursor(int delta) {
  selected_ = StepCursor(selected_, delta, static_cast<int>(bosses_.size()));
}

void BossSelectPanel::ChangeDifficulty(int delta) {
  if (bosses_.empty()) {
    return;
  }
  int count = state_.bosses.at(bosses_[selected_]).difficulties_size();
  difficulties_[selected_] =
      std::clamp(difficulties_[selected_] + delta, 0, std::max(0, count - 1));
}

const std::string& BossSelectPanel::selected_boss() const {
  static const std::string kNone;
  return bosses_.empty() ? kNone : bosses_[selected_];
}

int BossSelectPanel::selected_difficulty() const {
  return bosses_.empty() ? 0 : difficulties_[selected_];
}

const BossDifficulty* BossSelectPanel::selected() const {
  if (bosses_.empty()) {
    return nullptr;
  }
  const Boss& boss = state_.bosses.at(bosses_[selected_]);
  int at = difficulties_[selected_];
  if (at < 0 || at >= boss.difficulties_size()) {
    return nullptr;
  }
  return &boss.difficulties(at);
}

std::string BossSelectPanel::selected_title() const {
  const BossDifficulty* difficulty = selected();
  if (difficulty == nullptr) {
    return "";
  }
  return difficulty->name() + " " + state_.bosses.at(bosses_[selected_]).name();
}

bool BossSelectPanel::selected_available() const {
  const BossDifficulty* difficulty = selected();
  if (difficulty == nullptr) {
    return false;
  }
  int64_t cleared =
      state_.character.BossClearedAt(bosses_[selected_], difficulty->name());
  return BossAvailable(cleared, difficulty->reset(),
                       static_cast<int64_t>(std::time(nullptr)));
}

ftxui::Element BossSelectPanel::RenderBossList() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" " + PadRight("Name", kBossNameWidth) +
                             PadRight("Difficulty", kDifficultyWidth)));
  rows.push_back(ThemedSeparator());
  if (bosses_.empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < static_cast<int>(bosses_.size()); ++i) {
    const Boss& boss = state_.bosses.at(bosses_[i]);
    std::string difficulty;
    if (difficulties_[i] < boss.difficulties_size()) {
      difficulty = boss.difficulties(difficulties_[i]).name();
    }
    std::string row = i == selected_ ? ">" : " ";
    row += PadRight(boss.name(), kBossNameWidth) +
           PadRight(difficulty, kDifficultyWidth);
    rows.push_back(ftxui::text(row));
  }
  return ThemedWindow(" Bosses ", ftxui::vbox(std::move(rows)));
}

ftxui::Element BossSelectPanel::RenderDetail() const {
  const BossDifficulty* difficulty = selected();
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(PadRight(" " + selected_title(), kDetailWidth)) |
                 ftxui::color(kTheme));
  rows.push_back(ThemedSeparator());
  if (difficulty == nullptr) {
    rows.push_back(EmptyState("empty"));
    return ThemedWindow(" Fight ", ftxui::vbox(std::move(rows)));
  }
  rows.push_back(
      DetailRow("Level", std::to_string(BossLevel(state_, *difficulty))));
  for (int i = 0; i < difficulty->phases_size(); ++i) {
    rows.push_back(
        DetailRow("Phase " + std::to_string(i + 1),
                  FormatWithCommas(PhaseHp(state_, difficulty->phases(i)))));
  }
  rows.push_back(
      DetailRow("PDR", std::to_string(BossPdr(state_, *difficulty)) + "%"));
  rows.push_back(
      DetailRow("Time Limit", Clock(difficulty->time_limit_seconds())));
  rows.push_back(DetailRow("Reset", ResetName(difficulty->reset())));
  if (!selected_available()) {
    // Red is the reason: the one value the player falls short of. What they
    // are short of here is a reset, so it goes on the status and nowhere else.
    rows.push_back(DetailRow("Status", "Cleared") | ftxui::color(kRed));
  } else {
    rows.push_back(DetailRow("Status", "Available"));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(ftxui::text(" Drops") | ftxui::color(kTheme));
  if (difficulty->drops().empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (const MobDrop& drop : difficulty->drops()) {
    std::map<std::string, ItemPrototype>::const_iterator it =
        state_.items.find(drop.item());
    if (it != state_.items.end()) {
      rows.push_back(ftxui::text(" " + it->second.name()));
    }
  }
  return ThemedWindow(" Fight ", ftxui::vbox(std::move(rows)));
}

ftxui::Element BossSelectPanel::Render() const {
  return ftxui::hbox({RenderBossList(), RenderDetail()});
}

}  // namespace ms
