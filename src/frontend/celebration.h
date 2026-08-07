/* The few seconds after something worth stopping for happens: the card across
 * the middle of the screen, and the panels lit gold behind it. Death rides the
 * same mechanism -- one card at a time, four seconds, any key dismisses it --
 * so it lives here rather than in a second copy that could put a card on
 * screen beside this one.
 *
 * Kept apart from Tui so the decisions can be tested: how long the moment
 * lasts, which panels it points at, what the card says. Nothing here blocks --
 * the game may be running unattended, and a card that waited for a keypress
 * would stall it.
 *
 * The card and the gold have separate lives. Four seconds is plenty of an
 * announcement; a signpost that took itself down before anybody walked past it
 * has not done its job. So a panel the player was not already looking at holds
 * its gold until they go and look.
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
  enum class Kind { kNone, kLevelUp, kAdvancement, kDeath };

  // Starts the level-up card for a climb from `from_level` to `to_level`,
  // paying `ap` and `sp` in total. `focused` is the panel the player is on, or
  // kNoPanel if they are off the main screen.
  //
  // A span rather than one level: a single tick can carry a character past
  // several thresholds, and the span is what decides which panels are lit.
  void BeginLevelUp(int from_level, int to_level, int ap, int sp,
                    Panel focused);

  // Starts the advancement card, replacing a level-up still on screen: it is
  // the larger news, and stacking the two would make the player wait.
  void BeginAdvancement(Job from_job, Job to_job, Panel focused);

  // Starts the death card, replacing whatever is up: being picked up and put
  // somewhere else outranks any news still being read. Lights nothing, and
  // puts nothing out -- dying is no reason to take a signpost down.
  void BeginDeath();

  // Runs both clocks down by `elapsed_seconds`: the card's, and the one the
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

  // Takes the card down, for a player who has already read it. Leaves the
  // gold alone -- getting a card out of the way is not the same as having
  // gone to look at what it was pointing at.
  void Dismiss();

  // Whether the card is on screen. The gold outlives it, so this is not the
  // question of whether a celebration is still doing anything.
  bool card_visible() const {
    return card_seconds_ > 0.0;
  }
  Kind kind() const {
    return kind_;
  }

  // Whether `panel` should be drawn lit. False for everything once its gold is
  // spent, so the caller can set every panel from this every frame rather than
  // remembering to put them out.
  bool Lights(Panel panel) const;

  // The card. Only call while card_visible().
  ftxui::Element Render() const;

 private:
  // What is keeping a panel gold, if anything.
  enum class Glow {
    kOff,
    // The player was already on it, so they have seen it: it fades on the
    // clock like the card does.
    kTimed,
    // They were not, so it waits however long it takes.
    kUntilVisited,
  };

  // Lights `panel` in whichever way suits where the player is standing.
  void Light(Panel panel, Panel focused);

  Kind kind_ = Kind::kNone;
  double card_seconds_ = 0.0;
  // Kept apart from card_seconds_ so that dismissing the card does not cut
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
