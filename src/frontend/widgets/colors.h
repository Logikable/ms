#ifndef MS_SRC_FRONTEND_WIDGETS_COLORS_H_
#define MS_SRC_FRONTEND_WIDGETS_COLORS_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

/* Red and dim, and what each means.
 *
 * Red marks the reason. It goes on the one value the player falls short of (a
 * level, a job, a price), so the screen shows why without being asked. Always a
 * single cell, never a whole row, and never a value that is just missing. Red
 * isn't a category colour: something always red says nothing.
 *
 * Dim marks what can't be used: a button that won't respond, a menu entry, a
 * row whose action is blocked. Never on a value the player needs to read and
 * compare; that is red's job.
 *
 * Using both at once is the strongest signal, and is intentional. The scroll
 * confirmation turns the cost red and dims the Confirm button: red names the
 * shortfall, and dim says the action is unavailable. Use that pairing rather
 * than choosing one. Put them on different elements, though, since dimming a
 * red cell mutes the one thing worth reading.
 *
 * Dim has a second, milder use: something not currently in effect rather than
 * blocked, like an unearned set tier or a job category an item isn't for. Those
 * appear on screens where nothing is being refused, so the two meanings don't
 * clash in practice.
 */

// The identity colour: borders, dividers, panel titles and structural labels.
// Also a plain damage line, and the mark for a weapon's price.
inline const ftxui::Color kTheme = ftxui::Color::RGB(100, 150, 200);

// The band behind the row under a list's cursor. Background only, never text.
// Dark enough that everything a row can show still reads on it: a bright name,
// a red requirement, a dimmed row. Only used by HighlightRow in chrome.h.
inline const ftxui::Color kSelectedRow = ftxui::Color::RGB(40, 62, 92);

// Star bar in the inspect panel.
inline const ftxui::Color kYellow = ftxui::Color::RGB(255, 210, 50);
inline const ftxui::Color kGray = ftxui::Color::RGB(100, 100, 100);

// Stat source breakdown in the inspect panel, a skill list's auto-attack tag,
// and the mark for a shoulder's price. Different enough from the gold beside it
// that the two tags don't look like shades of each other, and cool where the
// other two marks are warm and blue.
inline const ftxui::Color kPurple = ftxui::Color::RGB(173, 163, 255);
// A skill list's attack tag, the Star Force share of a stat, and the mark for a
// Root Abyss hat's price.
inline const ftxui::Color kGold = ftxui::Color::RGB(255, 198, 50);
// A critical damage line, and the mark for a secondary weapon's price. Clearly
// different from the gold above: a crit must be distinguishable from a plain
// line at a glance, and the two marks from each other.
inline const ftxui::Color kOrange = ftxui::Color::RGB(240, 140, 60);

// Star Force success rates, and the mark for a Root Abyss top's price.
inline const ftxui::Color kGreen = ftxui::Color::RGB(100, 175, 100);
// The mark for a Root Abyss bottom's price. Warm where kPurple is cool, so the
// two are never confused on the token shelf.
inline const ftxui::Color kPink = ftxui::Color::RGB(235, 130, 175);
inline const ftxui::Color kMutedYellow = ftxui::Color::RGB(185, 155, 70);

// Why something is refused, and bad outcomes: an unmet requirement, an
// unaffordable price, the Star Force destroy rate. See the note above.
inline const ftxui::Color kRed = ftxui::Color::RGB(185, 70, 70);

// A party member's damage numbers and charge bar, normal and critical. Half as
// bright as the player's, so a fight with three people still reads as the
// player's own: their numbers are the bright ones.
inline const ftxui::Color kFaintTheme = ftxui::Color::RGB(55, 80, 110);
inline const ftxui::Color kFaintOrange = ftxui::Color::RGB(125, 75, 35);

// A colour stored as components, since ftxui::Color can't return its own.
struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;

  ftxui::Color ToColor() const {
    return ftxui::Color::RGB(r, g, b);
  }
};

// The four colours GMS uses for Inner Ability ranks: blue, purple, yellow,
// green. The exact values aren't published; these are what the community uses.
inline constexpr Rgb kRare = {0x66, 0xFF, 0xFF};
inline constexpr Rgb kEpic = {0x99, 0x33, 0xFF};
inline constexpr Rgb kUnique = {0xFF, 0xCC, 0x00};
inline constexpr Rgb kLegendary = {0x77, 0xEE, 0x00};

// The game's background tone, and the unfilled part of every progress bar (EXP,
// attack charge, mob HP).
inline constexpr Rgb kGround = {20, 35, 55};
inline const ftxui::Color kBarEmpty = kGround.ToColor();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_COLORS_H_
