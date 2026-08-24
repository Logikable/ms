#include "src/frontend/widgets/marquee.h"

#include <chrono>
#include <string>

#include "src/frontend/widgets/text_columns.h"

namespace ms {

namespace {

// How far into the name the window has slid, given how long the row has been
// selected.
//
// Every offset is held for one step except the two ends, which are held for
// the pause instead: the head so it can be read before it goes, the tail so
// the answer the player was waiting for does not flick past. So the slide
// itself is the offsets between them -- one fewer than the number of steps.
int OffsetAt(int steps, std::chrono::steady_clock::duration elapsed) {
  std::chrono::milliseconds ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
  if (ms < std::chrono::milliseconds::zero()) {
    return 0;  // a clock that ran backwards shows the head, not a crash
  }
  std::chrono::milliseconds slide = kMarqueeStep * (steps - 1);
  std::chrono::milliseconds cycle = kMarqueePause * 2 + slide;
  std::chrono::milliseconds into(ms.count() % cycle.count());
  if (into < kMarqueePause) {
    return 0;
  }
  into -= kMarqueePause;
  if (into >= slide) {
    return steps;  // the tail pause, spent at the end
  }
  return 1 + static_cast<int>(into / kMarqueeStep);
}

}  // namespace

void SelectionClock::Follow(int key) {
  if (key == key_) {
    return;
  }
  key_ = key;
  since_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::duration SelectionClock::Elapsed() const {
  return std::chrono::steady_clock::now() - since_;
}

std::string ScrollingWindow(const std::string& text, int width,
                            std::chrono::steady_clock::duration elapsed) {
  if (width <= 0) {
    return "";
  }
  int columns = TextColumns(text);
  if (columns <= width) {
    return ColumnWindow(text, 0, width);
  }
  return ColumnWindow(text, OffsetAt(columns - width, elapsed), width);
}

}  // namespace ms
