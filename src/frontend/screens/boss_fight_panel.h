/* The boss fight screen: one bar per monster around the player in the middle,
 * the fight's own HP bar across the top, and the clock under it.
 *
 * A view over a BossRun and nothing else. The run holds every clock and every
 * bar; this decides where they go. Which side of the player a bar sits on is
 * fixed when the phase starts, so a bar never moves while the fight is on --
 * a monster that dies leaves its slot empty rather than closing the gap.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"

namespace ms {

// How wide every panel in the arena is drawn: the monsters' bars and the
// player's alike, so the ring is square whatever is standing in it.
//
// Sized off the longest thing that goes in one, which is the name of a swing
// being charged -- with room to spare and a column of clearance each side.
// A skill named past it is caught by a data test rather than by a player
// watching half a name.
inline constexpr int kBossPanelWidth = 32;

// Seconds as mm:ss, rounded up so the clock reads 0:00 only when the time is
// actually gone.
std::string FightClock(double seconds_left);

// The line across the top: "Normal Zakum - P1 - 100%", or what became of the
// fight once it is over.
std::string FightHeading(const BossRun& run);

// The whole screen for `run`.
ftxui::Element BossFightPanel(const BossRun& run);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_
