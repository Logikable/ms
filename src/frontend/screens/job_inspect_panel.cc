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

// Chars inside the window border. The panel stands beside the skill card
// rather than in the middle of the screen, so the room it takes is room the
// card does not get. Wide enough for the weapon row, which is the longest
// thing here that cannot slide: names longer than the column left over do.
constexpr int kContentWidth = kJobInspectBookWidth - 2;

// Seats " Weapon" with a gap after it. The row is the only labelled one on the
// panel, so the label column exists for its sake alone.
constexpr int kLabelWidth = 9;

// " Max 20 " -- the widest a level reads, with the gutter either side.
constexpr int kMaxLevelWidth = 8;

// The "> " cursor every list in the game marks its selected row with.
constexpr int kCursorWidth = 2;
constexpr int kNameWidth =
    kContentWidth - kCursorWidth - kSkillTagWidth - kMaxLevelWidth;

// Room for any book, so a row number in one and the same row number in
// another are different keys to the name clock. Keyed by job and stage both:
// a 4th job holds two books, its own and its V.
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
  // The 5th hands over a V Matrix rather than a book, and the matrix is the
  // common nodes as well as the job's own -- every kind of node alike, so a
  // boost node lists here the day one is written. Asked of the same function
  // the V page draws, so the two cannot disagree.
  if (stage_ == kFifthJobStage) {
    return VNodesFor(skills_, advancement);
  }
  // Below it, this job's own book and no other. A player choosing between a
  // Fighter and a Page already holds the Swordman's, so listing it again
  // would bury what they are actually choosing between.
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
  // A book holds names longer than the column, and cutting one silently costs
  // the player the one thing the row is for. The selected name slides under
  // the column instead; the rest sit cut, which they can be read out of by
  // stepping onto them.
  std::string name =
      ScrollingWindow(skill.name(), kNameWidth,
                      selected ? name_clock_.Elapsed()
                               : std::chrono::steady_clock::duration::zero());
  return ftxui::hbox({
      ftxui::text(selected ? "> " : "  "),
      ftxui::text(tag.text) | ftxui::color(tag.color),
      ftxui::text(std::move(name)),
      ftxui::text(PadLeft("Max " + std::to_string(skill.max_level()) + " ",
                          kMaxLevelWidth)),
  });
}

ftxui::Element JobInspectPanel::Render() const {
  // Folded with the job so that opening another book restarts the slide: the
  // cursor is back on row 0 either way, and row 0 of a book nobody has read
  // is a new name.
  name_clock_.Follow((static_cast<int>(job_) * kMaxJobStage + stage_) *
                         kJobClockStride +
                     selected_);

  std::vector<ftxui::Element> rows;
  // What the job is built around, which is the one thing a player cannot read
  // off the skills themselves -- most of a book names no weapon at all.
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
