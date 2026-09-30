#include "src/character/max_character.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// A level 200 4th job, with a full Hyper Stat pool to spend and every gear band
// behind them.
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

// The bands only improve: nothing is removed as the level rises.
TEST(MaxCharacterTest, GearClimbsWithTheLevel) {
  EXPECT_FALSE(MaxGearForLevel(140).hammered);
  EXPECT_EQ(MaxGearForLevel(140).stars, 10);
  EXPECT_EQ(MaxGearForLevel(140).weapon_stars, 14);
  EXPECT_EQ(MaxGearForLevel(140).potential_level, 0);

  EXPECT_TRUE(MaxGearForLevel(200).hammered);
  EXPECT_EQ(MaxGearForLevel(200).stars, 12);
  EXPECT_EQ(MaxGearForLevel(200).weapon_stars, 14);
  EXPECT_EQ(MaxGearForLevel(200).potential_level, 0);
  EXPECT_EQ(MaxGearForLevel(230).stars, 12);
  EXPECT_EQ(MaxGearForLevel(260).stars, 17);
  EXPECT_EQ(MaxGearForLevel(260).weapon_stars, 15);
  EXPECT_EQ(MaxGearForLevel(230).potential_level, 230);
  EXPECT_EQ(MaxGearForLevel(260).potential_level, 260);

  int last_stars = 0;
  int last_weapon_stars = 0;
  int last_potentials = 0;
  bool last_hammered = false;
  for (int level = 1; level <= 260; ++level) {
    const MaxGear gear = MaxGearForLevel(level);
    EXPECT_GE(gear.stars, last_stars) << "at level " << level;
    EXPECT_GE(gear.weapon_stars, last_weapon_stars) << "at level " << level;
    EXPECT_TRUE(gear.hammered || !last_hammered) << "at level " << level;
    EXPECT_GE(gear.potential_level, last_potentials) << "at level " << level;
    last_stars = gear.stars;
    last_weapon_stars = gear.weapon_stars;
    last_hammered = gear.hammered;
    last_potentials = gear.potential_level;
  }
}

// A line the cube could never put there would be a character no player can
// be, so every line of every band must be in its slot's pool at its rank.
TEST(MaxCharacterTest, EveryLineCouldHaveBeenRolled) {
  const EquipSlot kSlots[] = {EQUIP_SLOT_PRIMARY_WEAPON, EQUIP_SLOT_SECONDARY,
                              EQUIP_SLOT_EMBLEM,         EQUIP_SLOT_HAT,
                              EQUIP_SLOT_GLOVES,         EQUIP_SLOT_TOP,
                              EQUIP_SLOT_RING,           EQUIP_SLOT_PENDANT};
  const StatField kStats[] = {STAT_FIELD_STR, STAT_FIELD_DEX, STAT_FIELD_INT,
                              STAT_FIELD_LUK};
  int checked = 0;
  for (int level : {230, 260}) {
    for (PotentialTrack track :
         {PotentialTrack::kMain, PotentialTrack::kBonus}) {
      for (EquipSlot slot : kSlots) {
        for (StatField stat : kStats) {
          const Potential potential =
              MaxPotentialFor(slot, MaxGearForLevel(level), stat, track);
          for (const PotentialLine& line : potential.lines()) {
            std::vector<PotentialLineType> pool =
                PotentialPool(track, PotentialGroupOf(slot), line.rank());
            EXPECT_NE(std::find(pool.begin(), pool.end(), line.type()),
                      pool.end())
                << PotentialLineType_Name(line.type()) << " at "
                << PotentialRank_Name(line.rank()) << " on "
                << EquipSlot_Name(slot) << ", level " << level;
            ++checked;
          }
        }
      }
    }
  }
  EXPECT_GT(checked, 0);
}

// The %stat follows the job, and only the first line is prime.
TEST(MaxCharacterTest, ArmourCarriesThePrimaryStat) {
  const MaxGear gear = MaxGearForLevel(260);
  const Potential top = MaxPotentialFor(EQUIP_SLOT_TOP, gear, STAT_FIELD_STR,
                                        PotentialTrack::kMain);
  EXPECT_EQ(top.rank(), POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(LinesOf(top, POTENTIAL_LINE_TYPE_STR_PCT), 2);
  EXPECT_EQ(LinesOf(top, POTENTIAL_LINE_TYPE_ALL_STATS_PCT), 1);
  EXPECT_EQ(top.lines(0).rank(), POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(top.lines(1).rank(), POTENTIAL_RANK_UNIQUE);

  const Potential ring = MaxPotentialFor(EQUIP_SLOT_RING, gear, STAT_FIELD_LUK,
                                         PotentialTrack::kBonus);
  EXPECT_EQ(ring.rank(), POTENTIAL_RANK_EPIC);
  EXPECT_EQ(LinesOf(ring, POTENTIAL_LINE_TYPE_BONUS_LUK_PCT), 2);
}

// The weapon and secondary hold boss damage and ignored defence; bonus
// weaponry holds attack.
TEST(MaxCharacterTest, WeaponryCarriesItsOwnLines) {
  const MaxGear gear = MaxGearForLevel(260);
  const Potential weapon = MaxPotentialFor(
      EQUIP_SLOT_PRIMARY_WEAPON, gear, STAT_FIELD_INT, PotentialTrack::kMain);
  EXPECT_EQ(weapon.rank(), POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(weapon.lines(0).type(), POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40);
  EXPECT_EQ(LinesOf(weapon, POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30), 1);

  const Potential bonus = MaxPotentialFor(
      EQUIP_SLOT_EMBLEM, gear, STAT_FIELD_INT, PotentialTrack::kBonus);
  EXPECT_EQ(LinesOf(bonus, POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT), 3);

  // Crit damage only rolls at Legendary, so both its lines are prime.
  const Potential gloves = MaxPotentialFor(
      EQUIP_SLOT_GLOVES, gear, STAT_FIELD_DEX, PotentialTrack::kMain);
  EXPECT_EQ(LinesOf(gloves, POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT), 2);
  EXPECT_EQ(gloves.lines(1).rank(), POTENTIAL_RANK_LEGENDARY);

  const Potential early =
      MaxPotentialFor(EQUIP_SLOT_SECONDARY, MaxGearForLevel(230),
                      STAT_FIELD_STR, PotentialTrack::kMain);
  EXPECT_EQ(early.lines(0).type(), POTENTIAL_LINE_TYPE_ATTACK_PCT);
}

// A slot that takes no potential gets none, nor does a level with no cubing,
// nor bonus potential before it opens.
TEST(MaxCharacterTest, NothingIsCubedThatCannotBe) {
  EXPECT_EQ(MaxPotentialFor(EQUIP_SLOT_POCKET, MaxGearForLevel(260),
                            STAT_FIELD_STR, PotentialTrack::kMain)
                .lines_size(),
            0);
  EXPECT_EQ(MaxPotentialFor(EQUIP_SLOT_HAT, MaxGearForLevel(229),
                            STAT_FIELD_STR, PotentialTrack::kMain)
                .lines_size(),
            0);
  EXPECT_EQ(MaxPotentialFor(EQUIP_SLOT_HAT, MaxGearForLevel(230),
                            STAT_FIELD_STR, PotentialTrack::kBonus)
                .lines_size(),
            0);
}

}  // namespace
}  // namespace ms
