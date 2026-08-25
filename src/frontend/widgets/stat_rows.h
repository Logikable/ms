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
#include "src/protos/skill.pb.h"

namespace ms {

// One stat as it is shown: what it is called, and its value already written
// out. The value is a string because these columns hold percentages, stage
// names and plain counts side by side.
//
// A line with `rule` set is not a stat at all but the break between two groups
// of them. Held in the list rather than left to each screen so that both draw
// it in the same place; what it looks like is theirs to decide.
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

// The combat stats, **most important first**. Every one the character has, so
// this is the All Stats screen's list. A new stat goes where it belongs in
// that order rather than on the end -- both callers drop the tail.
std::vector<StatLine> ExtraStatLines(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills);

// The same list as the Character panel shows it, which is less of it early on:
// empty until a first job advancement, and without the four percent rows until
// a second. The panel is one column on a busy screen, so it earns its numbers;
// the All Stats screen is where all of them always are.
//
// The advancement can be any character's on `account`, which is what opens the
// rows for a second character from level 1.
std::vector<StatLine> PanelExtraStatLines(
    const CharacterInstance& character, const AccountInstance& account,
    const std::map<std::string, Skill>& skills);

// The pools and the four AP stats: HP, MP, STR, INT, DEX, LUK. That order
// pairs them the way the All Stats screen reads them, two to a row. An AP stat
// gear or a skill has added to reads "(base+bonus) total".
std::vector<StatLine> MainStatLines(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills);

// What the character's whole stat line comes to, with no attack skill and no
// target -- combat power stands for the character rather than for a swing.
int CharacterCombatPower(const CharacterInstance& character,
                         const std::map<std::string, Skill>& skills);

// Combat power spelled out, until it outgrows the row it sits in. Past six
// figures the label shortens to "CP" rather than the number being cut.
std::string CombatPowerText(int power);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_STAT_ROWS_H_
