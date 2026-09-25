/* The player's active buffs, shown as dots on a charge bar.
 *
 * The dots grow inward from both ends of the bar and never reach the name:
 * there is always a blank column between them. When there isn't room, glyphs
 * double up (· then : then ⁞) instead of crossing into the name. A row the name
 * leaves empty fills first.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_BUFF_DOTS_H_
#define MS_SRC_FRONTEND_WIDGETS_BUFF_DOTS_H_

#include <string>
#include <vector>

namespace ms {

// One glyph per cell of a `width`-column bar with `labels` centred one per row,
// as ProgressBar lays them out; "" where there is no dot. A `count` larger than
// the room holds at four per glyph is shown as the maximum that fits.
std::vector<std::vector<std::string>> BuffDots(
    int width, const std::vector<std::string>& labels, int count);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_BUFF_DOTS_H_
