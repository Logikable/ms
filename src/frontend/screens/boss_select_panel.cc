#include "src/frontend/screens/boss_select_panel.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/boss_reset.h"
#include "src/character/honor.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

constexpr int kBossNameWidth = 16;
constexpr int kDifficultyWidth = 10;
// The detail rows are a label and a right-aligned value, so the panel does not
// breathe as a number gains a digit.
constexpr int kLabelWidth = 12;
constexpr int kValueWidth = 14;
// What the rest of a wrapped name is set in from its first line, so the two
// rows read as one name.
constexpr int kNameIndent = 2;
// A column of clearance on each side, the way every panel here is padded.
constexpr int kDetailWidth = kLabelWidth + kValueWidth + 2;
// The rows the screen always takes, whichever fight the cursor is on. Tall
// enough for the longest detail panel in the game -- a test holds it there --
// so the top of the screen never moves.
constexpr int kScreenHeight = 24;

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
                     PadLeft(value, kValueWidth) + " ");
}

}  // namespace

int64_t PhaseHp(const GameState& state, const BossPhase& phase) {
  int64_t hp = 0;
  for (const Spawn& spawn : phase.spawns()) {
    std::map<std::string, Mob>::const_iterator it =
        state.mobs.find(spawn.mob());
    if (it != state.mobs.end()) {
      hp += static_cast<int64_t>(SpawnCount(spawn)) * it->second.max_hp();
    }
  }
  return hp;
}

int64_t BossHp(const GameState& state, const BossDifficulty& difficulty) {
  int64_t hp = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    hp += PhaseHp(state, phase);
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
  // The level the fight opens at, on the easiest difficulty -- the one the
  // cursor starts on and the one a player meets the fight through. The gate is
  // what says where a fight sits in a character's life; HP only stands in for
  // it, and stands in badly where one body holds what another spreads over six
  // parts. A fight with no gate stated is not built yet and sorts last, on the
  // HP that is the only thing it does state.
  std::vector<std::tuple<int, int64_t, std::string, std::string>> sorted;
  for (const std::pair<const std::string, Boss>& entry : state_.bosses) {
    int64_t hp = 0;
    int unlock = std::numeric_limits<int>::max();
    if (entry.second.difficulties_size() > 0) {
      hp = BossHp(state_, entry.second.difficulties(0));
      int gate = entry.second.difficulties(0).unlock_level();
      unlock = gate > 0 ? gate : unlock;
    }
    sorted.push_back({unlock, hp, entry.second.name(), entry.first});
  }
  std::sort(sorted.begin(), sorted.end());
  for (const std::tuple<int, int64_t, std::string, std::string>& entry :
       sorted) {
    bosses_.push_back(std::get<3>(entry));
  }
}

void BossSelectPanel::Reset() {
  selected_ = 0;
  column_ = 0;
}

void BossSelectPanel::MoveCursor(int delta) {
  selected_ = StepCursor(selected_, delta, static_cast<int>(bosses_.size()));
}

int BossSelectPanel::Columns() const {
  int columns = 0;
  for (const std::string& key : bosses_) {
    columns = std::max(columns, state_.bosses.at(key).difficulties_size());
  }
  return columns;
}

void BossSelectPanel::ChangeDifficulty(int delta) {
  column_ = std::clamp(column_ + delta, 0, std::max(0, Columns() - 1));
}

int BossSelectPanel::DifficultyAt(int boss) const {
  int count = state_.bosses.at(bosses_[boss]).difficulties_size();
  return std::clamp(column_, 0, std::max(0, count - 1));
}

const std::string& BossSelectPanel::selected_boss() const {
  static const std::string kNone;
  return bosses_.empty() ? kNone : bosses_[selected_];
}

int BossSelectPanel::selected_difficulty() const {
  return bosses_.empty() ? 0 : DifficultyAt(selected_);
}

const BossDifficulty* BossSelectPanel::selected() const {
  if (bosses_.empty()) {
    return nullptr;
  }
  const Boss& boss = state_.bosses.at(bosses_[selected_]);
  int at = DifficultyAt(selected_);
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
  if (selected() == nullptr) {
    return false;
  }
  // Asked of the boss rather than of the rung: beating him at any difficulty
  // is beating him, and the whole ladder waits for the reset.
  const std::string& key = bosses_[selected_];
  return BossAvailable(key, state_.bosses.at(key),
                       state_.character.proto().boss_clears(),
                       static_cast<int64_t>(std::time(nullptr)));
}

bool BossSelectPanel::Unlocked(const BossDifficulty& difficulty) const {
  return state_.character.proto().level() >= difficulty.unlock_level();
}

bool BossSelectPanel::selected_unlocked() const {
  const BossDifficulty* difficulty = selected();
  return difficulty == nullptr || Unlocked(*difficulty);
}

int BossSelectPanel::selected_unlock_level() const {
  const BossDifficulty* difficulty = selected();
  return difficulty == nullptr ? 0 : difficulty->unlock_level();
}

bool BossSelectPanel::selected_coming_soon() const {
  const BossDifficulty* difficulty = selected();
  return difficulty != nullptr && difficulty->coming_soon();
}

ResetPeriod BossSelectPanel::selected_reset() const {
  const BossDifficulty* difficulty = selected();
  return difficulty == nullptr ? RESET_PERIOD_UNSPECIFIED : difficulty->reset();
}

ftxui::Element BossSelectPanel::RenderDifficultyCell(int boss, int at) const {
  const Boss& fight = state_.bosses.at(bosses_[boss]);
  if (at >= fight.difficulties_size()) {
    return ftxui::text(std::string(kDifficultyWidth, ' '));
  }
  const BossDifficulty& difficulty = fight.difficulties(at);
  ftxui::Element name = ftxui::text(difficulty.name());
  // The cursor is the lit cell rather than a caret on the row, since Left and
  // Right walk the row and Up and Down the column.
  if (boss == selected_ && at == DifficultyAt(boss)) {
    name = std::move(name) | ftxui::inverted;
  }
  // Dim is the door: a fight the character has not levelled up to -- or one
  // that is not built yet -- is still listed and still readable, and Enter on
  // it says what it wants. Every cell answers for itself, so Normal can be
  // open while Chaos beside it is not.
  if (!Unlocked(difficulty) || difficulty.coming_soon()) {
    name = std::move(name) | ftxui::dim;
  }
  int pad = std::max(
      0, kDifficultyWidth - static_cast<int>(difficulty.name().size()));
  return ftxui::hbox({std::move(name), ftxui::text(std::string(pad, ' '))});
}

ftxui::Element BossSelectPanel::RenderBossList() const {
  int columns = Columns();
  std::vector<ftxui::Element> rows;
  rows.push_back(
      ftxui::text(" " + PadRight("Name", kBossNameWidth) +
                  PadRight("Difficulty", columns * kDifficultyWidth) + " "));
  rows.push_back(ThemedSeparator());
  if (bosses_.empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < static_cast<int>(bosses_.size()); ++i) {
    std::vector<ftxui::Element> cells;
    cells.push_back(ftxui::text(
        " " + PadRight(state_.bosses.at(bosses_[i]).name(), kBossNameWidth)));
    for (int at = 0; at < columns; ++at) {
      cells.push_back(RenderDifficultyCell(i, at));
    }
    cells.push_back(ftxui::text(" "));
    rows.push_back(ftxui::hbox(std::move(cells)));
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
  // A fight that is not built yet has none of the rest to state: no clock, no
  // reset, no reward. What it can honestly show is how big it is, under a line
  // saying why that is all there is.
  if (difficulty->coming_soon()) {
    rows.push_back(ftxui::text(PadRight(" Coming soon!", kDetailWidth)) |
                   ftxui::color(kYellow));
    rows.push_back(ftxui::text(std::string(kDetailWidth, ' ')));
    RenderPhaseHp(rows, *difficulty);
    return ThemedWindow(" Fight ", ftxui::vbox(std::move(rows)));
  }
  rows.push_back(
      DetailRow("Level", std::to_string(BossLevel(state_, *difficulty))));
  // Under the fight's own level, because the two together are what the player
  // is: what they are up against, and what it takes to stand there. Everything
  // below is the fight's own.
  if (difficulty->unlock_level() > 0) {
    // Red is the reason: the one value the player falls short of.
    rows.push_back(RedUnless(
        DetailRow("Unlock Level", std::to_string(difficulty->unlock_level())),
        Unlocked(*difficulty)));
  }
  RenderPhaseHp(rows, *difficulty);
  rows.push_back(
      DetailRow("PDR", std::to_string(BossPdr(state_, *difficulty)) + "%"));
  rows.push_back(
      DetailRow("Time Limit", Clock(difficulty->time_limit_seconds())));
  rows.push_back(DetailRow("Reset", ResetName(difficulty->reset())));
  if (!Unlocked(*difficulty)) {
    // Neither "Available" nor "Cleared" is true of a fight the character
    // cannot enter at all, and the level above says what it is short of.
    rows.push_back(DetailRow("Status", "Locked") | ftxui::color(kRed));
  } else if (!selected_available()) {
    // Red is the reason: the one value the player falls short of. What they
    // are short of here is a reset, so it goes on the status and nowhere else.
    rows.push_back(DetailRow("Status", "Cleared") | ftxui::color(kRed));
  } else {
    rows.push_back(DetailRow("Status", "Available") | ftxui::color(kGreen));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(ftxui::text(" Rewards ") | ftxui::color(kTheme));
  RenderRewards(rows, *difficulty);
  return ThemedWindow(" Fight ", ftxui::vbox(std::move(rows)));
}

void BossSelectPanel::RenderPhaseHp(std::vector<ftxui::Element>& rows,
                                    const BossDifficulty& difficulty) const {
  for (int i = 0; i < difficulty.phases_size(); ++i) {
    std::string label = difficulty.phases_size() > 1
                            ? "P" + std::to_string(i + 1) + " HP"
                            : "HP";
    rows.push_back(
        DetailRow(label, FormatCompact(PhaseHp(state_, difficulty.phases(i)))));
  }
}

std::string BossSelectPanel::RewardName(const MobDrop& drop) const {
  if (drop.has_equip()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state_.equips.find(drop.equip());
    return it == state_.equips.end() ? "" : it->second.name();
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state_.items.find(drop.item());
  return it == state_.items.end() ? "" : it->second.name();
}

void BossSelectPanel::RenderRewards(std::vector<ftxui::Element>& rows,
                                    const BossDifficulty& difficulty) const {
  // The meso first: it is the one thing a clear always pays, and everything
  // under it is a chance at something.
  int named = 0;
  if (difficulty.meso() > 0) {
    rows.push_back(DetailRow("Meso", FormatWithCommas(difficulty.meso())));
    ++named;
  }
  if (difficulty.exp() > 0) {
    rows.push_back(DetailRow("EXP", FormatWithCommas(difficulty.exp())));
    ++named;
  }
  // Honor is paid for a clear the calendar gates, as PayReward has it, and is
  // named only to a player who has something to spend it on.
  if (difficulty.reset() != RESET_PERIOD_UNSPECIFIED &&
      HonorVisible(state_.character.proto().level(),
                   state_.account.max_level())) {
    rows.push_back(DetailRow("Honor", FormatWithCommas(kBossClearHonor)));
    ++named;
  }
  // The gear last and apart, commonest first: what a clear always pays reads
  // as one block, and the drop a player is here for is at the bottom of it. A
  // drop no catalog holds names nothing and is left out here, before its place
  // in the list is counted.
  std::vector<const MobDrop*> items;
  std::vector<const MobDrop*> equips;
  for (const MobDrop& drop : difficulty.drops()) {
    if (RewardName(drop).empty()) {
      continue;
    }
    (drop.has_equip() ? equips : items).push_back(&drop);
  }
  std::stable_sort(equips.begin(), equips.end(),
                   [](const MobDrop* a, const MobDrop* b) {
                     return a->per_kill() > b->per_kill();
                   });
  for (const MobDrop* drop : items) {
    RenderDropRow(rows, *drop);
  }
  named += static_cast<int>(items.size());
  if (!equips.empty() && named > 0) {
    rows.push_back(ThemedSeparator());
  }
  for (const MobDrop* drop : equips) {
    RenderDropRow(rows, *drop);
  }
  named += static_cast<int>(equips.size());
  if (named == 0) {
    rows.push_back(EmptyState("empty"));
  }
}

void BossSelectPanel::RenderDropRow(std::vector<ftxui::Element>& rows,
                                    const MobDrop& drop) const {
  std::string name = RewardName(drop);
  // Wrapped rather than cut, and rather than let the panel grow: these are the
  // longest names in the game, and half of one names nothing. The chance sits
  // on the last line of the name, where a one-line name puts it in the same
  // column every other value on this panel stands in.
  std::string chance = DropChance(drop.per_kill());
  int width = kDetailWidth - 2;
  std::vector<std::string> lines = WrapBalanced(
      name, width, static_cast<int>(chance.size()) + 1, kNameIndent);
  for (int i = 0; i + 1 < static_cast<int>(lines.size()); ++i) {
    rows.push_back(ftxui::text(" " + PadRight(lines[i], width) + " "));
  }
  rows.push_back(ftxui::text(
      " " + PadRight(lines.back(), width - static_cast<int>(chance.size())) +
      chance + " "));
}

ftxui::Element BossSelectPanel::Render() const {
  // Held to a fixed height with the panels at the top of it, so that walking
  // the list -- where one fight has more drops than the next -- moves the
  // bottom of the panel and leaves its top where the eye left it.
  return ftxui::vbox({
             ftxui::hbox({RenderBossList(), RenderDetail()}),
             ftxui::filler(),
         }) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, kScreenHeight);
}

}  // namespace ms
