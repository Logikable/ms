/* The console window the game runs in, in the two cases where it gets one.
 *
 * A Windows player opens the game from Explorer, which creates a console for it
 * and destroys it as soon as the process ends, along with any final message.
 * Naming that window and keeping it open is done here so a release is a single
 * file.
 *
 * Neither call does anything outside Windows, where the terminal outlives the
 * program.
 */
#ifndef MS_SRC_CONSOLE_H_
#define MS_SRC_CONSOLE_H_

namespace ms {

// Names the console window the game is running in.
void NameConsoleWindow();

// Keeps the window open at exit when closing it would lose the screen. Declare
// one in main before anything that can return: a game opened from a file
// manager waits for a keypress, and one run from a terminal (whose window stays
// open anyway) doesn't.
class ConsoleHold {
 public:
  ConsoleHold() = default;
  ConsoleHold(const ConsoleHold&) = delete;
  ConsoleHold& operator=(const ConsoleHold&) = delete;
  ~ConsoleHold();
};

}  // namespace ms

#endif  // MS_SRC_CONSOLE_H_
