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

#include <string>
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

  // `learned` is the level points were spent to and `bonus` the levels the
  // book lends, which the card counts into the level it heads its blocks with.
  // Both are ignored under kPreview, which is about a SKILL.
  void SetSkill(const Skill* skill, int learned, int bonus,
                Levels levels = kLearned);

  // The rows the card may take; past this it scrolls, with a bar down its
  // right edge. Zero is no limit, which a card beside something else wants.
  // Not read from the terminal, for CharacterPanel's reason.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  // The columns the card lays out in, borders and scroll bar included. Both
  // default to zero, which measures the card from the skill and gives it what
  // it asks for. A screen that must stand still while the cursor walks a book
  // sets the two equal, to the widest card in it.
  void SetWidthBounds(int min_columns, int max_columns) {
    min_width_ = min_columns;
    max_width_ = max_columns;
  }
  // Moves the view `delta` rows, held to the card at both ends. There is no
  // selected row on this screen -- nothing to point at, only text to read --
  // so a key moves the page itself, and it does not wrap: coming out of the
  // foot at the head is disorienting with no cursor to follow.
  void ScrollBy(int delta);
  // Back to the top, for a card the player has just opened.
  void ResetScroll() {
    offset_ = 0;
  }

  ftxui::Element Render() const;

 private:
  // How many of `total` rows fit inside the border, given the row budget. All
  // of them when there is no budget.
  int VisibleRows(int total) const;

  const Skill* skill_ = nullptr;
  int level_ = 0;
  int bonus_ = 0;
  Levels levels_ = kLearned;
  int max_rows_ = 0;
  int min_width_ = 0;
  int max_width_ = 0;
  int offset_ = 0;
};

// The size of the largest preview card of `skills`, borders included.
struct PreviewCardSize {
  int rows = 0;
  int columns = 0;
};

// What a screen holding a card beside a list of skills asks before drawing
// any of them, so it stands still as the cursor walks cards of different
// shapes. MEASURED rather than guessed: a card is as wide as its widest row
// and as tall as whichever levers its skill carries.
PreviewCardSize LargestPreviewCard(const std::vector<const Skill*>& skills,
                                   int max_columns);

// What `skill` grants at `level`, label and value, in the order the card's own
// level block lists them: {"Crit Rate", "+9%"}. For a LIST with a column for
// an effect rather than a card -- see LinkSkillPanel. The headings the card
// rules its sections off with are not among them: a cell has no room to say
// which half of a skill a number came from.
struct SkillEffectLine {
  std::string label;
  std::string value;
};
std::vector<SkillEffectLine> SkillEffectsAt(const Skill& skill, int level);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_
