/* SkillInspectScreen is the screen a skill's card opens on. The left card is
 * the skill and whatever comes bundled with it (a HEXA node's riders), as tabs.
 * The right card is each skill the left tab boosts (a V boost node, a Hyper
 * Skill that reinforces an attack), as tabs named after them, so the player
 * reads what is boosted next to the boost. It follows the left tab, and stays
 * open saying so when that tab boosts nothing while another does.
 *
 * Both cards scroll, and tabs take Left and Right, so the keys go only to the
 * focused card and Tab or Shift-Tab moves between them. A terminal too narrow
 * for two cards shows the left card alone, as a bundle that boosts nothing
 * always does.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_SCREEN_H_
#define MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_SCREEN_H_

#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/screens/inspect/skill_inspect_panel.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One card's skill and the levels SkillInspectPanel::SetSkill takes.
struct InspectedSkill {
  const Skill* skill = nullptr;
  int learned = 0;
  int bonus = 0;
};

// One tab of the left card: its skill and the skills it boosts.
struct InspectedTab {
  InspectedSkill skill;
  std::vector<InspectedSkill> boosted;
};

class SkillInspectScreen {
 public:
  // Read live every frame, since the levels can change behind the card.
  // The skill first, then what is bundled with it.
  void SetSkills(std::vector<InspectedTab> tabs);
  // The terminal's size, which decides whether the two cards fit.
  void SetSize(int columns, int rows) {
    columns_ = columns;
    rows_ = rows;
  }
  // Focus on the left card, the first tabs, both cards at the top: a card the
  // player has just opened.
  void Reset();
  // The arrows and Tab. Returns false for any other key.
  bool OnEvent(const ftxui::Event& event);

  // True while the boosted skills are shown beside the card.
  bool Split() const;
  int own_tab() const {
    return own_tab_;
  }
  int tab() const {
    return tab_;
  }
  bool boosted_focused() const {
    return boosted_focused_;
  }

  ftxui::Element Render() const;

 private:
  // The width a card is held to, so switching tabs doesn't move its edges: the
  // widest of `skills` within `budget` columns.
  static int WidestCard(const std::vector<InspectedSkill>& skills, int budget);
  const std::vector<InspectedSkill>& boosted() const {
    return tabs_[own_tab_].boosted;
  }

  std::vector<InspectedTab> tabs_;
  int own_tab_ = 0;
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
