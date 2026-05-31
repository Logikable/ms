/* TUI: interactive terminal UI using ftxui. RunTui owns the main event loop
 * and does not return until the user exits (Ctrl-C or q).
 */
#ifndef MS_SRC_TUI_H_
#define MS_SRC_TUI_H_

#include <random>
#include <vector>

#include "src/character.h"
#include "src/protos/scroll.pb.h"

namespace ms {

void RunTui(CharacterInstance& character,
            const std::vector<Scroll>& scrolls,
            std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_TUI_H_
