/* The widths the main view's two columns lay out at.
 *
 * Every panel on the main screen belongs to one of two columns -- the
 * character panel over combat on the left, the equipped panel over the bag on
 * the right -- and takes its column's width, which is what keeps each pair
 * lined up with the other. The numbers live here rather than in a panel so
 * both the panels and the layout that sizes them can read the same ones.
 *
 * A column's width follows the terminal's and nothing else: what a panel is
 * currently displaying never moves a border.
 */
#ifndef MS_SRC_FRONTEND_PANEL_WIDTHS_H_
#define MS_SRC_FRONTEND_PANEL_WIDTHS_H_

namespace ms {

// The character and combat panels. The minimum seats the Stats tab's
// [+]/[Max] buttons and combat's map row; the maximum is what the longest
// skill name the game ships asks for beside a full level column and its [+],
// and stops there so a row's left and right halves stay readable together.
inline constexpr int kLeftColumnMin = 35;
inline constexpr int kLeftColumnMax = 51;

// The equipped panel and the bag, whose lists carry the same columns and so
// the same minimum: an 82-column header inside a border. Nothing caps them --
// whatever the left column does not take is theirs.
inline constexpr int kRightColumnMin = 84;

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANEL_WIDTHS_H_
