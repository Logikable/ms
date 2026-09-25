/* Which weapon uses which ammunition. One mapping, read from both directions:
 * the character checks what a projectile needs before counting its attack, and
 * the gear sims check what a weapon needs before measuring it.
 */
#ifndef MS_SRC_ITEM_PROJECTILE_H_
#define MS_SRC_ITEM_PROJECTILE_H_

#include "src/protos/equip.pb.h"

namespace ms {

// The ammunition `weapon` uses, or unspecified if it uses none.
EquipType AmmoFor(EquipType weapon);

// The weapon that uses `ammo`, or unspecified if it isn't ammunition.
EquipType WeaponDrawing(EquipType ammo);

}  // namespace ms

#endif  // MS_SRC_ITEM_PROJECTILE_H_
