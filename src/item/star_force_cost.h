/* What one star force attempt costs in meso.
 *
 * The price depends on the item and the star being attempted: a level 138
 * weapon going from 19 to 20 costs two hundred times its first star. That curve
 * is the point of the system: it stops a player from just buying thirty stars
 * once they can afford one.
 *
 * Source: maplestorywiki.net, "Star Force Enhancement", Base Meso Cost. The GMS
 * column, which differs from KMS/JMS/MSEA below 15 stars and matches above.
 */
#ifndef MS_SRC_ITEM_STAR_FORCE_COST_H_
#define MS_SRC_ITEM_STAR_FORCE_COST_H_

#include <cstdint>

namespace ms {

// Meso one attempt costs on an item of `required_level` with `stars`, paid
// whether it succeeds, fails or destroys the item. 0 for a level or star count
// the game can't produce; since this is only reached through CanStarForce, that
// means no charge, not a free upgrade.
int64_t StarForceCost(int required_level, int stars);

}  // namespace ms

#endif  // MS_SRC_ITEM_STAR_FORCE_COST_H_
