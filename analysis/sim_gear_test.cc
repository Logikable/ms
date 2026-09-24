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

EquipPrototype Ring(const std::string& name, int level) {
  EquipPrototype ring;
  ring.set_name(name);
  ring.set_equip_slot(EQUIP_SLOT_RING);
  ring.set_required_level(level);
  ring.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return ring;
}

// A second copy of a worn ring swaps for it, which sends the worn one back to
// the bag; it is left there rather than traded back and forth for ever. The
// first ring slot holds something weaker, which is what made it look like an
// upgrade every time round.
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

}  // namespace
}  // namespace ms
