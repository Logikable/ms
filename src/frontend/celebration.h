/* The few seconds after something good happens: the card in the middle of the
 * screen, and the panels lit gold behind it.
 *
 * Kept apart from Tui so the decisions in it can be tested -- how long the
 * moment lasts, which panels it points at, what the card says -- while Tui is
 * left with the wiring: noticing the change, ticking the clock, and drawing
 * the result.
 *
 * Nothing here blocks. A celebration is something the player is shown, never
 * something they have to dismiss: the game is idle and may be running
 * unattended, and a card that waited for a keypress would stall it and stack
 * up behind itself. It expires on its own, and any key gets rid of it early.
 */
#ifndef MS_SRC_FRONTEND_CELEBRATION_H_
#define MS_SRC_FRONTEND_CELEBRATION_H_

#include "ftxui/dom/elements.hpp"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {

// How long a celebration stays up. Long enough to be caught out of the corner
// of an eye and read, short enough that it is gone before it is in the way.
constexpr double kCelebrationSeconds = 4.0;

class Celebration {
 public:
  enum class Kind { kNone, kLevelUp, kAdvancement };

  // Starts the level-up card for a climb from `from_level` to `to_level`,
  // paying `ap` and `sp` in total.
  //
  // Takes the whole span rather than one level because a single combat tick
  // can carry a character past more than one threshold, and because the span
  // is what decides which panels are lit: a jump from 2 to 5 opened both the
  // equipped panel and the bag, and both should be pointed at.
  void BeginLevelUp(int from_level, int to_level, int ap, int sp);

  // Starts the advancement card. Replaces a level-up still on screen -- taking
  // an advancement is the larger news, and stacking the two would leave the
  // second waiting behind the first for something the player never asked for.
  void BeginAdvancement(Job from_job, Job to_job);

  // Runs the clock down by `elapsed_seconds`, ending the celebration when it
  // runs out. Safe to call when nothing is up.
  void Advance(double elapsed_seconds);

  // Ends it now, for a player who has already looked.
  void Dismiss();

  bool active() const {
    return kind_ != Kind::kNone;
  }
  Kind kind() const {
    return kind_;
  }

  // Whether `panel` should be drawn lit. False for everything once the
  // celebration is over, so the caller can set every panel from this every
  // frame rather than remembering to put them out.
  bool Lights(Panel panel) const;

  // The card. Only call while active().
  ftxui::Element Render() const;

 private:
  Kind kind_ = Kind::kNone;
  double remaining_seconds_ = 0.0;
  // Which panels are lit, indexed by Panel. Worked out when the celebration
  // begins rather than on every frame: it depends on the levels climbed
  // through, which is not something the character still knows afterwards.
  bool lit_[kNumPanels] = {};

  int from_level_ = 0;
  int to_level_ = 0;
  int ap_ = 0;
  int sp_ = 0;
  Job from_job_ = JOB_BEGINNER;
  Job to_job_ = JOB_BEGINNER;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CELEBRATION_H_
