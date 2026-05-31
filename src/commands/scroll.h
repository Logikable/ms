/* Scroll command: /scroll <scroll_name>
 * Applies a scroll to the equipped primary weapon. scroll.cc implements
 * ScrollCommand and RegisterScrollCommand.
 */
#ifndef MS_COMMANDS_SCROLL_H_
#define MS_COMMANDS_SCROLL_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/character.h"
#include "src/frontend.h"
#include "src/scroll.pb.h"

namespace ms {

// Applies the named scroll to the equipped primary weapon. Returns result string.
std::string ScrollCommand(CharacterInstance& character,
                          const std::map<std::string, Scroll>& scrolls,
                          const std::string& scroll_name, std::mt19937& rng);
void RegisterScrollCommand(Frontend& frontend, CharacterInstance& character,
                           const std::map<std::string, Scroll>& scrolls,
                           std::mt19937& rng);

}  // namespace ms

#endif  // MS_COMMANDS_SCROLL_H_
