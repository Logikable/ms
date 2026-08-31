#ifndef MS_SRC_FRONTEND_WIDGETS_COLORS_H_
#define MS_SRC_FRONTEND_WIDGETS_COLORS_H_

#include <algorithm>
#include <cmath>

#include "ftxui/dom/elements.hpp"

namespace ms {

/* Red and dim, and which of them says what.
 *
 * **Red is the reason.** It goes on the ONE value the player falls short of --
 * a level, a job, a price -- so the screen answers "why not" without being
 * asked. Always a cell, never a whole row, and never a value that is merely
 * absent. Red is not a category colour: a thing that is always red says
 * nothing.
 *
 * **Dim is the door.** It goes on what cannot be used: a button that will not
 * answer, a menu entry, a row whose action is blocked. Never on the value the
 * player is meant to read and compare -- that is red's job.
 *
 * **Both at once is the strongest form, and is deliberate.** The scroll
 * confirm reddens the cost and dims the Confirm button: red names the
 * shortfall, dim says the door is shut. Copy that pairing rather than choosing
 * between them. Put them on DIFFERENT elements, though -- dimming a red cell
 * mutes the one thing worth reading.
 *
 * Dim has a second, softer job: what is not in play rather than what is
 * blocked -- a set tier not yet earned, a job category an item does not serve.
 * Those sit on screens where nothing is being refused, so the two readings do
 * not collide in practice.
 */

// Identity color: borders, separators, panel titles, structural labels. Also
// a plain damage line, and the mark a weapon's price is asked in.
inline const ftxui::Color kTheme = ftxui::Color::RGB(100, 150, 200);

// Star bar in the inspect panel.
inline const ftxui::Color kYellow = ftxui::Color::RGB(255, 210, 50);
inline const ftxui::Color kGray = ftxui::Color::RGB(100, 100, 100);

// Stat source breakdown in the inspect panel, and a skill list's auto-attack
// tag. Far enough from the gold beside it that the two tags cannot be read as
// shades of each other.
inline const ftxui::Color kPurple = ftxui::Color::RGB(173, 163, 255);
// A skill list's attack tag, and the Star Force share of a stat.
inline const ftxui::Color kGold = ftxui::Color::RGB(255, 198, 50);
// A critical damage line, and the mark an off-hand's price is asked in. Well
// clear of the gold above: a crit has to be told apart from a plain line at a
// glance, and the two marks from each other.
inline const ftxui::Color kOrange = ftxui::Color::RGB(240, 140, 60);

// Star Force outcome rates.
inline const ftxui::Color kGreen = ftxui::Color::RGB(100, 175, 100);
inline const ftxui::Color kMutedYellow = ftxui::Color::RGB(185, 155, 70);

// The reason something is refused, and a bad outcome: a requirement not met,
// a price out of reach, the Star Force destroy rate. See the note above.
inline const ftxui::Color kRed = ftxui::Color::RGB(185, 70, 70);

// A party member's damage numbers and charge bar, plain and critical. Half the
// strength of the pair above, so a fight with three people in it still reads
// as the player's own: their numbers are the bright ones.
inline const ftxui::Color kFaintTheme = ftxui::Color::RGB(55, 80, 110);
inline const ftxui::Color kFaintOrange = ftxui::Color::RGB(125, 75, 35);

/* Rarity, and the two colours a rarity-painted row needs.
 *
 * A colour a row computes from is held as components rather than as an
 * ftxui::Color, which does not give its own back. An Inner Ability line is
 * drawn on its rank's colour and takes whichever text reads against it, and a
 * line out of play fades -- so both colours fall out of the rank and the
 * fading, and neither is written down per rank. See RarityColors.
 */
struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;

  ftxui::Color ToColor() const {
    return ftxui::Color::RGB(r, g, b);
  }
};

// The four ranks GMS paints an Inner Ability line in: blue, purple, yellow,
// green. The exact values are not published anywhere; these are what the
// community renders them at.
inline constexpr Rgb kRare = {0x66, 0xFF, 0xFF};
inline constexpr Rgb kEpic = {0x99, 0x33, 0xFF};
inline constexpr Rgb kUnique = {0xFF, 0xCC, 0x00};
inline constexpr Rgb kLegendary = {0x77, 0xEE, 0x00};

// The game's ground tone: the dark blue behind a bar's unfilled remainder,
// and what a faded colour is drawn toward.
inline constexpr Rgb kGround = {20, 35, 55};

// Unfilled remainder of any progress bar (EXP, attack charge, mob HP).
inline const ftxui::Color kBarEmpty = kGround.ToColor();

// `color` drawn `toward` of the way to the ground, for something out of play.
// Not ftxui::dim, which reaches the foreground alone and would leave a
// background at full strength.
inline Rgb Faded(Rgb color, double toward = 0.6) {
  const double keep = 1.0 - std::clamp(toward, 0.0, 1.0);
  const auto blend = [&](int c, int ground) {
    return static_cast<int>(std::lround(keep * c + (1.0 - keep) * ground));
  };
  return {blend(color.r, kGround.r), blend(color.g, kGround.g),
          blend(color.b, kGround.b)};
}

// Black or white, whichever reads against `background`. Decided by relative
// luminance, so a colour that fades past the middle turns its text over
// without anybody naming the rank it belongs to.
inline ftxui::Color TextOn(Rgb background) {
  // sRGB relative luminance, and the point either side of which the better
  // contrast changes hands.
  const auto linear = [](int c) {
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  const double luminance = 0.2126 * linear(background.r) +
                           0.7152 * linear(background.g) +
                           0.0722 * linear(background.b);
  return luminance < 0.179 ? ftxui::Color::White : ftxui::Color::Black;
}

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_COLORS_H_
