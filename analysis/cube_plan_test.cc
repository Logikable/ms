#include "analysis/cube_plan.h"

#include <gtest/gtest.h>

#include <cmath>
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
// Cubing gear the character will outgrow is discounted to a quarter, so getting
// this wrong misprices every cube on that slot.

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

  // Sets the hat's potential on `track`, which every roll on that track is
  // judged against.
  void Wearing(const Potential& potential,
               PotentialTrack track = PotentialTrack::kMain) {
    EquipInstance* hat = nullptr;
    for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
         state_->character.equipped()) {
      if (worn.first == EQUIP_SLOT_HAT) {
        hat = const_cast<EquipInstance*>(worn.second);
      }
    }
    ASSERT_NE(hat, nullptr);
    hat->SetPotential(track, potential);
  }

  std::unique_ptr<GameState> state_;
  Yardstick yard_;
  CubeBasis basis_;
  CubeIncome income_;
  // A rule with every reservation value at zero, so a roll is taken for its
  // lines alone. Tests of the rank rule set their own.
  CubeProgram program_;
};

// Both sides stay at the same rank, so this tests keep-better and not the rank
// rule. A percent line, since a flat line at Epic is worth nothing.
TEST_F(CubePlanTest, ARollWorthMoreIsTaken) {
  Potential bare;
  bare.set_rank(POTENTIAL_RANK_EPIC);
  Wearing(bare);

  EXPECT_TRUE(WorthTaking(
      *state_, basis_, kBossGear, EQUIP_SLOT_HAT, PotentialTrack::kMain,
      Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_STR_PCT), income_,
      program_));
}

TEST_F(CubePlanTest, ARollWorthLessIsDeclined) {
  Wearing(Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_STR_PCT));

  Potential bare;
  bare.set_rank(POTENTIAL_RANK_EPIC);
  EXPECT_FALSE(WorthTaking(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                           PotentialTrack::kMain, bare, income_, program_));
}

// Under a defence wall every roll deals the 1-damage floor, so accepting on
// damage alone throws away the rank-up the run was bought for: the higher
// rank's reservation value is what it is worth.
TEST_F(CubePlanTest, ARankIsTakenOnItsFuture) {
  program_.reserve[POTENTIAL_RANK_EPIC] = 1.0;
  program_.reserve[POTENTIAL_RANK_UNIQUE] = 2.0;
  // Two potentials of different rank with no lines, so neither changes the
  // damage.
  Potential held;
  held.set_rank(POTENTIAL_RANK_EPIC);
  Potential up;
  up.set_rank(POTENTIAL_RANK_UNIQUE);
  Wearing(held);

  EXPECT_TRUE(WorthTaking(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                          PotentialTrack::kMain, up, income_, program_));

  Potential down;
  down.set_rank(POTENTIAL_RANK_RARE);
  EXPECT_FALSE(WorthTaking(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                           PotentialTrack::kMain, down, income_, program_))
      << "a lower rank worth the same is not a reason to keep it";
}

// A bonus roll adds to the main lines rather than competing with them, so a
// weak one is still worth taking onto an empty bonus potential.
TEST_F(CubePlanTest, ABonusRollIsJudgedAgainstTheBonusPotential) {
  Wearing(Rolled(POTENTIAL_RANK_LEGENDARY, POTENTIAL_LINE_TYPE_STR_PCT));
  Potential bare;
  bare.set_rank(POTENTIAL_RANK_RARE);
  Wearing(bare, PotentialTrack::kBonus);

  EXPECT_TRUE(WorthTaking(
      *state_, basis_, kBossGear, EQUIP_SLOT_HAT, PotentialTrack::kBonus,
      Rolled(POTENTIAL_RANK_RARE, POTENTIAL_LINE_TYPE_BONUS_STR_PCT), income_,
      program_));
  EXPECT_FALSE(WorthTaking(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                           PotentialTrack::kBonus, bare, income_, program_));
}

// A piece worn only while farming is worth its income and nothing else, and a
// piece worn only against bosses earns no income: a meso line belongs on the
// first, a stat line on the second.
TEST_F(CubePlanTest, FarmAndBossPiecesAreEachJudgedOnTheirOwnWork) {
  state_->character.set_autoswap_presets(true);
  Wearing(Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_STR_PCT));
  state_->character.PickUp(std::make_unique<EquipInstance>(Hat("Helm", 60)));
  ASSERT_TRUE(state_->character.Equip(0, kBossGear));
  Potential bare;
  bare.set_rank(POTENTIAL_RANK_LEGENDARY);
  ASSERT_TRUE(state_->character.TakePotential(
      EQUIP_SLOT_HAT, PotentialTrack::kMain, bare, kBossGear));
  ASSERT_NE(state_->character.WornAt(kBossGear, EQUIP_SLOT_HAT),
            state_->character.WornAt(kFarmGear, EQUIP_SLOT_HAT));
  basis_ = CubeBasisFor(*state_, yard_);
  income_.rate = [](double meso_bonus, double) {
    return 1000.0 * (1.0 + meso_bonus);
  };
  income_.seconds_left = 1e5;
  income_.power_per_meso = 1.0;

  const Potential meso =
      Rolled(POTENTIAL_RANK_LEGENDARY, POTENTIAL_LINE_TYPE_MESO_RATE);
  EXPECT_TRUE(WorthTaking(*state_, basis_, kFarmGear, EQUIP_SLOT_HAT,
                          PotentialTrack::kMain, meso, income_, program_));
  EXPECT_FALSE(WorthTaking(
      *state_, basis_, kFarmGear, EQUIP_SLOT_HAT, PotentialTrack::kMain,
      Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_LUK_PCT), income_,
      program_))
      << "farming damage is not what a farm piece is cubed for";
  EXPECT_FALSE(WorthTaking(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                           PotentialTrack::kMain, meso, income_, program_))
      << "a boss-only piece earns nothing while farming";
  EXPECT_TRUE(WorthTaking(
      *state_, basis_, kBossGear, EQUIP_SLOT_HAT, PotentialTrack::kMain,
      Rolled(POTENTIAL_RANK_LEGENDARY, POTENTIAL_LINE_TYPE_STR_PCT), income_,
      program_));
}

// A rank-up whose lines are far worse is still taken, since what the run goes
// on to roll from is the rank.
TEST_F(CubePlanTest, WorseLinesAtAHigherRankAreTaken) {
  Wearing(Rolled(POTENTIAL_RANK_EPIC, POTENTIAL_LINE_TYPE_STR_PCT));
  Potential up;
  up.set_rank(POTENTIAL_RANK_UNIQUE);
  program_.reserve[POTENTIAL_RANK_EPIC] = 1.0;
  program_.reserve[POTENTIAL_RANK_UNIQUE] = 1e12;
  EXPECT_TRUE(WorthTaking(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                          PotentialTrack::kMain, up, income_, program_));
}

TEST_F(CubePlanTest, NothingWornIsNeverWorthTaking) {
  EXPECT_FALSE(WorthTaking(
      *state_, basis_, kBossGear, EQUIP_SLOT_GLOVES, PotentialTrack::kMain,
      Rolled(POTENTIAL_RANK_LEGENDARY, POTENTIAL_LINE_TYPE_STR_PCT), income_,
      program_));
}

// --- BestCubeProgram ---

// A run is compared per meso against everything else on the shelf, so its cost
// must be the cubes it expects to buy, at the cube's own price.
TEST_F(CubePlanTest, ARunIsPricedInItsOwnCubes) {
  Wearing(Rolled(POTENTIAL_RANK_RARE, POTENTIAL_LINE_TYPE_STR));
  for (CubeType cube :
       {CubeType::kRed, CubeType::kBlack, CubeType::kGreen, CubeType::kWhite}) {
    std::mt19937 rng(1234);
    CubeProgram program = BestCubeProgram(*state_, basis_, kBossGear,
                                          EQUIP_SLOT_HAT, cube, income_, rng);
    ASSERT_TRUE(program.worth()) << "a Rare hat has somewhere to climb";
    EXPECT_GE(program.cubes, 1.0);
    EXPECT_EQ(program.cost, std::llround(program.cubes * CubeOf(cube).cost));
  }
}

// A roll costs its price in power at what a meso buys elsewhere, so the dearer
// the meso, the sooner the run stops, until no roll pays at all.
TEST_F(CubePlanTest, DearerMesoStopsSooner) {
  Wearing(Rolled(POTENTIAL_RANK_RARE, POTENTIAL_LINE_TYPE_STR));
  std::mt19937 free_rng(1234);
  const CubeProgram free =
      BestCubeProgram(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                      CubeType::kRed, income_, free_rng);
  ASSERT_TRUE(free.worth());

  income_.power_per_meso = free.gain / free.cost;
  std::mt19937 dear_rng(1234);
  const CubeProgram dear =
      BestCubeProgram(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                      CubeType::kRed, income_, dear_rng);
  ASSERT_TRUE(dear.worth());
  EXPECT_LT(dear.cubes, free.cubes);
  EXPECT_GE(dear.gain / dear.cost, income_.power_per_meso)
      << "a run the rule starts pays at least what the meso buys elsewhere";

  income_.power_per_meso = 1e6;
  std::mt19937 dearest_rng(1234);
  EXPECT_FALSE(BestCubeProgram(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                               CubeType::kRed, income_, dearest_rng)
                   .worth());
}

// Lines no roll can beat are worth no roll, however cheap the meso.
TEST_F(CubePlanTest, TheBestLinesAreNotCubed) {
  Potential best;
  best.set_rank(POTENTIAL_RANK_LEGENDARY);
  for (int i = 0; i < kPotentialLines; ++i) {
    *best.add_lines() =
        Line(POTENTIAL_LINE_TYPE_STR_PCT, POTENTIAL_RANK_LEGENDARY);
  }
  Wearing(best);
  std::mt19937 rng(1234);
  EXPECT_FALSE(BestCubeProgram(*state_, basis_, kBossGear, EQUIP_SLOT_HAT,
                               CubeType::kBlack, income_, rng)
                   .worth());
}

TEST_F(CubePlanTest, NoRunIntoASlotThatTakesNoPotential) {
  std::mt19937 rng(1234);
  CubeProgram program =
      BestCubeProgram(*state_, basis_, kBossGear, EQUIP_SLOT_GLOVES,
                      CubeType::kRed, income_, rng);
  EXPECT_FALSE(program.worth());
}

}  // namespace
}  // namespace ms
