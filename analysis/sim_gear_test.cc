#include "analysis/sim_gear.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "src/character/character.h"
#include "src/character/progression.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
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
  const int presets = UnlockLevel(Feature::kEquipPresets);
  for (int level : {presets - 1, presets}) {
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
    EXPECT_EQ(farm->name(), level >= presets ? "Old Mask" : "Crystal")
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
  proto.set_level(UnlockLevel(Feature::kEquipPresets) - 1);
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

// A level 200 Hero in a world of `equips`, `scrolls` and `items`.
std::unique_ptr<GameState> HeroWith(
    std::map<std::string, EquipPrototype> equips,
    std::map<std::string, Scroll> scrolls,
    std::map<std::string, ItemPrototype> items) {
  auto state = std::make_unique<GameState>(
      std::move(equips), std::move(scrolls), std::move(items),
      std::map<std::string, Mob>{{"snail", SnailMob()}},
      std::map<std::string, MapData>{{"field", SnailMap()}});
  state->current_map = "field";
  Character hero;
  hero.set_level(200);
  hero.set_job(JOB_HERO);
  state->character.RestoreFrom(hero, state->equips, state->items);
  return state;
}

EquipPrototype SkillRing(const std::string& skill, int level, EquipSlot slot) {
  EquipPrototype ring =
      Accessory(skill + " Lv. " + std::to_string(level), 110, slot);
  ring.mutable_equipment_skill()->set_skill(skill);
  ring.mutable_equipment_skill()->set_level(level);
  return ring;
}

// A token box gives the piece not yet owned in the weakest slot; a pick box the
// piece richest in the main stat; a ring box the ring held lower, Continuous on
// a tie. A box with nothing left to give stays shut.
TEST(OpenBoxesTest, EachBoxGivesWhatHelpsMost) {
  std::map<std::string, EquipPrototype> equips;
  for (EquipSlot slot : {EQUIP_SLOT_HAT, EQUIP_SLOT_CAPE}) {
    EquipPrototype piece = Accessory(EquipSlot_Name(slot), 200, slot);
    TokenPrice* price = piece.add_token_prices();
    price->set_token_item("coin");
    price->set_count(1);
    equips[EquipSlot_Name(slot)] = piece;
  }
  EquipPrototype red = Accessory("Red Book", 160, EQUIP_SLOT_POCKET);
  red.mutable_base_stats()->set_str(20);
  EquipPrototype blue = Accessory("Blue Book", 160, EQUIP_SLOT_POCKET);
  blue.mutable_base_stats()->set_int_(20);
  equips["red"] = red;
  equips["blue"] = blue;
  equips["old_cape"] = Accessory("Old Cape", 100, EQUIP_SLOT_CAPE);
  equips["old_hat"] = Accessory("Old Hat", 150, EQUIP_SLOT_HAT);
  for (int level : {1, 4}) {
    equips["r" + std::to_string(level)] =
        SkillRing("Ring of Restraint", level, EQUIP_SLOT_RING);
    equips["c" + std::to_string(level)] =
        SkillRing("Continuous Ring", level, EQUIP_SLOT_CONT_RING);
  }
  ItemPrototype armor;
  armor.set_name("Armor Box");
  armor.mutable_box()->add_token_items("coin");
  armor.mutable_box()->add_slots(EQUIP_SLOT_HAT);
  armor.mutable_box()->add_slots(EQUIP_SLOT_CAPE);
  ItemPrototype books;
  books.set_name("Book Box");
  books.mutable_pick_box()->add_equips("Blue Book");
  books.mutable_pick_box()->add_equips("Red Book");
  ItemPrototype rings;
  rings.set_name("Ring Box");
  rings.mutable_ring_box()->add_skills("Ring of Restraint");
  rings.mutable_ring_box()->add_skills("Continuous Ring");
  RingBox::LevelChance* four = rings.mutable_ring_box()->add_levels();
  four->set_level(4);
  four->set_chance(1.0);
  std::unique_ptr<GameState> owner = HeroWith(
      equips, {}, {{"armor", armor}, {"books", books}, {"rings", rings}});
  GameState& state = *owner;
  CharacterInstance& hero = state.character;
  for (const char* worn : {"old_cape", "old_hat"}) {
    hero.PickUp(std::make_unique<EquipInstance>(equips.at(worn)));
    ASSERT_TRUE(hero.Equip(hero.inventory().size() - 1));
  }
  hero.PickUp(std::make_unique<EquipInstance>(equips.at("r1")));
  hero.AddItem(armor, 3);
  hero.AddItem(books, 2);
  hero.AddItem(rings, 3);

  EXPECT_EQ(OpenBoxes(state), 2 + 1 + 2);
  // The cape first, under the weaker piece; then the hat; the third box shut.
  EXPECT_EQ(hero.CountItem("Armor Box"), 1);
  EXPECT_EQ(hero.inventory()[1].prototype().name(), "EQUIP_SLOT_CAPE");
  EXPECT_EQ(hero.inventory()[2].prototype().name(), "EQUIP_SLOT_HAT");
  EXPECT_EQ(hero.inventory()[3].prototype().name(), "Red Book");
  EXPECT_EQ(hero.CountItem("Book Box"), 1);
  // Continuous at 0 against Restraint at 1, then Restraint at 1 against 4.
  EXPECT_EQ(hero.inventory()[4].prototype().name(), "Continuous Ring Lv. 4");
  EXPECT_EQ(hero.inventory()[5].prototype().name(), "Ring of Restraint Lv. 4");
  EXPECT_EQ(hero.CountItem("Ring Box"), 1);
}

// A token piece already worn keeps its slot: outfitting must not swap in an
// older one from the bag, which the next outfit would swap back.
TEST(OutfitTest, KeepsTheWornTokenPiece) {
  std::map<std::string, EquipPrototype> equips;
  for (int level : {160, 200}) {
    EquipPrototype hat =
        Accessory("Hat " + std::to_string(level), level, EQUIP_SLOT_HAT);
    TokenPrice* price = hat.add_token_prices();
    price->set_token_item("coin");
    price->set_count(1);
    equips["hat_" + std::to_string(level)] = hat;
  }
  std::unique_ptr<GameState> owner = HeroWith(equips, {}, {});
  CharacterInstance& hero = owner->character;
  hero.PickUp(std::make_unique<EquipInstance>(equips.at("hat_200")));
  ASSERT_TRUE(hero.Equip(0));
  hero.PickUp(std::make_unique<EquipInstance>(equips.at("hat_160")));

  for (int pass = 0; pass < 2; ++pass) {
    Outfit(*owner, /*budget=*/true, EQUIP_TYPE_ONE_HANDED_SWORD);
    ASSERT_NE(hero.WornAt(kFarmGear, EQUIP_SLOT_HAT), nullptr);
    EXPECT_EQ(hero.WornAt(kFarmGear, EQUIP_SLOT_HAT)->name(), "Hat 200")
        << "pass " << pass;
  }
}

// The Continuous Ring takes its own slot; the Ring of Restraint takes the ring
// slot `power` likes best, once per set of rings.
TEST(WearSkillRingsTest, RestraintTakesTheSlotPowerLikesBest) {
  std::map<std::string, EquipPrototype> equips;
  for (int i = 0; i < 4; ++i) {
    equips["ring" + std::to_string(i)] = Ring("Ring " + std::to_string(i), 150);
  }
  equips["r4"] = SkillRing("Ring of Restraint", 4, EQUIP_SLOT_RING);
  equips["c3"] = SkillRing("Continuous Ring", 3, EQUIP_SLOT_CONT_RING);
  std::unique_ptr<GameState> owner = HeroWith(equips, {}, {});
  GameState& state = *owner;
  CharacterInstance& hero = state.character;
  for (int i = 0; i < 4; ++i) {
    hero.PickUp(
        std::make_unique<EquipInstance>(equips.at("ring" + std::to_string(i))));
    ASSERT_TRUE(hero.Equip(hero.inventory().size() - 1));
  }
  hero.PickUp(std::make_unique<EquipInstance>(equips.at("r4")));
  hero.PickUp(std::make_unique<EquipInstance>(equips.at("c3")));
  int measured = 0;
  // Ring 2 is worth least, so losing it costs least.
  auto power = [&measured](GameState& inner) {
    ++measured;
    double total = 0;
    for (const auto& worn : inner.character.equipped(kBossGear)) {
      total += worn.second->name() == "Ring 2"                  ? 1
               : worn.second->prototype().has_equipment_skill() ? 5
                                                                : 3;
    }
    return total;
  };
  std::string memo;
  WearSkillRings(state, power, &memo);
  const EquipInstance* cont = hero.WornAt(kFarmGear, EQUIP_SLOT_CONT_RING);
  ASSERT_NE(cont, nullptr);
  EXPECT_EQ(cont->name(), "Continuous Ring Lv. 3");
  const EquipInstance* third = hero.WornAt(kBossGear, EQUIP_SLOT_RING_3);
  ASSERT_NE(third, nullptr);
  EXPECT_EQ(third->name(), "Ring of Restraint Lv. 4");
  EXPECT_EQ(hero.WornAt(kFarmGear, EQUIP_SLOT_RING_3)->name(), "Ring 2");
  EXPECT_EQ(measured, 5);
  WearSkillRings(state, power, &memo);
  EXPECT_EQ(measured, 5) << "worn already, so nothing to weigh";
}

// Dropped scrolls fill open slots of what boss fights wear, the biggest first,
// one item each.
TEST(UseDroppedScrollsTest, TheBiggestScrollFillsOpenSlotsFirst) {
  EquipPrototype earring = Accessory("Earring", 150, EQUIP_SLOT_EARRINGS);
  earring.set_upgrade_slots(1);
  auto coupon = [](const std::string& name, int attack) {
    Scroll scroll;
    scroll.set_name(name);
    scroll.set_target(SCROLL_TARGET_ACCESSORY);
    scroll.set_paid_with(name);
    scroll.mutable_stats()->set_attack(attack);
    scroll.mutable_stats()->set_magic_attack(attack);
    scroll.set_success_rate(100);
    scroll.add_applicable_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
    return scroll;
  };
  ItemPrototype plain;
  plain.set_name("Plain");
  ItemPrototype premium;
  premium.set_name("Premium");
  std::unique_ptr<GameState> owner = HeroWith(
      {{"earring", earring}},
      {{"plain", coupon("Plain", 3)}, {"premium", coupon("Premium", 5)}},
      {{"plain", plain}, {"premium", premium}});
  GameState& state = *owner;
  CharacterInstance& hero = state.character;
  hero.PickUp(std::make_unique<EquipInstance>(earring));
  ASSERT_TRUE(hero.Equip(0));
  hero.AddItem(plain, 5);
  hero.AddItem(premium, 1);
  const int slots = hero.WornAt(kBossGear, EQUIP_SLOT_EARRINGS)
                        ->equip_state()
                        .remaining_upgrade_slots();

  EXPECT_EQ(UseDroppedScrolls(state), slots);
  EXPECT_EQ(hero.CountItem("Premium"), 0);
  EXPECT_EQ(hero.CountItem("Plain"), 5 - (slots - 1));
  EXPECT_EQ(hero.WornAt(kBossGear, EQUIP_SLOT_EARRINGS)
                ->equip_state()
                .scroll_stats()
                .attack(),
            5 + 3 * (slots - 1));
}

}  // namespace
}  // namespace ms
