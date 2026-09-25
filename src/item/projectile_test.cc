#include "src/item/projectile.h"

#include <gtest/gtest.h>

#include "google/protobuf/descriptor.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The two directions are written separately, and each caller reads one: the
// sims equip a weapon with AmmoFor, and the character counts the attack with
// WeaponDrawing. If they disagreed, a sim would measure a bow with arrows the
// character then refuses to count.
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

// The common case callers rely on: a weapon using no ammunition, and an item
// that isn't ammunition, both return unspecified.
TEST(ProjectileTest, NothingElseIsPaired) {
  EXPECT_EQ(AmmoFor(EQUIP_TYPE_ONE_HANDED_SWORD), EQUIP_TYPE_UNSPECIFIED);
  EXPECT_EQ(WeaponDrawing(EQUIP_TYPE_ONE_HANDED_SWORD), EQUIP_TYPE_UNSPECIFIED);
  EXPECT_EQ(AmmoFor(EQUIP_TYPE_UNSPECIFIED), EQUIP_TYPE_UNSPECIFIED);
  EXPECT_EQ(WeaponDrawing(EQUIP_TYPE_UNSPECIFIED), EQUIP_TYPE_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
