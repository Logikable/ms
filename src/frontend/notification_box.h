/* The gold box in the bottom-right corner: something the player has to be
 * told wherever they are, and whatever screen they are on.
 *
 * One box at a time. A new one replaces what is up rather than stacking under
 * it, so the corner never grows a column of stale news.
 *
 * It goes away only once four seconds have passed AND the player has pressed
 * a key since it appeared -- both, not either. The clock alone would take it
 * down in front of nobody while they were away from the keyboard, and a key
 * alone would lose it to whatever they happened to be pressing as it arrived.
 */
#ifndef MS_SRC_FRONTEND_NOTIFICATION_BOX_H_
#define MS_SRC_FRONTEND_NOTIFICATION_BOX_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

// How long the box stands before a keypress can take it down. The
// celebration's four seconds: long enough to be caught out of the corner of
// an eye and read.
inline constexpr double kNotificationSeconds = 4.0;

class NotificationBox {
 public:
  // Puts `lines` up, one per row, replacing whatever was there. The caller
  // breaks the text: the box is small and nothing here wraps.
  void Raise(std::vector<std::string> lines);
  // Runs the clock down by `elapsed_seconds`. Safe to call when nothing is
  // up.
  void Advance(double elapsed_seconds);
  // Records that the player has pressed a key since the box went up. Every
  // key counts except the ticker's own redraw, which is not the player.
  void Touch();
  // Takes the box down whatever its clock says, for a screen that has taken
  // the player somewhere the news no longer belongs.
  void Dismiss();
  ftxui::Element Render() const;

  bool visible() const;

 private:
  std::vector<std::string> lines_;
  double seconds_ = 0.0;
  // Whether a key has been pressed since this box went up.
  bool touched_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_NOTIFICATION_BOX_H_
