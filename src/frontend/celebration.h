/* The few seconds after something good happens: the banner across the middle of
 * the screen, and the panels lit gold behind it.
 *
 * Kept apart from Tui so the decisions in it can be tested -- how long the
 * moment lasts, which panels it points at, what the banner says -- while Tui is
 * left with the wiring: noticing the change, ticking the clock, and drawing
 * the result.
 *
 * Nothing here blocks. A celebration is something the player is shown, never
 * something they have to dismiss: the game is idle and may be running
 * unattended, and a banner that waited for a keypress would stall it and stack
 * up behind itself. It expires on its own, and any key gets rid of it early.
 *
 * The banner and the gold have separate lives. The banner is an announcement
 * and four seconds is plenty of one. The gold is a signpost, and a signpost
 * that takes itself down before anybody walked past it has not done its job --
 * so a panel the player was not already looking at holds its gold until they go
 * and look. Only a panel that was in front of them the whole time fades on the
 * clock, having been seen by definition.
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

  // Starts the level-up banner for a climb from `from_level` to `to_level`,
  // paying `ap` and `sp` in total. `focused` is the panel the player is on at
  // that moment, or kNoPanel when they are somewhere other than the main
  // screen.
  //
  // Takes the whole span rather than one level because a single combat tick
  // can carry a character past more than one threshold, and because the span
  // is what decides which panels are lit: a jump from 2 to 5 opened both the
  // equipped panel and the bag, and both should be pointed at.
  void BeginLevelUp(int from_level, int to_level, int ap, int sp,
                    Panel focused);

  // Starts the advancement banner. Replaces a level-up still on screen --
  // taking an advancement is the larger news, and stacking the two would leave
  // the second waiting behind the first for something the player never asked
  // for.
  void BeginAdvancement(Job from_job, Job to_job, Panel focused);

  // Runs both clocks down by `elapsed_seconds`: the banner's, and the one the
  // panels already in front of the player fade on. Safe to call when nothing
  // is up.
  void Advance(double elapsed_seconds);

  // Records that the player is now looking at `focused`, putting out its gold
  // if it was waiting to be visited. kNoPanel for a screen with no panel on
  // it, which visits nothing.
  //
  // Latches: leaving again does not bring the gold back, because the point of
  // it was to be seen once and it has been.
  void Visit(Panel focused);

  // Takes the banner down, for a player who has already read it. Leaves the
  // gold alone -- getting a banner out of the way is not the same as having
  // gone to look at what it was pointing at.
  void Dismiss();

  // Whether the banner is on screen. The gold outlives it, so this is not the
  // question of whether a celebration is still doing anything.
  bool banner_visible() const {
    return banner_seconds_ > 0.0;
  }
  Kind kind() const {
    return kind_;
  }

  // Whether `panel` should be drawn lit. False for everything once its gold is
  // spent, so the caller can set every panel from this every frame rather than
  // remembering to put them out.
  bool Lights(Panel panel) const;

  // The banner. Only call while banner_visible().
  ftxui::Element Render() const;

 private:
  // What is keeping a panel gold, if anything.
  enum class Glow {
    kOff,
    // The player was already on it, so they have seen it: it fades on the
    // clock like the banner does.
    kTimed,
    // They were not, so it waits however long it takes.
    kUntilVisited,
  };

  // Lights `panel` in whichever way suits where the player is standing.
  void Light(Panel panel, Panel focused);

  Kind kind_ = Kind::kNone;
  double banner_seconds_ = 0.0;
  // Kept apart from banner_seconds_ so that dismissing the banner does not cut
  // a timed glow short with it.
  double glow_seconds_ = 0.0;
  // What is lighting each panel, indexed by Panel. Worked out when the
  // celebration begins rather than on every frame: it depends on the levels
  // climbed through, which is not something the character still knows
  // afterwards.
  Glow glow_[kNumPanels] = {};

  int from_level_ = 0;
  int to_level_ = 0;
  int ap_ = 0;
  int sp_ = 0;
  Job from_job_ = JOB_BEGINNER;
  Job to_job_ = JOB_BEGINNER;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CELEBRATION_H_
