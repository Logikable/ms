/* The boss fight screen: a heading with the fight's HP percent, then an arena
 * with one bar per monster in its grid cell, the player among them, and the
 * clock across the top.
 *
 * It only displays a BossRun. The run holds every clock, every bar and each
 * bar's cell, and this spreads those cells over the screen. The bars keep their
 * size and the gaps take whatever is left, so the same fight fills a wide
 * terminal and still fits a narrow one. A bar never moves during the fight: a
 * monster that dies leaves its cell empty instead of closing the gap.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"
#include "src/frontend/placement.h"

namespace ms {

// The width of every panel in the arena, monsters and player alike, so the
// arena is a grid whatever is in it. It is narrow because a phase can have ten
// panels across four rows. Names wrap over the bar's rows instead of setting
// the width, so a row must fit the longest word in a name.
inline constexpr int kBossPanelWidth = 16;

// How many rows the player's bar uses. Fixed, unlike the monsters', because the
// name on it changes with every attack, and a panel that grew and shrank
// mid-fight would move everything around it.
inline constexpr int kPlayerBarRows = 2;

// The most rows a monster's bar uses. If a phase's longest name needs both,
// every bar in it gets both, so the arena's rows stay even.
inline constexpr int kMaxMobBarRows = 2;

// The grid every fight uses, the same shape for all of them, so a new boss's
// room has its corners where the player expects. The minimum terminal fits
// seven panels across and six rows. Things stand on alternate cells, leaving
// room beside a bar for a damage stack, so the grid has nine columns rather
// than the thirteen that would fit.
inline constexpr int kArenaColumns = 9;
inline constexpr int kArenaRows = 6;

// The heading: "Normal Zakum - P1 - 100%", or the fight's result once it is
// over.
std::string FightHeading(const BossRun& run);

// The whole screen for `run`. `buff_dots` is the Buff Indicators option.
ftxui::Element BossFightPanel(const BossRun& run, bool buff_dots);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_FIGHT_PANEL_H_
