/* Which weapon draws which ammunition. One pairing, read from both ends: the
 * character asks what a projectile needs before crediting its attack, and the
 * gear sims ask what a weapon needs before measuring one.
 */
#ifndef MS_SRC_ITEM_PROJECTILE_H_
#define MS_SRC_ITEM_PROJECTILE_H_

#include "src/protos/equip.pb.h"

namespace ms {

// What `weapon` draws from, or unspecified for a weapon that draws from
// nothing.
EquipType AmmoFor(EquipType weapon);

// The weapon that draws `ammo`, or unspecified for anything that is not
// ammunition.
EquipType WeaponDrawing(EquipType ammo);

}  // namespace ms

#endif  // MS_SRC_ITEM_PROJECTILE_H_
