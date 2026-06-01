#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include <random>
#include <vector>

#include "src/character.h"
#include "src/protos/scroll.pb.h"

namespace ms {

void RunTui(CharacterInstance& character,
            const std::vector<Scroll>& scrolls,
            std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
