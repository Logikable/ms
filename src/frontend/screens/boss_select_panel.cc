#include "src/frontend/screens/boss_select_panel.h"

#include <algorithm>
#include <chrono>
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
#include "src/combat/drop.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

constexpr int kDifficultyWidth = 10;
// The detail rows are a label and a right-aligned value, so the panel does not
// breathe as a number gains a digit. The value takes whatever the card has
// left over after the label and a column of clearance on each side -- the card
// is sized by its TITLE, which is the longest thing on it.
constexpr int kLabelWidth = 12;
constexpr int kValueWidth = kDetailWidth - kLabelWidth - 2;
// The fight card's own column plus the lane the scroll bar runs down. The lane
// is held open whether or not the rewards overflow, so the card does not
// change width as the cursor walks the list.
constexpr int kDetailContentWidth = kDetailWidth + 1;
// The rows inside a panel's border.
constexpr int kPanelRows = kBossPanelHeight - 2;
// Two borders and the row of switches between them.
constexpr int kOptionsHeight = 3;
// The switches the row holds, left to right.
constexpr int kOptionCount = 1;
// One switch: its box, and the name beside it. The box leads, so the row reads
// as a column of states rather than a sentence to the end of.
ftxui::Element OptionChip(const std::string& label, bool on, bool on_cursor) {
  return HighlightRow(
      ftxui::text(std::string("[") + (on ? "X" : " ") + "] " + label),
      on_cursor);
}

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
  // The level the fight opens at, on the easiest difficulty. The GATE says
  // where a fight sits in a character's life; HP stands in badly, one body
  // holding what another spreads over six parts. A fight with no gate is not
  // built yet and sorts last, on the HP it does state.
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
  focus_ = BossPanel::kList;
  option_ = 0;
  scroll_ = 0;
}

bool BossSelectPanel::practice() const {
  return state_.boss_options.practice();
}

void BossSelectPanel::SwitchPanel(int delta) {
  focus_ = static_cast<BossPanel>(
      StepCursor(static_cast<int>(focus_), delta, kBossPanelCount));
}

void BossSelectPanel::MoveCursor(int delta) {
  if (focus_ == BossPanel::kFight) {
    ScrollRewards(delta);
    return;
  }
  if (focus_ != BossPanel::kList) {
    return;
  }
  selected_ = StepCursor(selected_, delta, static_cast<int>(bosses_.size()));
  scroll_ = 0;
}

int BossSelectPanel::Columns() const {
  int columns = 0;
  for (const std::string& key : bosses_) {
    columns = std::max(columns, state_.bosses.at(key).difficulties_size());
  }
  return columns;
}

void BossSelectPanel::ChangeDifficulty(int delta) {
  if (focus_ == BossPanel::kOptions) {
    option_ = std::clamp(option_ + delta, 0, kOptionCount - 1);
    return;
  }
  if (focus_ != BossPanel::kList) {
    return;
  }
  column_ = std::clamp(column_ + delta, 0, std::max(0, Columns() - 1));
  scroll_ = 0;
}

void BossSelectPanel::ScrollRewards(int delta) {
  const BossDifficulty* difficulty = selected();
  if (difficulty == nullptr || difficulty->coming_soon()) {
    scroll_ = 0;
    return;
  }
  DetailRows detail =
      BuildDetail(*difficulty, std::chrono::steady_clock::now());
  int visible = std::max(0, kPanelRows - static_cast<int>(detail.head.size()));
  int last = std::max(0, static_cast<int>(detail.rewards.size()) - visible);
  scroll_ = std::clamp(scroll_ + delta, 0, last);
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

ftxui::Element BossSelectPanel::RenderBossList(
    std::chrono::steady_clock::time_point now) const {
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
        " " + ScrollingWindow(state_.bosses.at(bosses_[i]).name(),
                              kBossNameWidth, now.time_since_epoch())));
    for (int at = 0; at < columns; ++at) {
      cells.push_back(RenderDifficultyCell(i, at));
    }
    cells.push_back(ftxui::text(" "));
    rows.push_back(ftxui::hbox(std::move(cells)));
  }
  return ThemedWindow(" Bosses ", ftxui::vbox(std::move(rows)),
                      focus_ == BossPanel::kList) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kBossPanelHeight);
}

ftxui::Element BossSelectPanel::RenderDetailTitle(
    std::chrono::steady_clock::time_point now) const {
  return ftxui::text(" " +
                     ScrollingWindow(selected_title(), kDetailWidth - 2,
                                     now.time_since_epoch()) +
                     " ") |
         ftxui::color(kTheme);
}

BossSelectPanel::DetailRows BossSelectPanel::BuildDetail(
    const BossDifficulty& difficulty,
    std::chrono::steady_clock::time_point now) const {
  DetailRows detail;
  std::vector<ftxui::Element>& rows = detail.head;
  rows.push_back(RenderDetailTitle(now));
  rows.push_back(ThemedSeparator());
  rows.push_back(
      DetailRow("Level", std::to_string(BossLevel(state_, difficulty))));
  // Under the fight's own level, because the two together are what the player
  // is: what they are up against, and what it takes to stand there. Everything
  // below is the fight's own.
  if (difficulty.unlock_level() > 0) {
    // Red is the reason: the one value the player falls short of.
    rows.push_back(RedUnless(
        DetailRow("Unlock Level", std::to_string(difficulty.unlock_level())),
        Unlocked(difficulty)));
  }
  RenderPhaseHp(rows, difficulty);
  rows.push_back(
      DetailRow("PDR", std::to_string(BossPdr(state_, difficulty)) + "%"));
  rows.push_back(
      DetailRow("Time Limit", Clock(difficulty.time_limit_seconds())));
  rows.push_back(DetailRow("Reset", ResetName(difficulty.reset())));
  if (!Unlocked(difficulty)) {
    // Neither "Available" nor "Cleared" is true of a fight the character
    // cannot enter at all, and the level above says what it is short of.
    rows.push_back(DetailRow("Status", "Locked") | ftxui::color(kRed));
  } else if (practice()) {
    // Practice walks past the reset, so "Cleared" is no longer what stands
    // between the player and the fight. Yellow rather than green: it opens the
    // door and empties the purse behind it.
    rows.push_back(DetailRow("Status", "Practice") | ftxui::color(kYellow));
  } else if (!selected_available()) {
    // Red is the reason: the one value the player falls short of. What they
    // are short of here is a reset, so it goes on the status and nowhere else.
    rows.push_back(DetailRow("Status", "Cleared") | ftxui::color(kRed));
  } else {
    rows.push_back(DetailRow("Status", "Available") | ftxui::color(kGreen));
  }
  rows.push_back(ThemedSeparator());
  ftxui::Element heading = ftxui::text(" Rewards ") | ftxui::color(kTheme);
  RenderRewards(detail.rewards, difficulty, now);
  // Dimmed whole rather than cut: a practice run pays none of it, and the
  // player is still reading the card to decide what a real clear is worth.
  if (practice()) {
    heading = std::move(heading) | ftxui::dim;
    for (RewardRow& row : detail.rewards) {
      row.element = std::move(row.element) | ftxui::dim;
    }
  }
  rows.push_back(std::move(heading));
  return detail;
}

void BossSelectPanel::AppendRewardWindow(
    std::vector<ftxui::Element>& rows, std::vector<RewardRow>& rewards) const {
  int total = static_cast<int>(rewards.size());
  int visible = std::max(0, kPanelRows - static_cast<int>(rows.size()));
  // Clamped here as well as in ScrollRewards: the window shrinks as the fight
  // above it grows, and a card scrolled to its foot then has too far to go.
  int offset = std::clamp(scroll_, 0, std::max(0, total - visible));
  std::vector<ftxui::Element> cells = ScrollBarCells(total, offset, visible);
  for (int row = 0; row < visible && offset + row < total; ++row) {
    RewardRow& reward = rewards[offset + row];
    if (reward.separator) {
      rows.push_back(std::move(reward.element));
      continue;
    }
    ftxui::Element cell =
        cells.empty() ? ftxui::text(" ") : std::move(cells[row]);
    rows.push_back(ftxui::hbox({
        std::move(reward.element) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kDetailWidth),
        std::move(cell) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
  }
}

ftxui::Element BossSelectPanel::RenderDetail(
    std::chrono::steady_clock::time_point now) const {
  const BossDifficulty* difficulty = selected();
  std::vector<ftxui::Element> rows;
  if (difficulty == nullptr) {
    rows.push_back(RenderDetailTitle(now));
    rows.push_back(ThemedSeparator());
    rows.push_back(EmptyState("empty"));
  } else if (difficulty->coming_soon()) {
    // A fight that is not built yet has none of the rest to state: no clock,
    // no reset, no reward. What it can honestly show is how big it is, under a
    // line saying why that is all there is.
    rows.push_back(RenderDetailTitle(now));
    rows.push_back(ThemedSeparator());
    rows.push_back(ftxui::text(PadRight(" Coming soon!", kDetailWidth)) |
                   ftxui::color(kYellow));
    rows.push_back(ftxui::text(std::string(kDetailWidth, ' ')));
    RenderPhaseHp(rows, *difficulty);
  } else {
    DetailRows detail = BuildDetail(*difficulty, now);
    rows = std::move(detail.head);
    AppendRewardWindow(rows, detail.rewards);
  }
  return ThemedWindow(
             " Fight ",
             ftxui::vbox(std::move(rows)) |
                 ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kDetailContentWidth),
             focus_ == BossPanel::kFight) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kBossPanelHeight);
}

ftxui::Element BossSelectPanel::RenderOptions() const {
  // The band marks the cursor only while the row holds the keys, a band on an
  // unfocused row claiming a selection Enter would not throw.
  bool focused = focus_ == BossPanel::kOptions;
  return ThemedWindow(
             " Options ",
             ftxui::hbox({
                 ftxui::text("  "),
                 OptionChip("Practice", practice(), focused && option_ == 0),
             }),
             focused) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kOptionsHeight);
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

void BossSelectPanel::RenderRewards(
    std::vector<RewardRow>& rows, const BossDifficulty& difficulty,
    std::chrono::steady_clock::time_point now) const {
  // The meso first: it is the one thing a clear always pays, and everything
  // under it is a chance at something.
  int named = 0;
  if (difficulty.meso() > 0) {
    rows.push_back({DetailRow("Meso", FormatWithCommas(difficulty.meso()))});
    ++named;
  }
  if (difficulty.exp() > 0) {
    rows.push_back({DetailRow("EXP", FormatWithCommas(difficulty.exp()))});
    ++named;
  }
  // Honor is paid for a clear the calendar gates, as PayReward has it, and is
  // named only to a player who has something to spend it on.
  if (difficulty.reset() != RESET_PERIOD_UNSPECIFIED &&
      HonorVisible(state_.character.proto().level(),
                   state_.account.max_level())) {
    rows.push_back({DetailRow("Honor", FormatWithCommas(kBossClearHonor))});
    ++named;
  }
  // The prizes last and apart, commonest first: what a clear always pays reads
  // as one block, and the drop a player is here for is at the bottom of it. A
  // drop no catalog holds names nothing and is left out here, before its place
  // in the list is counted.
  std::vector<const MobDrop*> paid;
  std::vector<const MobDrop*> prizes;
  for (const MobDrop& drop : difficulty.drops()) {
    if (DropName(state_, drop).empty()) {
      continue;
    }
    (DropIsPrize(state_, drop) ? prizes : paid).push_back(&drop);
  }
  std::stable_sort(prizes.begin(), prizes.end(),
                   [](const MobDrop* a, const MobDrop* b) {
                     return a->per_kill() > b->per_kill();
                   });
  for (const MobDrop* drop : paid) {
    RenderDropRow(rows, *drop, now);
  }
  named += static_cast<int>(paid.size());
  if (!prizes.empty() && named > 0) {
    rows.push_back({ThemedSeparator(), /*separator=*/true});
  }
  for (const MobDrop* drop : prizes) {
    RenderDropRow(rows, *drop, now);
  }
  named += static_cast<int>(prizes.size());
  if (named == 0) {
    rows.push_back({EmptyState("empty")});
  }
}

void BossSelectPanel::RenderDropRow(
    std::vector<RewardRow>& rows, const MobDrop& drop,
    std::chrono::steady_clock::time_point now) const {
  // Slid rather than wrapped: drop names run long, half of one names nothing,
  // and a second row would push the drop under it out of the window. The
  // chance keeps the column every other value on this panel stands in.
  std::string chance = DropChance(drop.per_kill());
  int width = kDetailWidth - 3 - static_cast<int>(chance.size());
  rows.push_back({ftxui::text(
      " " +
      ScrollingWindow(DropName(state_, drop), width, now.time_since_epoch()) +
      " " + chance + " ")});
}

ftxui::Element BossSelectPanel::Render(
    std::chrono::steady_clock::time_point now) const {
  return ftxui::vbox({
      ftxui::hbox({RenderBossList(now), RenderDetail(now)}),
      RenderOptions(),
  });
}

}  // namespace ms
