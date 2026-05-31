#include "src/commands/util.h"

#include <gtest/gtest.h>
#include "src/equip.pb.h"
#include "src/equip_instance.h"

namespace ms {
namespace {

TEST(FormatEquipTest, IncludesNameAndUpgradeSlots) {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_upgrade_slots(7);
  std::string out = FormatEquip(EquipInstance(proto));
  EXPECT_NE(out.find("Sword"), std::string::npos);
  EXPECT_NE(out.find("7 upgrade slots"), std::string::npos);
}

TEST(FormatEquipTest, ShowsNonZeroStats) {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.mutable_base_stats()->set_attack(15);
  std::string out = FormatEquip(EquipInstance(proto));
  EXPECT_NE(out.find("ATT"), std::string::npos);
}

TEST(FormatEquipTest, OmitsZeroStats) {
  EquipPrototype proto;
  proto.set_name("Sword");
  std::string out = FormatEquip(EquipInstance(proto));
  EXPECT_EQ(out.find("STR"), std::string::npos);
  EXPECT_EQ(out.find("DEF"), std::string::npos);
}

TEST(SlotFromNameTest, RecognisesPrimaryWeapon) {
  EXPECT_EQ(SlotFromName("primary_weapon"), EQUIP_SLOT_PRIMARY_WEAPON);
}

TEST(SlotFromNameTest, ReturnsUnspecifiedForUnknown) {
  EXPECT_EQ(SlotFromName("hat"), EQUIP_SLOT_UNSPECIFIED);
  EXPECT_EQ(SlotFromName(""), EQUIP_SLOT_UNSPECIFIED);
}

TEST(SlotToNameTest, PrimaryWeapon) {
  EXPECT_EQ(SlotToName(EQUIP_SLOT_PRIMARY_WEAPON), "Primary Weapon");
}

TEST(SlotToNameTest, UnknownSlot) {
  EXPECT_EQ(SlotToName(EQUIP_SLOT_UNSPECIFIED), "Unknown");
}

}  // namespace
}  // namespace ms
