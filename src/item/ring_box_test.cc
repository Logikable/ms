#include "src/item/ring_box.h"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <string>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

EquipPrototype Ring(const std::string& skill, int level) {
  EquipPrototype ring;
  ring.set_name(skill + " Lv. " + std::to_string(level));
  ring.mutable_equipment_skill()->set_skill(skill);
  ring.mutable_equipment_skill()->set_level(level);
  return ring;
}

ItemPrototype WhiteJade() {
  ItemPrototype box;
  box.set_name("White Jade Boss Ring Box");
  RingBox* rings = box.mutable_ring_box();
  rings->add_skills("Ring of Restraint");
  RingBox::LevelChance* three = rings->add_levels();
  three->set_level(3);
  three->set_chance(0.65);
  RingBox::LevelChance* four = rings->add_levels();
  four->set_level(4);
  four->set_chance(0.35);
  rings->add_items("grindstone_of_life");
  return box;
}

TEST(RingBoxTest, HoldsItsSkillsAtItsLevelsAndItsItems) {
  ItemPrototype box = WhiteJade();
  EXPECT_TRUE(RingBoxHolds(box, Ring("Ring of Restraint", 4)));
  EXPECT_FALSE(RingBoxHolds(box, Ring("Ring of Restraint", 2)));
  EXPECT_FALSE(RingBoxHolds(box, Ring("Continuous Ring", 4)));
  EXPECT_TRUE(RingBoxHolds(box, "grindstone_of_life"));
  EXPECT_FALSE(RingBoxHolds(box, "spell_trace"));
  EXPECT_FALSE(RingBoxHolds(ItemPrototype(), Ring("Ring of Restraint", 4)));
  EXPECT_EQ(RingLevelRange(box.ring_box()), "Lv. 3-4");
}

TEST(RingBoxTest, RingAtFindsTheLevel) {
  std::map<std::string, EquipPrototype> equips = {
      {"r3", Ring("Ring of Restraint", 3)},
      {"r4", Ring("Ring of Restraint", 4)},
  };
  ASSERT_NE(RingAt("Ring of Restraint", 4, equips), nullptr);
  EXPECT_EQ(RingAt("Ring of Restraint", 4, equips)->name(),
            "Ring of Restraint Lv. 4");
  EXPECT_EQ(RingAt("Ring of Restraint", 5, equips), nullptr);
}

TEST(RingBoxTest, RollsTheLevelsAtTheirChances) {
  RingBox box = WhiteJade().ring_box();
  std::mt19937 rng(7);
  constexpr int kRolls = 20000;
  int fours = 0;
  for (int i = 0; i < kRolls; ++i) {
    int level = RollRingLevel(box, rng);
    ASSERT_TRUE(level == 3 || level == 4);
    fours += level == 4 ? 1 : 0;
  }
  EXPECT_NEAR(static_cast<double>(fours) / kRolls, 0.35, 0.02);
}

}  // namespace
}  // namespace ms
