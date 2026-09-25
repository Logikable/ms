/* The widths of the main view's two columns.
 *
 * Every panel on the main screen is in one of two columns (the character panel
 * over combat on the left, the equipped panel over the bag on the right) and
 * takes its column's width, which keeps each pair lined up. The numbers live
 * here so both the panels and the layout can use them.
 *
 * A column's width depends only on the terminal's width: what a panel is
 * showing never moves a border.
 */
#ifndef MS_SRC_FRONTEND_PANEL_WIDTHS_H_
#define MS_SRC_FRONTEND_PANEL_WIDTHS_H_

namespace ms {

// The character and combat panels. The minimum fits the Stats tab's [+]/[Max]
// buttons and combat's map row. The maximum fits the longest skill name beside
// a full level column and its [+], and stops there so the left and right halves
// of a row stay readable together.
inline constexpr int kLeftColumnMin = 35;
inline constexpr int kLeftColumnMax = 51;

// The equipped panel and the bag, whose lists share the same columns and so the
// same minimum: an 82-column header, the blank column inside the right border,
// and the two borders. They have no maximum.
inline constexpr int kRightColumnMin = 85;

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANEL_WIDTHS_H_
