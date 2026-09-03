/* The boss fight screen: one bar per monster where the fight puts it, the
 * player standing among them, the fight's own HP bar across the top and the
 * clock under it.
 *
 * A view over a BossRun and nothing else. The run holds every clock, every bar
 * and the cell each one stands in; this spreads those cells over the screen it
 * is given. The bars keep their size and the gaps between them take whatever
 * is left, so the same fight fills a wide terminal and still fits a narrow
 * one. A bar never moves while the fight is on -- a monster that dies leaves
 * its slot empty rather than closing the gap.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"

namespace ms {

// How wide every panel in the arena is drawn: the monsters' bars and the
// player's alike, so the arena is a grid whatever is standing in it.
//
// Narrow, because a phase can hold ten of them across four rows and the whole
// arena has to fit one screen. Names wrap over the bar's rows rather than
// setting the width, so what a row must hold is the longest WORD in a name --
// "Assassinate", and "Horntail's". A name that outgrows the pair of rows is
// caught by a data test rather than by a player watching half of it.
inline constexpr int kBossPanelWidth = 16;

// How many rows the player's bar is drawn over. Fixed, unlike the monsters':
// the name on it changes with every swing, and a panel that grew and shrank
// mid-fight would move everything around it.
inline constexpr int kPlayerBarRows = 2;

// The most rows a monster's bar takes. A phase whose longest name needs both
// gives both to every bar in it, so the arena's rows stay square.
inline constexpr int kMaxMobBarRows = 2;

// The smallest terminal the game is laid out for. Every arena fits inside it,
// so a fight never draws a bar off the edge of somebody's screen.
inline constexpr int kMinTerminalColumns = 120;
inline constexpr int kMinTerminalRows = 30;

// The grid every fight stands on -- one shape for all of them, so a player
// walking into a new boss already knows where the room's corners are.
//
// What the smallest terminal holds is seven panels across a row and six rows
// of them; the arena spends the rest on the fight's HP bar and the clock.
// Nine columns rather than the thirteen that would fit, because things stand
// on alternate cells: five spots to a row leaves a damage stack the width it
// needs beside a bar.
inline constexpr int kArenaColumns = 9;
inline constexpr int kArenaRows = 6;

// The line across the top: "Normal Zakum - P1 - 100%", or what became of the
// fight once it is over.
std::string FightHeading(const BossRun& run);

// The whole screen for `run`.
ftxui::Element BossFightPanel(const BossRun& run);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_
