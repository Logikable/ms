/* Scroll command: /scroll [index]
 * With no args, lists scrolls applicable to the equipped primary weapon.
 * With an index, applies that scroll from the list.
 */
#ifndef MS_COMMANDS_SCROLL_H_
#define MS_COMMANDS_SCROLL_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/character.h"
#include "src/frontend.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// Lists scrolls applicable to the equipped primary weapon.
std::string ScrollListCommand(const CharacterInstance& character,
                               const std::map<std::string, Scroll>& scrolls);

// Applies the scroll at the given index from the applicable list.
std::string ScrollApplyCommand(CharacterInstance& character,
                                const std::map<std::string, Scroll>& scrolls,
                                int index, std::mt19937& rng);

void RegisterScrollCommand(Frontend& frontend, CharacterInstance& character,
                           const std::map<std::string, Scroll>& scrolls,
                           std::mt19937& rng);

}  // namespace ms

#endif  // MS_COMMANDS_SCROLL_H_
