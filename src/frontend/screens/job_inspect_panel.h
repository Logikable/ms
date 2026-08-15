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
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

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

  ftxui::Element Render() const;

 private:
  // The job's book, in the order the Character panel lists it.
  std::vector<const Skill*> Skills() const;
  // One skill row: the kind tag, the name, and the level it tops out at.
  ftxui::Element RenderSkillRow(const Skill& skill, int index) const;

  std::map<std::string, Skill> skills_;
  Job job_ = JOB_UNSPECIFIED;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_JOB_INSPECT_PANEL_H_
