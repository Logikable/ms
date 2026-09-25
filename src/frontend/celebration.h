/* The few seconds after something important happens: a card across the middle
 * of the screen, and the related panels lit gold behind it. Death uses the same
 * mechanism (one card at a time, four seconds, any key dismisses it), so it
 * lives here too and two cards can never be on screen at once.
 *
 * It is separate from Tui so its decisions can be tested: how long it lasts,
 * which panels it lights, and what the card says. Nothing here blocks, because
 * the game may be running unattended and a card waiting for a key would stall
 * it.
 *
 * The card and the gold last different lengths of time. Four seconds is enough
 * for the announcement, but the gold is there to show the player where to go.
 * So a panel the player wasn't already looking at stays gold until they visit
 * it.
 */
#ifndef MS_SRC_FRONTEND_CELEBRATION_H_
#define MS_SRC_FRONTEND_CELEBRATION_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {

// How long a celebration stays up: long enough to notice and read, short enough
// to be gone before it gets in the way.
constexpr double kCelebrationSeconds = 4.0;

class Celebration {
 public:
  enum class Kind { kNone, kLevelUp, kAdvancement, kDeath };

  // Starts the level-up card for a climb from `from_level` to `to_level`. It
  // takes a range because one tick can cross several levels, and the range
  // decides which panels light up.
  //
  // `account_level` is the highest level any character has reached. A climb
  // over levels already reached still pays AP but unlocks nothing. It also
  // decides whether honor is shown; see HonorVisible.
  void BeginLevelUp(int from_level, int to_level, int ap, int sp, int hyper_sp,
                    int account_level, Panel focused);

  // Starts the advancement card, replacing a level-up card still on screen. It
  // is the bigger news, and queueing both would make the player wait.
  void BeginAdvancement(Job from_job, Job to_job, int to_stage, Panel focused);

  // Starts the death card, replacing whatever is up, since being moved
  // elsewhere matters more than any news. It lights and clears no panels; dying
  // is no reason to remove a pointer the player hasn't followed.
  void BeginDeath();

  // Runs both clocks down by `elapsed_seconds`: the card's, and the one that
  // fades the gold on panels the player was already looking at. Safe to call
  // when nothing is up.
  void Advance(double elapsed_seconds);

  // Records that the player is looking at `focused`, clearing its gold if it
  // was waiting to be visited. This is permanent: leaving doesn't bring the
  // gold back, since it only needed to be seen once.
  void Visit(Panel focused);

  // Takes the card down for a player who has already read it. The gold stays,
  // since dismissing the card isn't the same as visiting the panel.
  void Dismiss();

  // Whether the card is on screen. The gold can outlast it, so this doesn't say
  // whether the celebration is still doing anything.
  bool card_visible() const {
    return card_seconds_ > 0.0;
  }
  Kind kind() const {
    return kind_;
  }

  // Whether `panel` should be drawn lit. It returns false once the gold is
  // gone, so the caller can set every panel from this each frame instead of
  // having to clear them.
  bool Lights(Panel panel) const;

  // The card. Only call while card_visible().
  ftxui::Element Render() const;

 private:
  // Why a panel is gold, if it is.
  enum class Glow {
    kOff,
    // The player was already on it, so it fades on the clock like the card.
    kTimed,
    // The player wasn't on it, so it waits until they visit.
    kUntilVisited,
  };

  // Lights `panel` in the way that suits where the player is.
  void Light(Panel panel, Panel focused);

  Kind kind_ = Kind::kNone;
  double card_seconds_ = 0.0;
  // Separate from card_seconds_ so dismissing the card doesn't cut a timed glow
  // short.
  double glow_seconds_ = 0.0;
  // Why each panel is lit, indexed by Panel. It is worked out when the
  // celebration starts, not every frame, because it depends on the levels
  // passed, which the character no longer knows afterwards.
  Glow glow_[kNumPanels] = {};

  int from_level_ = 0;
  int to_level_ = 0;
  int ap_ = 0;
  int sp_ = 0;
  int hyper_sp_ = 0;
  // Honor the climb paid, or 0 for a player who hasn't unlocked Inner Ability
  // yet: the card only mentions what the player can look up.
  int64_t honor_ = 0;
  // Worked out when the climb happens, for the same reason as the glow: the
  // character no longer knows which levels it passed.
  std::vector<std::string> unlocks_;
  Job from_job_ = JOB_BEGINNER;
  Job to_job_ = JOB_BEGINNER;
  int to_stage_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CELEBRATION_H_
