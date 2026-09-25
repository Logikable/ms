/* HyperStatInspectPanel is the Hyper tab's version of SkillInspectPanel: one
 * stat's name and maximum level, then its value at the current level and what
 * the next level would give, priced in points. A stat with nothing spent shows
 * only the second block, and one at its maximum only the first.
 *
 * Every card is the same width, measured from the widest name and value, so
 * moving through the list doesn't resize the window. It is much narrower than a
 * skill card, since it shows only one number.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_HYPER_STAT_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_HYPER_STAT_INSPECT_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

class HyperStatInspectPanel {
 public:
  // Sets the card's stat, the level the displayed allocation has, and the
  // maximum this character's job stage allows.
  void SetStat(HyperStatField field, int level, int max_level);

  ftxui::Element Render() const;

  // The width the card takes, borders included. The same for every stat.
  static int Columns();

 private:
  HyperStatField field_ = HYPER_STAT_FIELD_UNSPECIFIED;
  int level_ = 0;
  int max_level_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HYPER_STAT_INSPECT_PANEL_H_
