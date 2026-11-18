/* Scrolls a name that is too long for its column.
 *
 * A selectable row has a fixed width, and a longer name has to be cut. Cutting
 * it for good would hide what the row is for, so the name is cut while the row
 * is idle and scrolls while the row is selected, which is when the player wants
 * to read it.
 *
 * Nothing here holds state. The caller keeps the clock (how long the row has
 * been selected), because the caller knows when the selection moved, and a pure
 * function works the same for a skill row, an inventory row or anything else.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_
#define MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_

#include <chrono>
#include <string>

namespace ms {

// How long each scroll step lasts, and how often the game repaints. A step
// shorter than the redraw can't be seen: the window would jump two characters
// instead of scrolling. Shortening this also speeds up the redraw.
constexpr std::chrono::milliseconds kMarqueeStep(150);

// How long the name stays still at each end, long enough to read the start
// before it moves and the end once it arrives. Without the pause at the start,
// the first characters would disappear one step after the row is selected.
constexpr std::chrono::milliseconds kMarqueePause(1000);

// `text` cut to `width` columns, padded if short. `elapsed` is how long the row
// has been selected, and zero returns the start of the name. A name that fits
// never moves, so a column of them stays aligned.
std::string ScrollingWindow(const std::string& text, int width,
                            std::chrono::steady_clock::duration elapsed);

// How long the selection has been on the same row, for ScrollingWindow. A panel
// that rebuilds its rows every render can't hook the keypress that moved the
// cursor, so it notices the move by watching the index.
class SelectionClock {
 public:
  // Call once per render with a key for the selected row and whether it is
  // drawn selected. The key is the index, or the index combined with its page
  // so the same row on another page counts as different. An unfocused panel
  // keeps its clock at zero, so the name starts over when focus returns.
  void Follow(int key, bool focused = true);

  // Zero when the selection arrives, growing from there, and zero while the row
  // isn't selected. Pass it for the selected row and `duration::zero()` for the
  // rest, so every other name shows from its start.
  std::chrono::steady_clock::duration Elapsed() const;

 private:
  int key_ = -1;
  bool focused_ = true;
  std::chrono::steady_clock::time_point since_ =
      std::chrono::steady_clock::now();
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_
