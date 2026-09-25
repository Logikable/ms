/* SkillInspectPanel shows everything about one skill: its name and maximum
 * level, the description, the facts that apply at every level (how many enemies
 * an attack hits, what weapon it requires), then what the skill grants at the
 * character's current level and what one more point would give. The window
 * title is Active or Passive, the first thing worth knowing about a skill.
 *
 * Every number comes from the skill's own SkillEffect fields, in the same base
 * + per_level * (L - 1) form the stats use, so a skill that gains a field gains
 * a line here without this file changing.
 *
 * An unlearned skill has no current-level block and a maxed one has no
 * next-level block; a skill with neither would have no levels at all.
 * SetSkill(nullptr, 0) shows a placeholder.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/scroll_card.h"
#include "src/protos/skill.pb.h"

namespace ms {

// A card with no row budget that still keeps the bar's column, so a card
// measured without limits is the width it is drawn at.
ScrollCard UnboundedCard();

class SkillInspectPanel {
 public:
  // Which levels the card shows.
  enum Levels {
    // The character's current level of the skill and what one more point would
    // give: what a player spending SP is choosing between.
    kLearned,
    // Level 1 and the maximum level, for a book nobody has opened yet: what the
    // first point gives and what the skill becomes. This is what a player
    // choosing a job compares, where "one more point" means nothing.
    kPreview,
  };

  // `learned` is the level points were spent to, and `bonus` the extra levels
  // the book gives, which the card includes in the level shown on its blocks.
  // Both are ignored under kPreview, which describes the skill itself.
  void SetSkill(const Skill* skill, int learned, int bonus,
                Levels levels = kLearned);

  // The most rows the card may take; beyond this it scrolls, with a bar on its
  // right edge. Zero means no limit, which suits a card beside something else.
  // It isn't read from the terminal, for the reason CharacterPanel gives.
  void SetMaxRows(int rows);
  // The width range the card lays out in, borders and scroll bar included. Both
  // default to zero, which measures the card from the skill and gives it what
  // it needs. A screen that must stay still while the cursor moves through a
  // book sets the two equal, to the widest card in it.
  void SetWidthBounds(int min_columns, int max_columns) {
    min_width_ = min_columns;
    max_width_ = max_columns;
  }
  // Scrolls the level blocks `delta` rows, stopping at both ends. The name,
  // description and every-level facts above them stay still. There is nothing
  // to select, only text to read, so a key moves the page itself.
  void ScrollBy(int delta);
  // Back to the top, for a card the player has just opened.
  void ResetScroll() {
    card_.Reset();
  }

  ftxui::Element Render() const;

 private:
  const Skill* skill_ = nullptr;
  int level_ = 0;
  int bonus_ = 0;
  Levels levels_ = kLearned;
  int min_width_ = 0;
  int max_width_ = 0;
  ScrollCard card_ = UnboundedCard();
};

// The size of the largest preview card of `skills`, borders included.
struct PreviewCardSize {
  int rows = 0;
  int columns = 0;
};

// A screen showing a card beside a list of skills calls this before drawing any
// of them, so it stays still as the cursor moves between cards of different
// shapes. Measured rather than estimated: a card is as wide as its widest row
// and as tall as the fields its skill has.
PreviewCardSize LargestPreviewCard(const std::vector<const Skill*>& skills,
                                   int max_columns);

// What `skill` grants at `level`, as label and value, in the order the card's
// level block lists them: {"Crit Rate", "+9%"}. For a list with an effect
// column rather than a card (see LinkSkillPanel). The section headings the card
// uses aren't included, since a cell has no room to say which part of a skill a
// number came from.
struct SkillEffectLine {
  std::string label;
  std::string value;
};
std::vector<SkillEffectLine> SkillEffectsAt(const Skill& skill, int level);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_
