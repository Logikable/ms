#include "analysis/sim_gear.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <string>

#include "src/character/character.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

EquipPrototype Accessory(const std::string& name, int level, EquipSlot slot) {
  EquipPrototype accessory;
  accessory.set_name(name);
  accessory.set_equip_slot(slot);
  accessory.set_required_level(level);
  accessory.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return accessory;
}

EquipPrototype Ring(const std::string& name, int level) {
  return Accessory(name, level, EQUIP_SLOT_RING);
}

// Wearing a second copy of a worn ring swaps it into the bag, and it must stay
// there rather than be swapped back every pass. The first slot's weaker ring is
// what made the bag copy look like an upgrade.
TEST(WearBestFromBagTest, ASecondCopyOfAWornRingStaysInTheBag) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(160);
  proto.set_job(JOB_HERO);
  CharacterInstance character(rng, proto);
  character.PickUp(std::make_unique<EquipInstance>(Ring("Tin Ring", 30)));
  ASSERT_TRUE(character.Equip(0));
  character.PickUp(std::make_unique<EquipInstance>(Ring("Horntail Ring", 110)));
  ASSERT_TRUE(character.Equip(0));
  character.PickUp(std::make_unique<EquipInstance>(Ring("Horntail Ring", 110)));

  WearBestFromBag(character);
  EXPECT_EQ(character.inventory().size(), 1);
  EXPECT_EQ(character.equipped().size(), 2u);
}

// Once presets open, the better accessory goes on for boss fights only, and the
// one it replaced stays on for farming, where a meso line costs a boss
// nothing. Before then there is one set, and the better piece replaces it.
TEST(WearBestFromBagTest, ABetterAccessoryGoesOnForBossesOnly) {
  for (int level : {170, 180}) {
    std::mt19937 rng(1);
    Character proto;
    proto.set_level(level);
    proto.set_job(JOB_HERO);
    CharacterInstance character(rng, proto);
    character.PickUp(std::make_unique<EquipInstance>(
        Accessory("Old Mask", 30, EQUIP_SLOT_FACE_ACCESSORY)));
    ASSERT_TRUE(character.Equip(0));
    character.PickUp(std::make_unique<EquipInstance>(
        Accessory("Crystal", 110, EQUIP_SLOT_FACE_ACCESSORY)));

    WearBestFromBag(character);
    const EquipInstance* boss =
        character.WornAt(kBossGear, EQUIP_SLOT_FACE_ACCESSORY);
    const EquipInstance* farm =
        character.WornAt(kFarmGear, EQUIP_SLOT_FACE_ACCESSORY);
    ASSERT_NE(boss, nullptr);
    ASSERT_NE(farm, nullptr);
    EXPECT_EQ(boss->name(), "Crystal") << "at " << level;
    EXPECT_EQ(farm->name(), level >= 180 ? "Old Mask" : "Crystal")
        << "at " << level;
  }
}

}  // namespace
}  // namespace ms
