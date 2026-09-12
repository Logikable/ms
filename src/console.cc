#include "src/console.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>

#include <cstdio>
#endif

namespace ms {
#ifdef _WIN32
namespace {

// Whether the console window closes when this process does. Explorer makes a
// console for a program it launches and nothing else joins it, so being the
// only process attached means the window is ours and dies with us; a shell
// that started us is attached too, and its window stays.
bool ConsoleDiesWithUs() {
  // Failure reports zero, which counts as ours: a keypress nobody needed costs
  // a second, while a window that vanishes costs the player the reason the
  // game would not start.
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
  // Without this the key that quit the game can still be sitting in the input
  // buffer, and the window shuts on a press the player never made.
  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
  _getch();
#endif
}

}  // namespace ms
