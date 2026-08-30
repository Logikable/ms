/* HyperStatInspectPanel is the Hyper tab's counterpart to SkillInspectPanel:
 * one stat's name and ceiling, then what it is worth at the level it is at and
 * what the next level would buy, priced in points. A stat with nothing spent
 * on it has only the second block, and one at its ceiling only the first.
 *
 * Every card is the same width, measured from the widest name and value in the
 * roster, so walking the list does not resize the window under the cursor. It
 * is far narrower than a skill card -- there is one number to state.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_HYPER_STAT_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_HYPER_STAT_INSPECT_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

class HyperStatInspectPanel {
 public:
  // Seeds the card: which stat, the level the allocation on screen has it at,
  // and the ceiling this character's job stage allows.
  void SetStat(HyperStatField field, int level, int max_level);

  ftxui::Element Render() const;

  // The columns the card takes, borders included. The same for every stat.
  static int Columns();

 private:
  HyperStatField field_ = HYPER_STAT_FIELD_UNSPECIFIED;
  int level_ = 0;
  int max_level_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HYPER_STAT_INSPECT_PANEL_H_
