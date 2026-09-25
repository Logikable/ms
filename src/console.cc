#include "src/console.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>

#include <cstdio>
#endif

namespace ms {
#ifdef _WIN32
namespace {

// Whether the console window closes when this process exits. Explorer creates a
// console for a program it launches and nothing else attaches to it, so being
// the only attached process means the window is ours and closes with us. A
// shell that started us is attached too, and its window stays open.
bool ConsoleDiesWithUs() {
  // On failure this reports zero, which counts as ours. An unneeded keypress
  // costs a second, while a window that vanishes hides why the game didn't
  // start.
  DWORD pids[4];
  return GetConsoleProcessList(pids, 4) != 1;
}

}  // namespace
#endif

void NameConsoleWindow() {
#ifdef _WIN32
  SetConsoleTitleW(L"MapleStory");
#endif
}

ConsoleHold::~ConsoleHold() {
#ifdef _WIN32
  if (!ConsoleDiesWithUs()) {
    return;
  }
  std::printf("\nPress any key to close this window.\n");
  std::fflush(stdout);
  // Otherwise the key that quit the game may still be in the input buffer, and
  // the window would close on a press the player never made.
  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
  _getch();
#endif
}

}  // namespace ms
