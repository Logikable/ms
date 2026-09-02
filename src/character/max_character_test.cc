#include "src/character/max_character.h"

#include <gtest/gtest.h>

#include <random>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// A level 200 4th job, which is the only character with a full Hyper Stat
// pool to spend and every band of gear behind them.
Character MaxProto(Job job = JOB_HERO, int level = 200) {
  Character proto;
  proto.set_level(level);
  proto.set_job(job);
  return proto;
}

int LinesOf(const Potential& potential, PotentialLineType type) {
  int found = 0;
  for (const PotentialLine& line : potential.lines()) {
    found += line.type() == type ? 1 : 0;
  }
  return found;
}

// The bands climb: nothing is ever taken away as the level rises, and every
// one of them is inside the income of the level it opens at -- the table in
// max_character.cc carries that arithmetic.
TEST(MaxCharacterTest, GearClimbsWithTheLevel) {
  EXPECT_FALSE(MaxGearForLevel(140).hammered);
  EXPECT_EQ(MaxGearForLevel(140).stars, 10);
  EXPECT_EQ(MaxGearForLevel(140).weapon_stars, 14);
  EXPECT_EQ(MaxGearForLevel(140).armour_potential, POTENTIAL_RANK_UNSPECIFIED);

  EXPECT_TRUE(MaxGearForLevel(200).hammered);
  EXPECT_EQ(MaxGearForLevel(200).stars, 12);
  EXPECT_EQ(MaxGearForLevel(200).weapon_stars, 15);
  EXPECT_EQ(MaxGearForLevel(200).armour_potential, POTENTIAL_RANK_EPIC);
  EXPECT_EQ(MaxGearForLevel(200).weaponry_potential, POTENTIAL_RANK_UNIQUE);

  int last_stars = 0;
  bool last_hammered = false;
  for (int level = 1; level <= 200; ++level) {
    const MaxGear gear = MaxGearForLevel(level);
    EXPECT_GE(gear.stars, last_stars) << "at level " << level;
    EXPECT_GE(gear.weapon_stars, gear.stars) << "at level " << level;
    EXPECT_TRUE(gear.hammered || !last_hammered) << "at level " << level;
    last_stars = gear.stars;
    last_hammered = gear.hammered;
  }
}

// Every piece of a kind carries the same three lines, and the %stat follows
// the job: an armour piece is three lots of what the character fights with.
TEST(MaxCharacterTest, ArmourCarriesThreeLinesOfThePrimaryStat) {
  const MaxGear gear = MaxGearForLevel(200);
  const Potential hat = MaxPotentialFor(EQUIP_SLOT_HAT, gear, STAT_FIELD_STR);
  EXPECT_EQ(hat.rank(), POTENTIAL_RANK_EPIC);
  ASSERT_EQ(hat.lines_size(), kPotentialLines);
  EXPECT_EQ(LinesOf(hat, POTENTIAL_LINE_TYPE_STR_PCT), kPotentialLines);
  // Only the top line carries the potential's own rank.
  EXPECT_EQ(hat.lines(0).rank(), POTENTIAL_RANK_EPIC);
  EXPECT_EQ(hat.lines(1).rank(), POTENTIAL_RANK_RARE);

  const Potential ring = MaxPotentialFor(EQUIP_SLOT_RING, gear, STAT_FIELD_LUK);
  EXPECT_EQ(LinesOf(ring, POTENTIAL_LINE_TYPE_LUK_PCT), kPotentialLines);
}

// The weapon and the secondary are what a bossing player really cubes for, so
// each carries the line it is cubed for and two lots of the attack share. A
// magician's reads M.ATT.
TEST(MaxCharacterTest, WeaponryCarriesTheLineItIsCubedFor) {
  const MaxGear gear = MaxGearForLevel(200);
  const Potential weapon =
      MaxPotentialFor(EQUIP_SLOT_PRIMARY_WEAPON, gear, STAT_FIELD_STR);
  EXPECT_EQ(weapon.rank(), POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(weapon.lines(0).type(), POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30);
  EXPECT_EQ(LinesOf(weapon, POTENTIAL_LINE_TYPE_ATTACK_PCT), 2);

  const Potential secondary =
      MaxPotentialFor(EQUIP_SLOT_SECONDARY, gear, STAT_FIELD_INT);
  EXPECT_EQ(secondary.lines(0).type(), POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30);
  EXPECT_EQ(LinesOf(secondary, POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT), 2);

  const Potential emblem =
      MaxPotentialFor(EQUIP_SLOT_EMBLEM, gear, STAT_FIELD_STR);
  EXPECT_EQ(LinesOf(emblem, POTENTIAL_LINE_TYPE_ATTACK_PCT), kPotentialLines);
}

// A slot that takes no potential gets none, and neither does a level with no
// cubing behind it.
TEST(MaxCharacterTest, NothingIsCubedThatCannotBe) {
  EXPECT_EQ(
      MaxPotentialFor(EQUIP_SLOT_POCKET, MaxGearForLevel(200), STAT_FIELD_STR)
          .lines_size(),
      0);
  EXPECT_EQ(
      MaxPotentialFor(EQUIP_SLOT_HAT, MaxGearForLevel(140), STAT_FIELD_STR)
          .lines_size(),
      0);
}

// The pool is spent to the point where nothing left is affordable, over
// several stats rather than two maxed ones -- a level's price climbs with the
// level it reaches, so the last twenty points buy four levels of a stat
// standing at zero and none of one already at six.
TEST(MaxCharacterTest, HyperStatsSpendThePoolAcrossTheList) {
  std::mt19937 rng(1);
  CharacterInstance character(rng, MaxProto());
  SpendMaxHyperStats(character);

  for (StatPreset preset : {StatPreset::kFarming, StatPreset::kBossing}) {
    // What is left over is the tail of the ladder: every stat on the list is
    // high enough that its next level costs more than the change.
    EXPECT_LT(character.hyper_stat_points_left(preset),
              TotalHyperStatPoints(200) / 20)
        << "the pool went largely unspent";
    EXPECT_GE(character.hyper_stat_level(HYPER_STAT_FIELD_ATTACK, preset), 5);
    EXPECT_LE(character.hyper_stat_level(HYPER_STAT_FIELD_ATTACK, preset),
              character.max_hyper_stat_level());
  }
  // The two presets differ where the fight does.
  EXPECT_GT(character.hyper_stat_level(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                       StatPreset::kBossing),
            0);
  EXPECT_EQ(character.hyper_stat_level(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                       StatPreset::kFarming),
            0);
}

// Allocating twice hands back the same allocation: the previous one is thrown
// away rather than added to.
TEST(MaxCharacterTest, HyperStatsAreReallocatedFromScratch) {
  std::mt19937 rng(1);
  CharacterInstance character(rng, MaxProto());
  SpendMaxHyperStats(character);
  const int spent = character.hyper_stat_points_left(StatPreset::kBossing);
  SpendMaxHyperStats(character);
  EXPECT_EQ(character.hyper_stat_points_left(StatPreset::kBossing), spent);
}

// One Legendary line and two Epic ones, which is the shape a reset chase
// lands on. The stat line follows the job; the top line follows the preset.
TEST(MaxCharacterTest, AbilityHoldsOneLegendaryLine) {
  const AbilityPreset boss =
      MaxAbilityPreset(StatPreset::kBossing, STAT_FIELD_LUK);
  EXPECT_EQ(boss.rank(), ABILITY_RANK_LEGENDARY);
  ASSERT_EQ(boss.lines_size(), kAbilityLines);
  EXPECT_EQ(boss.lines(0).type(), ABILITY_LINE_TYPE_CRIT_RATE);
  EXPECT_EQ(boss.lines(0).rank(), ABILITY_RANK_LEGENDARY);
  EXPECT_EQ(boss.lines(1).rank(), ABILITY_RANK_EPIC);
  EXPECT_EQ(boss.lines(2).type(), ABILITY_LINE_TYPE_LUK);

  const AbilityPreset farm =
      MaxAbilityPreset(StatPreset::kFarming, STAT_FIELD_INT);
  EXPECT_EQ(farm.lines(0).type(), ABILITY_LINE_TYPE_NORMAL_DAMAGE);
  EXPECT_EQ(farm.lines(2).type(), ABILITY_LINE_TYPE_INT);
  // No two lines ever share a type, which the roll itself guarantees.
  EXPECT_NE(farm.lines(0).type(), farm.lines(1).type());
  EXPECT_NE(farm.lines(1).type(), farm.lines(2).type());

  // A magician swings on magic attack, so that is the line they hold.
  EXPECT_EQ(
      MaxAbilityPreset(StatPreset::kBossing, STAT_FIELD_INT).lines(1).type(),
      ABILITY_LINE_TYPE_MAGIC_ATTACK);
}

}  // namespace
}  // namespace ms
