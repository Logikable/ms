#include "analysis/sim_gear.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <string>

#include "src/character/character.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/testing/prototypes.h"

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

// A split puts the bag's copy on for farming and leaves boss fights the worn
// piece with its stars. A ring lands where the copy does, even past an empty
// ring slot, and a slot farming already has its own piece in is left alone.
TEST(SplitFarmPieceTest, TheWornPieceStaysWithBossFights) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_HERO);
  CharacterInstance character(rng, proto);
  Equip starred;
  starred.set_stars(12);
  character.PickUp(std::make_unique<EquipInstance>(
      Accessory("Eyepatch", 160, EQUIP_SLOT_EYE_ACCESSORY), starred));
  ASSERT_TRUE(character.Equip(0));
  character.PickUp(std::make_unique<EquipInstance>(Ring("Meister", 140)));
  ASSERT_TRUE(character.Equip(0));
  character.PickUp(std::make_unique<EquipInstance>(Ring("Horntail", 110)));
  ASSERT_TRUE(character.Equip(0));
  ASSERT_TRUE(character.Unequip(EQUIP_SLOT_RING));  // Meister's slot empties
  ASSERT_EQ(character.WornAt(kFarmGear, EQUIP_SLOT_RING_2)->name(), "Horntail");
  character.SellEquip(0);

  EXPECT_EQ(SplitFarmPiece(character, EQUIP_SLOT_EYE_ACCESSORY),
            EQUIP_SLOT_UNSPECIFIED)
      << "no copy in the bag";
  character.PickUp(std::make_unique<EquipInstance>(
      Accessory("Eyepatch", 160, EQUIP_SLOT_EYE_ACCESSORY)));
  character.PickUp(std::make_unique<EquipInstance>(Ring("Horntail", 110)));
  EXPECT_EQ(SplitFarmPiece(character, EQUIP_SLOT_EYE_ACCESSORY),
            EQUIP_SLOT_EYE_ACCESSORY);
  EXPECT_EQ(character.WornAt(kBossGear, EQUIP_SLOT_EYE_ACCESSORY)->stars(), 12);
  EXPECT_EQ(character.WornAt(kFarmGear, EQUIP_SLOT_EYE_ACCESSORY)->stars(), 0);
  EXPECT_FALSE(SharesFarmPiece(character, EQUIP_SLOT_EYE_ACCESSORY));

  EquipSlot farm = SplitFarmPiece(character, EQUIP_SLOT_RING_2);
  ASSERT_NE(farm, EQUIP_SLOT_UNSPECIFIED);
  EXPECT_EQ(character.WornAt(kFarmGear, farm)->name(), "Horntail");
  EXPECT_EQ(character.WornAt(kBossGear, farm)->name(), "Horntail");
  EXPECT_NE(character.WornAt(kFarmGear, farm),
            character.WornAt(kBossGear, farm));
  EXPECT_EQ(character.inventory().size(), 0);
}

// Before presets open there is one set, so nothing splits.
TEST(SplitFarmPieceTest, NothingSplitsBeforePresets) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(170);
  proto.set_job(JOB_HERO);
  CharacterInstance character(rng, proto);
  character.PickUp(std::make_unique<EquipInstance>(Ring("Meister", 140)));
  ASSERT_TRUE(character.Equip(0));
  character.PickUp(std::make_unique<EquipInstance>(Ring("Meister", 140)));
  EXPECT_FALSE(SharesFarmPiece(character, EQUIP_SLOT_RING));
  EXPECT_EQ(SplitFarmPiece(character, EQUIP_SLOT_RING), EQUIP_SLOT_UNSPECIFIED);
}

// The free pets go on with the rest of the shop's accessories, and their
// scroll stays unbought: it spends an Etc item the sim never holds, which the
// trace price would otherwise give away.
TEST(OutfitTest, WearsThreeFreePetsUnscrolled) {
  std::map<std::string, EquipPrototype> equips;
  for (int i = 0; i < 4; ++i) {
    EquipPrototype pet =
        Accessory("Pet " + std::to_string(i), 0, EQUIP_SLOT_PET);
    pet.set_shop_price(0);
    pet.set_shelf_order(i);
    pet.set_upgrade_slots(8);
    equips["pet_" + std::to_string(i)] = pet;
  }
  Scroll premium;
  premium.set_name("Premium Scroll for Pet");
  premium.set_target(SCROLL_TARGET_PET);
  premium.set_paid_with("Premium Scroll for Pet");
  premium.mutable_stats()->set_attack(5);
  premium.set_tier(SCROLL_TIER_1);
  premium.set_success_rate(100);
  premium.add_applicable_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EquipPrototype sword = Accessory("Sword", 0, EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  equips["sword"] = sword;
  GameState state(equips, {{"premium", premium}}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailMap()}});
  state.current_map = "field";
  while (state.character.proto().level() < 30) {
    state.character.LevelUp();
  }

  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  ASSERT_TRUE(state.character.Equip(0));
  OutfitWeapon(state, EQUIP_TYPE_ONE_HANDED_SWORD);
  for (EquipSlot slot : {EQUIP_SLOT_PET, EQUIP_SLOT_PET_2, EQUIP_SLOT_PET_3}) {
    EXPECT_NE(state.character.WornAt(StatPreset::kFirst, slot), nullptr);
  }
  EXPECT_FALSE(state.character.IsWearing("Pet 3"));
  EXPECT_TRUE(ChooseScrolls(state, 0).empty());
}

}  // namespace
}  // namespace ms
