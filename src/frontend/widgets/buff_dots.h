/* The player's standing buffs, counted in dots along a charge bar.
 *
 * The dots grow in from both ends of the bar and never reach the name: a blank
 * column always stands between them and it, and a crowd that will not fit
 * doubles up (· then : then ⁞) rather than crossing. A bar with a row the
 * name leaves empty spends that row first.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_BUFF_DOTS_H_
#define MS_SRC_FRONTEND_WIDGETS_BUFF_DOTS_H_

#include <string>
#include <vector>

namespace ms {

// One glyph per cell of a `width`-column bar carrying `labels` centred a row
// apiece, as ProgressBar lays them; "" where no dot falls. `count` past what
// the room holds at four to a glyph shows as that many.
std::vector<std::vector<std::string>> BuffDots(
    int width, const std::vector<std::string>& labels, int count);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_BUFF_DOTS_H_
