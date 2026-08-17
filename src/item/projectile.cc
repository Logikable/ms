#include "src/item/projectile.h"

#include "src/protos/equip.pb.h"

namespace ms {

EquipType AmmoFor(EquipType weapon) {
  switch (weapon) {
    case EQUIP_TYPE_CLAW:
      return EQUIP_TYPE_THROWING_STAR;
    case EQUIP_TYPE_BOW:
      return EQUIP_TYPE_ARROW_FOR_BOW;
    case EQUIP_TYPE_CROSSBOW:
      return EQUIP_TYPE_ARROW_FOR_CROSSBOW;
    default:
      return EQUIP_TYPE_UNSPECIFIED;
  }
}

EquipType WeaponDrawing(EquipType ammo) {
  switch (ammo) {
    case EQUIP_TYPE_THROWING_STAR:
      return EQUIP_TYPE_CLAW;
    case EQUIP_TYPE_ARROW_FOR_BOW:
      return EQUIP_TYPE_BOW;
    case EQUIP_TYPE_ARROW_FOR_CROSSBOW:
      return EQUIP_TYPE_CROSSBOW;
    default:
      return EQUIP_TYPE_UNSPECIFIED;
  }
}

}  // namespace ms
