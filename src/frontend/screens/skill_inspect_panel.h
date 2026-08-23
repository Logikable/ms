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

  // `learned` is the level the character has spent points to, 0 for one they
  // have not opened. `bonus` is the levels their book lends every skill --
  // Combat Orders' -- which the card counts into the level it heads its
  // blocks with. Both are ignored under kPreview, which is about a skill
  // rather than about a character.
  void SetSkill(const Skill* skill, int learned, int bonus,
                Levels levels = kLearned);

  // The rows the card may take, borders included. Past this it scrolls, with
  // a bar down its right edge saying how much is off screen. Zero means no
  // limit, which is what a card laid out beside something else wants.
  //
  // Not read from the terminal here, for the reason CharacterPanel gives:
  // tests draw the card at whatever size they choose.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
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
  // One row of the card. `separator` rules off a section, and is drawn across
  // the scroll bar's column as well as the text -- a rule that stops a column
  // short of the border reads as a gap in the card.
  struct CardRow {
    ftxui::Element element;
    bool separator = false;
  };

  // Every row of the card, in order, borders aside. Render draws a window of
  // these and ScrollBy counts them: both have to agree on how tall the card
  // is, so neither gets its own copy of the layout.
  std::vector<CardRow> BuildRows() const;
  // How many of `total` rows fit inside the border, given the row budget. All
  // of them when there is no budget.
  int VisibleRows(int total) const;

  const Skill* skill_ = nullptr;
  int level_ = 0;
  int bonus_ = 0;
  Levels levels_ = kLearned;
  int max_rows_ = 0;
  int offset_ = 0;
};

// The rows the tallest preview card of `skills` takes, borders included. What
// a screen holding a card beside a list of skills asks before drawing it, so
// the screen stands still while the cursor walks cards of different heights.
// Measured rather than guessed: a card is as tall as its description and
// whichever levers its skill happens to carry.
int TallestPreviewCardRows(const std::vector<const Skill*>& skills);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SKILL_INSPECT_PANEL_H_
