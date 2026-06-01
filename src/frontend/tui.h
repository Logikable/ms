/* RunTui launches the interactive TUI. Blocks until the user exits (Ctrl-C).
 * Owns the ftxui ScreenInteractive event loop.
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include "src/game_state.h"

namespace ms {

void RunTui(GameState& state);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
