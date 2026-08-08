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

// How long each character of the slide is held. One redraw of the game's own
// ticker: a shorter step would only drop frames, since nothing repaints in
// between.
constexpr std::chrono::milliseconds kMarqueeStep(300);

// How long the name is held still at each end of the slide -- long enough to
// read the head before it leaves and the tail once it arrives. Without the
// pause at the head, the first characters would be gone within one step of the
// row being selected.
constexpr std::chrono::milliseconds kMarqueePause(2000);

// `text` cut to `width` columns, padded out if it is short of them. `elapsed`
// is how long the row has been selected; pass zero for a row that is not, and
// the head of the name comes back.
//
// A name that fits is returned padded and never moves, so a column of them
// stays a column.
std::string ScrollingWindow(const std::string& text, int width,
                            std::chrono::steady_clock::duration elapsed);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_MARQUEE_H_
