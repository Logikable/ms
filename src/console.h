/* The console window the game is running in, on the two paths it gets one.
 *
 * A player on Windows opens the game from Explorer, which makes a console for
 * it and destroys that console the moment the process ends -- taking any
 * parting message with it. Naming that window and holding it open is what the
 * launcher script beside the executable used to do, and it belongs here: a
 * release is one file now.
 *
 * Neither call does anything off Windows, where a terminal outlives the
 * program that ran in it.
 */
#ifndef MS_SRC_CONSOLE_H_
#define MS_SRC_CONSOLE_H_

namespace ms {

// Names the console window the game is running in.
void NameConsoleWindow();

// Holds the window open at exit when closing it would take the screen with
// it. Declare one in main above anything that can return: a game opened from
// a file manager waits for a keypress, and one run from a terminal -- whose
// window stays either way -- does not.
class ConsoleHold {
 public:
  ConsoleHold() = default;
  ConsoleHold(const ConsoleHold&) = delete;
  ConsoleHold& operator=(const ConsoleHold&) = delete;
  ~ConsoleHold();
};

}  // namespace ms

#endif  // MS_SRC_CONSOLE_H_
