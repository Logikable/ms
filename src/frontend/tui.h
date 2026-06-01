/* RunTui launches the interactive TUI. Blocks until the user exits (Ctrl-C).
 * Owns the ftxui ScreenInteractive event loop.
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include "src/character.h"

namespace ms {

void RunTui(CharacterInstance& character);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
