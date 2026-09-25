/* JobInspectPanel is what a player reads before taking an advancement: the
 * weapon the job is built around, and its whole book of skills. It sits to the
 * left of a SkillInspectPanel showing the skill under the cursor, so together
 * they answer "what would I become".
 *
 * The 5th advancement gives a V Matrix instead of a book, so that is what it
 * lists: the common nodes along with the job's own.
 *
 * Read-only. Up and Down move through the skills, and nothing else on the
 * screen takes a key. The advancement itself is taken from the menu this screen
 * opened from, so there is one path to it and it still asks for confirmation.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_JOB_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_JOB_INSPECT_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The book's full width, borders included. The screen beside it reads this to
// know how much room the skill card has.
inline constexpr int kJobInspectBookWidth = 35;

class JobInspectPanel {
 public:
  // `skills` is the loaded skill catalog, keyed by file stem. Copied because
  // the catalog doesn't change after loading, as with the Character panel.
  explicit JobInspectPanel(std::map<std::string, Skill> skills = {});

  // Opens the panel on the advancement into `job` at `stage`, with the cursor
  // at the top of its book. The stage is needed as well as the job because the
  // 5th advancement keeps the job's name: without it a Night Lord V would open
  // the Night Lord's book.
  void SetJob(Job job, int stage);
  Job job() const {
    return job_;
  }

  // The skill under the cursor, or null for a job whose book is empty, meaning
  // no skills have been written for it yet, which isn't a real case.
  const Skill* selected_skill() const;

  // Moves through the book, wrapping at both ends.
  void MoveCursor(int delta);

  // The job's book, in the Character panel's order. Public because the screen
  // measures the cards of every skill before drawing any one.
  std::vector<const Skill*> Skills() const;

  ftxui::Element Render() const;

 private:
  // One skill row: the kind tag, the name, and its maximum level.
  ftxui::Element RenderSkillRow(const Skill& skill, int index) const;

  std::map<std::string, Skill> skills_;
  Job job_ = JOB_UNSPECIFIED;
  int stage_ = 0;
  int selected_ = 0;
  // How long the cursor has been on the current row, for scrolling a long name.
  // Mutable because the render writes it, which is where the move is noticed
  // (see SelectionClock).
  mutable SelectionClock name_clock_;
};

// The job inspect screen: the book on the left and the selected skill's card on
// its right, at least `rows` tall. `rows` is the tallest card in the book,
// which keeps the screen still as the cursor moves; a short card leaves space
// below rather than shrinking the screen.
ftxui::Element JobInspectScreen(ftxui::Element book, ftxui::Element card,
                                int rows);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_JOB_INSPECT_PANEL_H_
