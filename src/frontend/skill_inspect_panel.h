/* SkillInspectPanel renders everything there is to know about one skill: the
 * name and maximum level, the description, the facts that hold at every level
 * (how many enemies a swing reaches, what weapon it must be held with), then
 * what the skill grants at the level the character has it at and what one more
 * point would buy. The window title is Active or Passive, which is the first
 * thing worth knowing about a skill.
 *
 * Every number is read off the skill's own SkillEffect levers, in the same
 * base + per_level * (L - 1) shape the stats themselves are folded with, so a
 * skill that gains a lever gains a line here without this file changing.
 *
 * An unlearned skill has no current-level block and a maxed one has no next-
 * level block; a skill that is both would be a skill with no levels at all.
 * SetSkill(nullptr, 0) renders a placeholder.
 */
#ifndef MS_SRC_FRONTEND_SKILL_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SKILL_INSPECT_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/skill.pb.h"

namespace ms {

class SkillInspectPanel {
 public:
  // `level` is the level the character has learned the skill to, 0 for one
  // they have not spent a point on.
  void SetSkill(const Skill* skill, int level);
  ftxui::Element Render() const;

 private:
  const Skill* skill_ = nullptr;
  int level_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SKILL_INSPECT_PANEL_H_
