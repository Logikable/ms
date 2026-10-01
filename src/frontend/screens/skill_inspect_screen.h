/* SkillInspectScreen is the screen a skill's card opens on. A skill that boosts
 * others (a V boost node, a Hyper Skill that reinforces an attack) shows the
 * card of each skill it boosts beside its own, as tabs named after them, so
 * the player reads what is boosted next to the boost.
 *
 * Both cards can scroll, and the right one's tabs take Left and Right, so the
 * keys go only to the focused card and Tab or Shift-Tab moves between them. A
 * terminal too narrow for two cards shows the skill's own card alone, as a
 * skill that boosts nothing always does.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_SCREEN_H_
#define MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_SCREEN_H_

#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One card's skill and the levels SkillInspectPanel::SetSkill takes.
struct InspectedSkill {
  const Skill* skill = nullptr;
  int learned = 0;
  int bonus = 0;
};

class SkillInspectScreen {
 public:
  // Read live every frame, since the levels can change behind the card.
  void SetSkills(InspectedSkill skill, std::vector<InspectedSkill> boosted);
  // The terminal's size, which decides whether the two cards fit.
  void SetSize(int columns, int rows) {
    columns_ = columns;
    rows_ = rows;
  }
  // Focus on the skill's own card, the first tab, both cards at the top: a
  // card the player has just opened.
  void Reset();
  // The arrows and Tab. Returns false for any other key.
  bool OnEvent(const ftxui::Event& event);

  // True while the boosted skills are shown beside the card.
  bool Split() const;
  int tab() const {
    return tab_;
  }
  bool boosted_focused() const {
    return boosted_focused_;
  }

  ftxui::Element Render() const;

 private:
  // The width every tab's card is held to, so switching tabs doesn't move the
  // card's edges: the widest of them within `budget` columns.
  int BoostedWidth(int budget) const;

  InspectedSkill skill_;
  std::vector<InspectedSkill> boosted_;
  int columns_ = 0;
  int rows_ = 0;
  int tab_ = 0;
  bool boosted_focused_ = false;
  // Mutable because the cards lay themselves out in Render, which is const.
  mutable SkillInspectPanel own_;
  mutable SkillInspectPanel other_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_SCREEN_H_
