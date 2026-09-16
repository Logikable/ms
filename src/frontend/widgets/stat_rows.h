/* The character's stats as rows of text, in the order they are shown.
 *
 * Two screens draw the same numbers -- the Character panel's Stats tab and the
 * All Stats screen behind it -- and they must not be able to disagree about
 * what a stat is called or how it is written. Both ask here.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_STAT_ROWS_H_
#define MS_SRC_FRONTEND_WIDGETS_STAT_ROWS_H_

#include <map>
#include <string>
#include <vector>

#include "src/account.h"
#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One stat as it is shown: its name and its value already written out, the
// value being a string because these columns hold percentages, stage names and
// counts side by side. A line with `rule` set is the BREAK between two groups,
// held in the list so both screens draw it in the same place.
struct StatLine {
  std::string label;
  std::string value;
  bool rule = false;
};

// The break between the combat stats and the three that are not about a fight.
inline StatLine StatRule() {
  StatLine line;
  line.rule = true;
  return line;
}

// The combat stats, **most important first** -- the All Stats screen's list. A
// NEW STAT GOES WHERE IT BELONGS in that order rather than on the end: both
// callers drop the tail. `preset` picks which Hyper Stat allocation is read.
std::vector<StatLine> ExtraStatLines(const CharacterInstance& character,
                                     const std::map<std::string, Skill>& skills,
                                     Activity preset = Activity::kFarming);

// The same list as the Character panel shows it, which is less of it early on:
// empty until a first advancement and without the percent rows until a second.
// The panel earns its numbers; the All Stats screen has them all. The
// advancement can be any character's on `account`.
std::vector<StatLine> PanelExtraStatLines(
    const CharacterInstance& character, const AccountInstance& account,
    const std::map<std::string, Skill>& skills,
    Activity preset = Activity::kFarming);

// The four AP stats. The order fills the All Stats screen's left column and
// then its right, so the rows read STR/INT and DEX/LUK. One gear or a skill
// has added to reads "(base+bonus) total". HP and MP are drawn as gauges.
std::vector<StatLine> MainStatLines(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    Activity preset = Activity::kFarming);

// Combat power spelled out, until it outgrows the row it sits in. Past six
// figures the label shortens to "CP" rather than the number being cut.
std::string CombatPowerText(int power);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_STAT_ROWS_H_
