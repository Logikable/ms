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
#ifndef MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_

#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/protos/skill.pb.h"

namespace ms {

class SkillInspectPanel {
 public:
  // Which levels the card shows.
  enum Levels {
    // The level the character has the skill at, and what one more point would
    // buy. What a player spending SP is deciding between.
    kLearned,
    // Level 1 and the last level, for a book nobody has opened yet: what the
    // first point buys and what the skill becomes. What a player choosing a
    // job is deciding between, where "one more point" means nothing.
    kPreview,
  };

  // `level` is the level the character has learned the skill to, 0 for one
  // they have not spent a point on. Ignored under kPreview, which is about a
  // skill rather than about a character.
  void SetSkill(const Skill* skill, int level, Levels levels = kLearned);
  ftxui::Element Render() const;

 private:
  const Skill* skill_ = nullptr;
  int level_ = 0;
  Levels levels_ = kLearned;
};

// The rows the tallest preview card of `skills` takes, borders included. What
// a screen holding a card beside a list of skills asks before drawing it, so
// the screen stands still while the cursor walks cards of different heights.
// Measured rather than guessed: a card is as tall as its description and
// whichever levers its skill happens to carry.
int TallestPreviewCardRows(const std::vector<const Skill*>& skills);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_
