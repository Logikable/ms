/* The character's stats as rows of text, in display order.
 *
 * Two screens show the same numbers: the Character panel's Stats tab and the
 * All Stats screen behind it. Both use this code so they can't disagree about a
 * stat's name or format.
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

// One stat as shown: its name and its formatted value. The value is a string
// because these columns mix percentages, stage names and counts. A line with
// `rule` set is the break between two groups, kept in the list so both screens
// draw it in the same place.
struct StatLine {
  std::string label;
  std::string value;
  bool rule = false;
};

// The break between the combat stats and the rows that aren't about fighting.
inline StatLine StatRule() {
  StatLine line;
  line.rule = true;
  return line;
}

// The combat stats, most important first, as the All Stats screen lists them.
// Put a new stat where it belongs in that order rather than at the end, since
// both callers drop the tail. `preset` picks which Hyper Stat allocation is
// read.
std::vector<StatLine> ExtraStatLines(const CharacterInstance& character,
                                     const std::map<std::string, Skill>& skills,
                                     Activity preset = Activity::kFarming);

// The same list as the Character panel shows it, which is shorter early on:
// empty until the first advancement and without the percent rows until the
// second. The All Stats screen always shows everything. The advancement can be
// any character's on `account`.
std::vector<StatLine> PanelExtraStatLines(
    const CharacterInstance& character, const AccountInstance& account,
    const std::map<std::string, Skill>& skills,
    Activity preset = Activity::kFarming);

// The four AP stats, ordered to fill the All Stats screen's left column and
// then its right, so the rows read STR/INT and DEX/LUK. A stat that gear or a
// skill has added to reads "(base+bonus) total". HP and MP are drawn as gauges.
std::vector<StatLine> MainStatLines(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    Activity preset = Activity::kFarming);

// "Combat Power" and the number, shortened to "CP" once the number passes six
// digits so the number isn't cut.
std::string CombatPowerText(int power);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_STAT_ROWS_H_
