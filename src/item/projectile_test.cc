#include "src/item/projectile.h"

#include <gtest/gtest.h>

#include "google/protobuf/descriptor.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The two directions are written out separately, and the callers read one
// each: the sims arm a weapon by AmmoFor, the character credits the attack by
// WeaponDrawing. A pair that disagreed would have a sim measuring a bow
// holding arrows the character then refuses to count.
TEST(ProjectileTest, TheTwoDirectionsAgree) {
  const google::protobuf::EnumDescriptor* types = EquipType_descriptor();
  int paired = 0;
  for (int i = 0; i < types->value_count(); ++i) {
    EquipType type = static_cast<EquipType>(types->value(i)->number());
    EquipType ammo = AmmoFor(type);
    if (ammo != EQUIP_TYPE_UNSPECIFIED) {
      ++paired;
      EXPECT_EQ(WeaponDrawing(ammo), type) << EquipType_Name(type);
    }
    EquipType weapon = WeaponDrawing(type);
    if (weapon != EQUIP_TYPE_UNSPECIFIED) {
      EXPECT_EQ(AmmoFor(weapon), type) << EquipType_Name(type);
    }
  }
  EXPECT_EQ(paired, 3) << "a weapon that draws ammunition has gone missing";
}

// The ordinary case, and the one the callers lean on: a weapon that draws from
// nothing, and an item that is not ammunition, both answer with silence.
TEST(ProjectileTest, NothingElseIsPaired) {
  EXPECT_EQ(AmmoFor(EQUIP_TYPE_ONE_HANDED_SWORD), EQUIP_TYPE_UNSPECIFIED);
  EXPECT_EQ(WeaponDrawing(EQUIP_TYPE_ONE_HANDED_SWORD), EQUIP_TYPE_UNSPECIFIED);
  EXPECT_EQ(AmmoFor(EQUIP_TYPE_UNSPECIFIED), EQUIP_TYPE_UNSPECIFIED);
  EXPECT_EQ(WeaponDrawing(EQUIP_TYPE_UNSPECIFIED), EQUIP_TYPE_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
