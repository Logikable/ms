/* Inv command: /inv
 * Displays equipped items with their stats. inv.cc implements InvCommand and
 * RegisterInvCommand.
 */
#ifndef MS_COMMANDS_INV_H_
#define MS_COMMANDS_INV_H_

#include <string>
#include <vector>

#include "src/character.h"
#include "src/frontend.h"

namespace ms {

// Returns equipped items and bag contents. Returns "Nothing to show." if both
// are empty.
// TODO: replace full stat block with a one-line summary; ncurses hover to expand.
std::string InvCommand(const CharacterInstance& character);
void RegisterInvCommand(Frontend& frontend, CharacterInstance& character);

}  // namespace ms

#endif  // MS_COMMANDS_INV_H_
