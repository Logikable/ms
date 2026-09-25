#include "analysis/cube_plan.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>

#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/character/skill_placement.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

EquipPrototype Hat(const std::string& name, int level) {
  EquipPrototype hat;
  hat.set_name(name);
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.set_required_level(level);
  hat.mutable_base_stats()->set_str(10);
  return hat;
}

EquipPrototype Weapon(const std::string& name, int level, EquipType type) {
  EquipPrototype weapon;
  weapon.set_name(name);
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.set_equip_type(type);
  weapon.set_required_level(level);
  weapon.set_attack_speed(ATTACK_SPEED_AVERAGE);
  weapon.mutable_base_stats()->set_attack(100);
  weapon.mutable_base_stats()->set_magic_attack(100);
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

std::unique_ptr<GameState> Shopper(
    std::map<std::string, EquipPrototype> catalog) {
  auto state = std::make_unique<GameState>(
      std::move(catalog), std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"snail", SnailMob()}},
      std::map<std::string, MapData>{{"field", SnailMap()}},
      std::map<std::string, Skill>{{"slash_blast", SlashBlast()}});
  state->current_map = "field";
  return state;
}

void WearNew(GameState& state, const EquipPrototype& proto) {
  state.character.PickUp(std::make_unique<EquipInstance>(proto));
  state.character.Equip(0);
}

// Levels up and spends the AP. A percentage line is worth a share of a stat, so
// a character who never allocated would have nothing for it to scale.
void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
  while (state.character.AllocateStat(STAT_FIELD_STR, 1)) {
  }
}

// Learns an attack skill. Without one the yardstick has no strand and every
// candidate is worth zero (see yardstick_test).
void LearnASwing(GameState& state) {
  while (state.character.proto().job_stage() < 1) {
    if (state.character.CanAdvanceJob()) {
      state.character.AdvanceJob(JOB_SWORDMAN);
    } else {
      state.character.LevelUp();
    }
  }
  state.character.LearnSkill(SlashBlast(), 1);
}

PotentialLine Line(PotentialLineType type, PotentialRank rank) {
  PotentialLine line;
  line.set_type(type);
  line.set_rank(rank);
  return line;
}

Potential Rolled(PotentialRank rank, PotentialLineType type) {
  Potential potential;
  potential.set_rank(rank);
  *potential.add_lines() = Line(type, rank);
  return potential;
}

// --- Replaceable ---
//
// This decides how much of a cube's gain the shopper counts: cubing gear you'll
// outgrow is discounted to a quarter. Getting it wrong either way misprices
// every cube on that slot.

TEST(ReplaceableTest, ABetterPieceTheCharacterCanAlreadyWear) {
  std::unique_ptr<GameState> state =
      Shopper({{"cap", Hat("Cap", 30)}, {"helm", Hat("Helm", 60)}});
  LevelTo(*state, 70);
  WearNew(*state, Hat("Cap", 30));

  EXPECT_TRUE(Replaceable(*state, EQUIP_SLOT_HAT));
}

TEST(ReplaceableTest, NotOneTheirLevelHasNotReached) {
  std::unique_ptr<GameState> state =
      Shopper({{"cap", Hat("Cap", 30)}, {"helm", Hat("Helm", 150)}});
  LevelTo(*state, 70);
  WearNew(*state, Hat("Cap", 30));

  EXPECT_FALSE(Replaceable(*state, EQUIP_SLOT_HAT));
}

// A branch's weapon type is chosen by measurement, not level, so a Lv140 sword
// doesn't replace a Lv120 axe.
TEST(ReplaceableTest, OnlyALongerLadderOfTheSameWeaponType) {
  std::unique_ptr<GameState> state =
      Shopper({{"axe", Weapon("Axe", 30, EQUIP_TYPE_ONE_HANDED_AXE)},
               {"sword", Weapon("Sword", 60, EQUIP_TYPE_ONE_HANDED_SWORD)}});
  LevelTo(*state, 70);
  WearNew(*state, Weapon("Axe", 30, EQUIP_TYPE_ONE_HANDED_AXE));
  EXPECT_FALSE(Replaceable(*state, EQUIP_SLOT_PRIMARY_WEAPON));

  state->equips["big_axe"] = Weapon("Big Axe", 60, EQUIP_TYPE_ONE_HANDED_AXE);
  EXPECT_TRUE(Replaceable(*state, EQUIP_SLOT_PRIMARY_WEAPON));
}

TEST(ReplaceableTest, NothingWornIsNothingToReplace) {
  std::unique_ptr<GameState> state = Shopper({{"cap", Hat("Cap", 30)}});
  EXPECT_FALSE(Replaceable(*state, EQUIP_SLOT_HAT));
}

// --- WorthTaking ---

class CubePlanTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = Shopper({{"cap", Hat("Cap", 60)}});
    LevelTo(*state_, 70);
    WearNew(*state_, Weapon("Sword", 30, EQUIP_TYPE_ONE_HANDED_SWORD));
    WearNew(*state_, Hat("Cap", 60));
    LearnASwing(*state_);
    yard_ = YardstickFor(*state_);
    ASSERT_FALSE(yard_.strands.empty())
        << "without a strand every roll prices at nothing and the tests below "
           "would pass on the rank rule alone";
    basis_ = CubeBasisFor(*state_, yard_);
  }

  // Sets the hat's current potential, which every roll is judged against.
  void Wearing(const Potential& potential) {
    EquipInstance* hat = nullptr;
    for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
         state_->character.equipped()) {
      if (worn.first == EQUIP_SLOT_HAT) {
        hat = const_cast<EquipInstance*>(worn.second);
      }
    }
    ASSERT_NE(hat, nullptr);
    hat->SetPotential(potential);
  }

  std::unique_ptr<GameState> state_;
  Yardstick yard_;
  CubeBasis basis_;
  CubeIncome income_;
};

// Both sides stay at the same rank, so this tests the keep-better rule and not
// the rank rule below. It uses a percent line because flat lines are Rare-only
// by design: a flat STR line at Epic is worth nothing, and the comparison would
// be between two zeroes.
TEST_F(CubePlanTest, ARollWorthMoreIsTaken) {
  Potential bare;
  bare.set_rank(POTENTIAL_RANK_EPIC);
  Wearing(bare);

  EXPECT_TRUE(WorthTaking(
      *state_, basis_, EQUIP_SLOT_HAT,
      Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_STR_PCT), income_));
}

TEST_F(CubePlanTest, ARollWorthLessIsDeclined) {
  Wearing(Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_STR_PCT));

  Potential bare;
  bare.set_rank(POTENTIAL_RANK_EPIC);
  EXPECT_FALSE(WorthTaking(*state_, basis_, EQUIP_SLOT_HAT, bare, income_));
}

// A rule the shopper once broke: under a defence wall every roll is worth
// nothing, since both sides deal the 1-damage floor, so accepting on damage
// alone throws away the rank-up the run was bought for. That cost 2,484 cubes
// with none kept. A higher rank is taken even when damage doesn't change.
TEST_F(CubePlanTest, ARankIsTakenEvenWhereTheDamageDoesNotMove) {
  // Two potentials of different rank with no lines, so neither changes the
  // damage.
  Potential held;
  held.set_rank(POTENTIAL_RANK_EPIC);
  Potential up;
  up.set_rank(POTENTIAL_RANK_UNIQUE);
  Wearing(held);

  EXPECT_TRUE(WorthTaking(*state_, basis_, EQUIP_SLOT_HAT, up, income_));

  Potential down;
  down.set_rank(POTENTIAL_RANK_RARE);
  EXPECT_FALSE(WorthTaking(*state_, basis_, EQUIP_SLOT_HAT, down, income_))
      << "a lower rank worth the same is not a reason to keep it";
}

TEST_F(CubePlanTest, NothingWornIsNeverWorthTaking) {
  EXPECT_FALSE(WorthTaking(
      *state_, basis_, EQUIP_SLOT_GLOVES,
      Rolled(POTENTIAL_RANK_LEGENDARY, POTENTIAL_LINE_TYPE_STR_PCT), income_));
}

// --- BestCubeProgram ---

TEST_F(CubePlanTest, PricesARunIntoASlotThatTakesPotential) {
  Wearing(Rolled(POTENTIAL_RANK_RARE, POTENTIAL_LINE_TYPE_STR));
  std::mt19937 rng(1234);

  CubeProgram program =
      BestCubeProgram(*state_, basis_, EQUIP_SLOT_HAT, income_, rng);
  ASSERT_TRUE(program.worth()) << "a Rare hat has somewhere to climb";
  EXPECT_GT(program.cubes, 0);
  EXPECT_GT(program.gain, 0.0);
  EXPECT_GT(program.cost, 0);
}

// A run is compared per meso against everything else on the shelf, so its cost
// must match the cubes it buys.
TEST_F(CubePlanTest, TheCostIsTheCubesItMeansToBuy) {
  Wearing(Rolled(POTENTIAL_RANK_RARE, POTENTIAL_LINE_TYPE_STR));
  std::mt19937 rng(1234);

  CubeProgram program =
      BestCubeProgram(*state_, basis_, EQUIP_SLOT_HAT, income_, rng);
  ASSERT_TRUE(program.worth());
  EXPECT_EQ(program.cost % program.cubes, 0)
      << "every cube in the run costs the same";
}

TEST_F(CubePlanTest, NoRunIntoASlotThatTakesNoPotential) {
  std::mt19937 rng(1234);
  CubeProgram program =
      BestCubeProgram(*state_, basis_, EQUIP_SLOT_GLOVES, income_, rng);
  EXPECT_FALSE(program.worth());
  EXPECT_EQ(program.cubes, 0);
}

}  // namespace
}  // namespace ms
