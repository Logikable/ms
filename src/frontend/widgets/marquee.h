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

// How long each character of the slide is held, and what the game repaints at:
// a step finer than the redraw cannot be seen, the window jumping two
// characters instead of sliding. Shortening it speeds the redraw with it.
constexpr std::chrono::milliseconds kMarqueeStep(150);

// How long the name is held still at each end of the slide -- long enough to
// read the head before it leaves and the tail once it arrives. Without the
// pause at the head, the first characters would be gone within one step of the
// row being selected.
constexpr std::chrono::milliseconds kMarqueePause(1000);

// `text` cut to `width` columns and padded if short. `elapsed` is how long the
// row has been selected; zero returns the head of the name. A name that fits
// never moves, so a column of them stays a column.
std::string ScrollingWindow(const std::string& text, int width,
                            std::chrono::steady_clock::duration elapsed);

// How long the selection has sat where it is, for ScrollingWindow. A panel
// rebuilding its rows every render cannot hook the keypress that moved the
// cursor, so the move is noticed by WATCHING the index.
class SelectionClock {
 public:
  // Once per render, with whatever identifies the selected row -- the index,
  // or the index folded with its page so the same row elsewhere counts as a
  // different one -- and whether it is drawn selected. An unfocused panel
  // holds its clock at zero, so focus coming back starts the name over.
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
