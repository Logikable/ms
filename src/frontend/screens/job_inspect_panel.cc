#include "src/frontend/screens/job_inspect_panel.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/job_name.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The width inside the window border. The panel is beside the skill card rather
// than centred, so the room it takes is room the card doesn't get. Wide enough
// for the weapon row, the longest content here that can't scroll; longer names
// scroll.
constexpr int kContentWidth = kJobInspectBookWidth - 2;

// Fits " Weapon" with a gap after it. It is the only labelled row on the panel,
// so the label column exists just for it.
constexpr int kLabelWidth = 9;

// " Max 20 ": the widest a level gets, with a gutter on each side.
constexpr int kMaxLevelWidth = 8;

// The "> " cursor every list in the game uses to mark its selected row.
constexpr int kCursorWidth = 2;
constexpr int kNameWidth =
    kContentWidth - kCursorWidth - kSkillTagWidth - kMaxLevelWidth;

// Room for any book, so the same row number in two books gives different keys
// to the name clock. Keyed by job and stage together, since the same job name
// has two books: its 4th job book and its V Matrix.
constexpr int kJobClockStride = 100;

}  // namespace

JobInspectPanel::JobInspectPanel(std::map<std::string, Skill> skills)
    : skills_(std::move(skills)) {
}

void JobInspectPanel::SetJob(Job job, int stage) {
  job_ = job;
  stage_ = stage;
  selected_ = 0;
}

std::vector<const Skill*> JobInspectPanel::Skills() const {
  JobAdvancement advancement = AdvancementForJobStage(job_, stage_);
  // The 5th advancement gives a V Matrix instead of a book, and the matrix
  // includes the common nodes as well as the job's own, of every kind, so a
  // boost node appears here as soon as one is written. Built with the same
  // function the V page uses, so the two can't disagree.
  if (stage_ == kFifthJobStage) {
    return VNodesFor(skills_, advancement);
  }
  // Below the 5th, this job's own book and no other. A player choosing between
  // Fighter and Page already has the Swordman's book, so listing it again would
  // bury what they are actually choosing between.
  return SkillsForAdvancement(skills_, advancement);
}

const Skill* JobInspectPanel::selected_skill() const {
  std::vector<const Skill*> skills = Skills();
  if (skills.empty()) {
    return nullptr;
  }
  return skills[std::clamp(selected_, 0, static_cast<int>(skills.size()) - 1)];
}

void JobInspectPanel::MoveCursor(int delta) {
  selected_ = StepCursor(selected_, delta, static_cast<int>(Skills().size()));
}

ftxui::Element JobInspectPanel::RenderSkillRow(const Skill& skill,
                                               int index) const {
  bool selected = index == selected_;
  KindTag tag = TagFor(skill);
  // A book has names longer than the column, and cutting one without a way to
  // read it would hide what the row is for. The selected name scrolls inside
  // the column; the others are cut and can be read by moving onto them.
  std::string name =
      ScrollingWindow(skill.name(), kNameWidth,
                      selected ? name_clock_.Elapsed()
                               : std::chrono::steady_clock::duration::zero());
  return HighlightRow(
      ftxui::hbox({
          ftxui::text(selected ? "> " : "  "),
          ftxui::text(tag.text) | ftxui::color(tag.color),
          ftxui::text(std::move(name)),
          ftxui::text(
              PadLeft("Max " + std::to_string(SkillMaxLevel(skill)) + " ",
                      kMaxLevelWidth)),
      }),
      selected);
}

ftxui::Element JobInspectPanel::Render() const {
  // Combined with the job so opening another book restarts the scroll: the
  // cursor is back on row 0 either way, and row 0 of a new book is a new name.
  name_clock_.Follow((static_cast<int>(job_) * kMaxJobStage + stage_) *
                         kJobClockStride +
                     selected_);

  std::vector<ftxui::Element> rows;
  // The weapons the job is built around, the one thing a player can't learn
  // from the skills themselves, since most skills name no weapon at all.
  std::string weapons = FormatWeaponList(ExpectedWeapons(job_));
  rows.push_back(ftxui::text(PadRight("  " + PadRight("Weapon", kLabelWidth) +
                                          (weapons.empty() ? "-" : weapons),
                                      kContentWidth)));
  rows.push_back(ThemedSeparator());

  std::vector<const Skill*> skills = Skills();
  if (skills.empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
    rows.push_back(RenderSkillRow(*skills[i], i));
  }
  return ThemedWindow(
      " " + AdvancementName(job_, stage_) + " ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth));
}

ftxui::Element JobInspectScreen(ftxui::Element book, ftxui::Element card,
                                int rows) {
  return ftxui::hbox({std::move(book), std::move(card)}) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, rows);
}

}  // namespace ms
