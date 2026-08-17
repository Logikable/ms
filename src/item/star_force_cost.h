/* What one star force attempt costs in meso.
 *
 * The price belongs to the ITEM and to the star it is reaching for: a level
 * 138 weapon going 19 to 20 costs two hundred times what the same weapon cost
 * at its first star. That curve is the whole point of the system -- it is what
 * stops a player from simply buying thirty stars once they can afford one.
 *
 * Source: maplestorywiki.net, "Star Force Enhancement", Base Meso Cost. The
 * GMS column, which differs from KMS/JMS/MSEA below 15 stars and agrees above.
 */
#ifndef MS_SRC_ITEM_STAR_FORCE_COST_H_
#define MS_SRC_ITEM_STAR_FORCE_COST_H_

#include <cstdint>

namespace ms {

// Meso one attempt costs on an item of `required_level` holding `stars` now.
// Paid whether the attempt succeeds, fails or destroys the item.
//
// Returns 0 for a level or a star count outside what the game can produce,
// which is a price nothing should ever be charged rather than a free upgrade
// -- the caller reaches this only through CanStarForce.
int64_t StarForceCost(int required_level, int stars);

}  // namespace ms

#endif  // MS_SRC_ITEM_STAR_FORCE_COST_H_
