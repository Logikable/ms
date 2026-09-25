/* The gold box in the bottom-right corner, for something the player must be
 * told wherever they are and whatever screen they are on.
 *
 * Only one box is shown at a time. A new one replaces the current one instead
 * of stacking, so the corner never fills with old news.
 *
 * It goes away only once four seconds have passed and the player has pressed a
 * key since it appeared. The clock alone would remove it while the player was
 * away from the keyboard, and a key alone would remove it on whatever key they
 * happened to be pressing when it arrived.
 */
#ifndef MS_SRC_FRONTEND_NOTIFICATION_BOX_H_
#define MS_SRC_FRONTEND_NOTIFICATION_BOX_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

// How long the box stays before a keypress can remove it. It matches the
// celebration's four seconds.
inline constexpr double kNotificationSeconds = 4.0;

class NotificationBox {
 public:
  // Shows `lines`, one per row, replacing what was there. The caller breaks the
  // text, since the box is small and nothing here wraps.
  void Raise(std::vector<std::string> lines);
  // Runs the clock down by `elapsed_seconds`. Safe to call when nothing is up.
  void Advance(double elapsed_seconds);
  // Records that the player has pressed a key since the box appeared. Every key
  // counts except the ticker's own redraw event, which isn't the player.
  void Touch();
  // Removes the box regardless of its clock, for a screen that has moved the
  // player somewhere the news no longer applies.
  void Dismiss();
  ftxui::Element Render() const;

  bool visible() const;

 private:
  std::vector<std::string> lines_;
  double seconds_ = 0.0;
  // Whether a key has been pressed since this box appeared.
  bool touched_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_NOTIFICATION_BOX_H_
