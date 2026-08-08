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

#include "src/character/character.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One stat as it is shown: what it is called, and its value already written
// out. The value is a string because these columns hold percentages, stage
// names and plain counts side by side.
struct StatLine {
  std::string label;
  std::string value;
};

// The combat stats, **most important first**. The Character panel drops the
// tail of this list when the terminal is too short to hold it, so a new stat
// goes where it belongs in that order rather than on the end.
std::vector<StatLine> ExtraStatLines(
    const CharacterInstance& character,
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
