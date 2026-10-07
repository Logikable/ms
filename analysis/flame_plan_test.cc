#include "analysis/flame_plan.h"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <string>

#include "analysis/cube_plan.h"
#include "analysis/yardstick.h"
#include "src/character/skill_placement.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

EquipPrototype Piece(const std::string& name, EquipSlot slot) {
  EquipPrototype piece;
  piece.set_name(name);
  piece.set_equip_slot(slot);
  piece.set_required_level(150);
  piece.mutable_base_stats()->set_str(10);
  return piece;
}

EquipPrototype Sword() {
  EquipPrototype weapon;
  weapon.set_name("Sword");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  weapon.set_required_level(30);
  weapon.set_attack_speed(ATTACK_SPEED_AVERAGE);
  weapon.mutable_base_stats()->set_attack(100);
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

FlameLines Lines(FlameStat stat, int tier) {
  FlameLines lines;
  for (int i = 0; i < kFlameLines; ++i) {
    FlameLine* line = lines.Add();
    line->set_stat(stat);
    line->set_tier(tier);
  }
  return lines;
}

class FlamePlanTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_unique<GameState>(
        std::map<std::string, EquipPrototype>{},
        std::map<std::string, Scroll>{}, std::map<std::string, ItemPrototype>{},
        std::map<std::string, Mob>{{"snail", SnailMob()}},
        std::map<std::string, MapData>{{"field", SnailMap()}},
        std::map<std::string, Skill>{{"slash_blast", SlashBlast()}});
    state_->current_map = "field";
    while (state_->character.proto().level() < 150) {
      if (state_->character.CanAdvanceJob() &&
          state_->character.proto().job_stage() < 1) {
        state_->character.AdvanceJob(JOB_SWORDMAN);
      }
      state_->character.LevelUp();
    }
    while (state_->character.AllocateStat(STAT_FIELD_STR, 1)) {
    }
    state_->character.LearnSkill(SlashBlast(), 1);
    for (const EquipPrototype& proto : {Sword(), Piece("Cap", EQUIP_SLOT_HAT),
                                        Piece("Ring", EQUIP_SLOT_RING)}) {
      state_->character.PickUp(std::make_unique<EquipInstance>(proto));
      state_->character.Equip(0);
    }
    yard_ = YardstickFor(*state_);
    ASSERT_FALSE(yard_.strands.empty());
    basis_ = CubeBasisFor(*state_, yard_);
  }

  FlameProgram Program(FlameType flame, double power_per_meso) {
    std::mt19937 rng(1234);
    return BestFlameProgram(*state_, basis_, EQUIP_SLOT_HAT, flame,
                            power_per_meso, rng);
  }

  std::unique_ptr<GameState> state_;
  Yardstick yard_;
  CubeBasis basis_;
};

// A run ranks per meso against the shelf, so it is priced in its own flames;
// a dearer meso stops it sooner, and at a price no roll pays it isn't offered.
TEST_F(FlamePlanTest, RunStopsWithTheMeso) {
  for (FlameType flame : {FlameType::kBurning, FlameType::kBlack}) {
    const FlameProgram free = Program(flame, 0.0);
    ASSERT_TRUE(free.worth()) << FlameName(flame);
    EXPECT_GE(free.flames, 1.0);
    EXPECT_EQ(free.cost, std::llround(free.flames * FlameOf(flame).cost));

    const FlameProgram dear = Program(flame, 0.5 * free.gain / free.cost);
    ASSERT_TRUE(dear.worth());
    EXPECT_LT(dear.flames, free.flames);

    EXPECT_FALSE(Program(flame, 1e6).worth());
  }
}

// The lines are valued as the character uses them: a STR warrior gains from
// STR and not from LUK, and Black keeps the better of the two.
TEST_F(FlamePlanTest, BlackKeepsOnlyARollWorthMore) {
  const FlameProgram program = Program(FlameType::kBlack, 0.0);
  ASSERT_TRUE(program.worth());
  const FlameRun run(*state_, yard_, EQUIP_SLOT_HAT, program);
  EXPECT_TRUE(run.Takes(*state_, Lines(FLAME_STAT_STR, 4)));
  EXPECT_FALSE(run.Takes(*state_, Lines(FLAME_STAT_LUK, 7)));

  state_->character.TakeFlame(EQUIP_SLOT_HAT, Lines(FLAME_STAT_STR, 7));
  EXPECT_FALSE(run.Takes(*state_, Lines(FLAME_STAT_STR, 4)));
  EXPECT_FALSE(run.Continues(*state_))
      << "the top tier in the primary stat is past any reservation value";
}

// A ring takes no flame, as in GMS.
TEST_F(FlamePlanTest, NothingIsOfferedWhereAFlameCannotGo) {
  std::mt19937 rng(1);
  EXPECT_FALSE(BestFlameProgram(*state_, basis_, EQUIP_SLOT_RING,
                                FlameType::kBurning, 0.0, rng)
                   .worth());
}

}  // namespace
}  // namespace ms
