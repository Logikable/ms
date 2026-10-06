#include "analysis/drop_value.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "analysis/sim_gear.h"
#include "analysis/yardstick.h"
#include "src/character/skill_placement.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

EquipPrototype Hat(const std::string& name, int str) {
  EquipPrototype hat;
  hat.set_name(name);
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.set_required_level(30);
  hat.mutable_base_stats()->set_str(str);
  return hat;
}

EquipPrototype Weapon(const std::string& name, int attack, EquipType type) {
  EquipPrototype weapon;
  weapon.set_name(name);
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.set_equip_type(type);
  weapon.set_required_level(30);
  weapon.set_attack_speed(ATTACK_SPEED_AVERAGE);
  weapon.mutable_base_stats()->set_attack(attack);
  return weapon;
}

Skill SlashBlast() {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.set_max_enemies(6);
  slash.mutable_base()->set_skill_pct(1.83);
  return slash;
}

// A level 200 Swordman with one swing, wearing a sword and the "worn" hat in
// every preset. Without a swing the yardstick has no strand and every drop is
// worth nothing (see yardstick_test).
class DropValueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_unique<GameState>(
        std::map<std::string, EquipPrototype>{},
        std::map<std::string, Scroll>{}, std::map<std::string, ItemPrototype>{},
        std::map<std::string, Mob>{{"snail", SnailMob()}},
        std::map<std::string, MapData>{{"field", SnailMap()}},
        std::map<std::string, Skill>{{"slash_blast", SlashBlast()}});
    state_->current_map = "field";
    CharacterInstance& character = state_->character;
    while (character.proto().level() < 200) {
      character.LevelUp();
    }
    while (character.AllocateStat(STAT_FIELD_STR, 1)) {
    }
    Wear(Weapon("Sword", 100, EQUIP_TYPE_ONE_HANDED_SWORD));
    Wear(Hat("Worn Hat", 10));
    character.AdvanceJob(JOB_SWORDMAN);
    character.LearnSkill(SlashBlast(), 1);
  }

  void Wear(const EquipPrototype& proto, StatPreset preset = kFarmGear) {
    state_->character.PickUp(std::make_unique<EquipInstance>(proto));
    ASSERT_TRUE(state_->character.Equip(
        state_->character.inventory().size() - 1, preset));
  }

  DropBasis Basis(bool by_tier) {
    return DropBasisFor(*state_, /*power_per_meso=*/1.0, held_, by_tier);
  }

  std::unique_ptr<GameState> state_;
  HeldYardstick held_;
};

// The closed form prices stats alone, so a heavier axe would outscore the
// sword every swing of theirs fires from.
TEST_F(DropValueTest, AWeaponOfAnotherTypeIsWorthNothing) {
  const DropBasis basis = Basis(/*by_tier=*/false);
  ASSERT_FALSE(basis.yard.strands.empty());
  EXPECT_GT(
      EquipDropValue(*state_, basis,
                     Weapon("Big Sword", 200, EQUIP_TYPE_ONE_HANDED_SWORD)),
      0.0);
  EXPECT_EQ(EquipDropValue(*state_, basis,
                           Weapon("Big Axe", 200, EQUIP_TYPE_ONE_HANDED_AXE)),
            0.0);
}

// By tier, a drop has to beat what either preset wears: a hat the bossing
// preset already has is no upgrade for it, though it beats the farm hat.
TEST_F(DropValueTest, ByTierADropMustBeatBothPresets) {
  Wear(Hat("Boss Hat", 50), kBossGear);
  ASSERT_EQ(state_->character.WornAt(kBossGear, EQUIP_SLOT_HAT)->name(),
            "Boss Hat");
  EXPECT_GT(EquipDropValue(*state_, Basis(false), Hat("Boss Hat", 50)), 0.0);
  EXPECT_EQ(EquipDropValue(*state_, Basis(true), Hat("Boss Hat", 50)), 0.0);
  EXPECT_GT(EquipDropValue(*state_, Basis(true), Hat("Better Hat", 80)), 0.0);
}

// A gear drop's chance scales with drop rate until it is certain, past which
// more rate adds nothing; a stackable's whole rate scales.
TEST_F(DropValueTest, AGearDropStopsCountingOnceCertain) {
  state_->equips["better_hat"] = Hat("Better Hat", 80);
  BossDifficulty difficulty;
  MobDrop* drop = difficulty.add_drops();
  drop->set_equip("better_hat");
  drop->set_per_kill(0.5);
  const DropBasis basis = Basis(true);
  const double value =
      EquipDropValue(*state_, basis, state_->equips.at("better_hat"));
  ASSERT_GT(value, 0.0);
  EXPECT_DOUBLE_EQ(ClearLootPerDropRate(*state_, basis, difficulty, 0.5),
                   0.5 * value);
  EXPECT_EQ(ClearLootPerDropRate(*state_, basis, difficulty, 1.0), 0.0);
}

}  // namespace
}  // namespace ms
