#include "src/frontend/screens/job_inspect_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Chars inside the window border. The longest shipped skill name is "Final
// Attack: Crossbow" at 21, and the weapon row's value is longer still --
// "Two-Handed Sword" for a job with only half a pair. The panel stands beside
// the skill card rather than in the middle of the screen, so the room it takes
// is room the card does not get; this is the least that holds both.
constexpr int kContentWidth = 33;

// Seats " Weapon" with a gap after it. The row is the only labelled one on the
// panel, so the label column exists for its sake alone.
constexpr int kLabelWidth = 9;

// " Max 20 " -- the widest a level reads, with the gutter either side.
constexpr int kMaxLevelWidth = 8;
constexpr int kNameWidth =
    kContentWidth - 1 - kSkillTagWidth - kMaxLevelWidth - 1;

// The advancement whose book is `job`'s own -- the inverse of
// JobForAdvancement. Walked rather than switched, because every stage of a job
// answers AdvancementForJobStage and only one of them names this job back: a
// Fighter is at the Swordman's advancement in stage 1 and their own in stage 2.
JobAdvancement AdvancementOf(Job job) {
  for (int stage = 1; stage <= 3; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(job, stage);
    if (advancement != JOB_ADVANCEMENT_UNSPECIFIED &&
        JobForAdvancement(advancement) == job) {
      return advancement;
    }
  }
  return JOB_ADVANCEMENT_UNSPECIFIED;
}

}  // namespace

JobInspectPanel::JobInspectPanel(std::map<std::string, Skill> skills)
    : skills_(std::move(skills)) {
}

void JobInspectPanel::SetJob(Job job) {
  job_ = job;
  selected_ = 0;
}

std::vector<const Skill*> JobInspectPanel::Skills() const {
  // This job's own book and no other. A player choosing between a Fighter and
  // a Page already holds the Swordman's, so listing it again would bury what
  // they are actually choosing between.
  return SkillsForAdvancement(skills_, AdvancementOf(job_));
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
  KindTag tag = TagFor(skill);
  std::string name = PadRight(skill.name(), kNameWidth);
  ftxui::Element name_text = ftxui::text(name);
  if (index == selected_) {
    name_text = name_text | ftxui::inverted;
  }
  return ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(tag.text) | ftxui::color(tag.color),
      std::move(name_text),
      ftxui::text(PadLeft("Max " + std::to_string(skill.max_level()) + " ",
                          kMaxLevelWidth)),
  });
}

ftxui::Element JobInspectPanel::Render() const {
  std::vector<ftxui::Element> rows;
  // What the job is built around, which is the one thing a player cannot read
  // off the skills themselves -- most of a book names no weapon at all.
  std::string weapons = FormatWeaponList(ExpectedWeapons(job_));
  rows.push_back(ftxui::text(PadRight(
      " " + PadRight("Weapon", kLabelWidth) + (weapons.empty() ? "-" : weapons),
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
      " " + JobName(job_) + " ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth));
}

ftxui::Element JobInspectScreen(ftxui::Element book, ftxui::Element card,
                                int rows) {
  return ftxui::hbox({std::move(book), std::move(card)}) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, rows);
}

}  // namespace ms
