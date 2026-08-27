/* JobInspectPanel is what a player reads before taking an advancement: the
 * weapon the job is built around, and its whole book of skills. It stands to
 * the left of a SkillInspectPanel showing whichever skill the cursor is on, so
 * the two together answer "what would I become".
 *
 * Read-only. Up and Down walk the skills and nothing else on the screen takes
 * a key -- the advancement itself is taken from the menu this screen opened
 * from, so there is one path to it and it still confirms.
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

// The book's whole width, borders included. The screen beside it reads this to
// know what room the skill card has.
inline constexpr int kJobInspectBookWidth = 35;

class JobInspectPanel {
 public:
  // `skills` is the loaded skill catalog (keyed by file stem). Copied because
  // the catalog is fixed after load, as the Character panel's is.
  explicit JobInspectPanel(std::map<std::string, Skill> skills = {});

  // Opens the panel on `job`, cursor back at the top of its book.
  void SetJob(Job job);
  Job job() const {
    return job_;
  }

  // The skill under the cursor, or null for a job whose book is empty -- which
  // is a job whose skills nobody has written yet, not a state to design for.
  const Skill* selected_skill() const;

  // Walks the book, wrapping at both ends.
  void MoveCursor(int delta);

  // The job's book, in the order the Character panel lists it. Public because
  // the screen measures the cards of all of them before drawing any one.
  std::vector<const Skill*> Skills() const;

  ftxui::Element Render() const;

 private:
  // One skill row: the kind tag, the name, and the level it tops out at.
  ftxui::Element RenderSkillRow(const Skill& skill, int index) const;

  std::map<std::string, Skill> skills_;
  Job job_ = JOB_UNSPECIFIED;
  int selected_ = 0;
  // How long the cursor has sat where it is, for sliding a long name under
  // its column. Mutable because it is written by the render, which is where
  // the move is noticed -- see SelectionClock.
  mutable SelectionClock name_clock_;
};

// The job inspect screen: the book on the left, the card of whichever skill
// the cursor is on to its right, the pair held to at least `rows` tall. Split
// out of Tui so a test can measure it.
//
// `rows` is the tallest card in the book, which is what keeps the screen still
// as the cursor walks it -- a short card leaves room below itself rather than
// pulling the whole screen up. A book taller than its cards still gets its
// full height.
ftxui::Element JobInspectScreen(ftxui::Element book, ftxui::Element card,
                                int rows);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_JOB_INSPECT_PANEL_H_
