/* A name too long for the column it sits in, read a window at a time.
 *
 * A row the player can select has a fixed width, and a name longer than that
 * has to lose its tail. Cutting it silently costs the player the one thing the
 * row is for; so the name is cut while the row sits there, and slides under
 * the column while the row is selected, which is when the player is asking
 * what it says.
 *
 * Nothing here holds state. The caller owns the clock -- how long the row has
 * been selected -- because the caller is what knows when the selection moved,
 * and because a pure function is the same function for a skill row, an
 * inventory row, or anything else that comes along.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_
#define MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_

#include <chrono>
#include <string>

namespace ms {

// How long each character of the slide is held.
//
// This is also what the game repaints at -- Tui's ticker sleeps for exactly
// this -- because a step finer than the redraw cannot be seen: the window
// would jump two characters at once instead of sliding. Shortening it here
// speeds the redraw with it; the tick that rides on the redraw is driven by
// elapsed time, so it does not care how often it is asked.
constexpr std::chrono::milliseconds kMarqueeStep(150);

// How long the name is held still at each end of the slide -- long enough to
// read the head before it leaves and the tail once it arrives. Without the
// pause at the head, the first characters would be gone within one step of the
// row being selected.
constexpr std::chrono::milliseconds kMarqueePause(1000);

// `text` cut to `width` columns, padded out if it is short of them. `elapsed`
// is how long the row has been selected; pass zero for a row that is not, and
// the head of the name comes back.
//
// A name that fits is returned padded and never moves, so a column of them
// stays a column.
std::string ScrollingWindow(const std::string& text, int width,
                            std::chrono::steady_clock::duration elapsed);

// How long the selection has sat where it is, for feeding ScrollingWindow.
//
// A panel that rebuilds its rows every render cannot hook the keypress that
// moved the cursor -- an ftxui::Menu writes the index behind its back -- so
// the move is noticed by watching the index instead.
class SelectionClock {
 public:
  // Call once per render with whatever identifies the selected row -- its
  // index, or the index folded together with the page it is on, so that the
  // same row of another page counts as a different row -- and whether that row
  // is drawn as selected at all. A panel the cursor has left is not being
  // read, so its clock holds at zero; focus coming back starts the name over
  // from its head, as stepping onto a row does.
  void Follow(int key, bool focused = true);

  // Zero at the moment the selection arrived, growing from there, and zero
  // for as long as the row is not selected. Pass it for the selected row and
  // `duration::zero()` for the rest, which is what shows every other name
  // from its head.
  std::chrono::steady_clock::duration Elapsed() const;

 private:
  int key_ = -1;
  bool focused_ = true;
  std::chrono::steady_clock::time_point since_ =
      std::chrono::steady_clock::now();
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_
