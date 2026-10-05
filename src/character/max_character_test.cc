#include "src/character/max_character.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/character/stat_preset.h"
#include "src/item/flame.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

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

// Stars and cubes only improve as the level rises. The weapon's stars may
// drop, since a new weapon tier arrives with fewer.
TEST(MaxCharacterTest, GearClimbsWithTheLevel) {
  EXPECT_EQ(MaxGearForLevel(140).stars, 8);
  EXPECT_EQ(MaxGearForLevel(140).weapon_stars, 14);
  EXPECT_EQ(MaxGearForLevel(140).potential_level, 0);

  const MaxGear at200 = MaxGearForLevel(200);
  EXPECT_EQ(at200.stars, 10);
  EXPECT_EQ(at200.potential_level, 0);
  EXPECT_EQ(MaxGearForLevel(230).weapon_stars, 13);
  EXPECT_EQ(MaxGearForLevel(260).stars, 11);
  EXPECT_EQ(MaxGearForLevel(260).weapon_stars, 14);
  EXPECT_EQ(MaxGearForLevel(230).potential_level, 230);
  EXPECT_EQ(MaxGearForLevel(260).potential_level, 260);

  int last_stars = 0;
  int last_potentials = 0;
  for (int level = 1; level <= 260; ++level) {
    const MaxGear gear = MaxGearForLevel(level);
    EXPECT_GE(gear.stars, last_stars) << "at level " << level;
    EXPECT_GE(gear.potential_level, last_potentials) << "at level " << level;
    last_stars = gear.stars;
    last_potentials = gear.potential_level;
  }
}

// Each job wears its own piece of a set, every slot holds one piece, and a
// level between checkpoints wears the one below.
TEST(MaxCharacterTest, TheOutfitFollowsTheSweep) {
  auto wears = [](Job job, int level, const std::string& key) {
    const std::vector<std::string> keys = MaxOutfit(job, level);
    return std::find(keys.begin(), keys.end(), key) != keys.end();
  };
  EXPECT_TRUE(MaxOutfit(JOB_HERO, 99).empty());
  EXPECT_TRUE(wears(JOB_HERO, 100, "frozen_hat"));
  EXPECT_FALSE(wears(JOB_HERO, 100, "lightning_god_ring"));
  EXPECT_TRUE(wears(JOB_HERO, 229, "frozen_hat"));
  EXPECT_TRUE(wears(JOB_HERO, 230, "royal_warrior_helm"));
  EXPECT_TRUE(wears(JOB_BISHOP, 230, "royal_dunwitch_hat"));
  EXPECT_TRUE(wears(JOB_HERO, 230, "absolab_broad_axe"));
  EXPECT_TRUE(wears(JOB_BISHOP, 230, "absolab_spellsong_staff"));
  EXPECT_TRUE(wears(JOB_NIGHT_LORD, 230, "princess_nos_charm"));
  EXPECT_TRUE(wears(JOB_MARKSMAN, 230, "falcon_wing_sentinel_shoulder"));
  EXPECT_TRUE(wears(JOB_SHADOWER, 260, "arcane_umbra_thief_shoulder"));
  EXPECT_TRUE(wears(JOB_HERO, 260, "absolab_knight_armor"));
  EXPECT_TRUE(wears(JOB_HERO, 260, "guardian_angel_ring"));
  EXPECT_FALSE(wears(JOB_HERO, 260, "lightning_god_ring"));
  for (Job job : {JOB_HERO, JOB_BISHOP, JOB_BOW_MASTER, JOB_NIGHT_LORD}) {
    for (int level : {100, 140, 170, 200, 230, 260}) {
      const std::vector<std::string> keys = MaxOutfit(job, level);
      std::vector<std::string> sorted = keys;
      std::sort(sorted.begin(), sorted.end());
      EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end())
          << Job_Name(job) << " at " << level;
    }
  }
}

// Fights fall when the sweep first beat them, which for the hardest is long
// after they open; a fight it never named never falls.
TEST(MaxCharacterTest, ClearsComeWhenTheSweepsDid) {
  EXPECT_EQ(MaxClearLevel("zakum", "Normal"), 110);
  EXPECT_EQ(MaxClearLevel("damien", "Hard"), 255);
  EXPECT_EQ(MaxClearLevel("zakum", "Hard"), 0);
  EXPECT_EQ(MaxClearLevel("wall", ""), 0);
}

// Three alts at 70 from 230, five at 120 at 260, never on the main's line.
TEST(MaxCharacterTest, AltsComeLateAndSkipTheMainsLine) {
  EXPECT_TRUE(MaxAlts(JOB_FIGHTER, 229).empty());
  const std::vector<MaxAlt> at230 = MaxAlts(JOB_FIGHTER, 230);
  ASSERT_EQ(at230.size(), 3u);
  EXPECT_EQ(at230[0].line, JOB_CROSSBOWMAN);
  EXPECT_EQ(at230[0].level, 70);
  const std::vector<MaxAlt> marksman = MaxAlts(JOB_CROSSBOWMAN, 260);
  ASSERT_EQ(marksman.size(), 5u);
  for (const MaxAlt& alt : marksman) {
    EXPECT_NE(alt.line, JOB_CROSSBOWMAN);
    EXPECT_EQ(alt.level, 120);
  }
}

Skill Node(VNodeKind kind, int max_level) {
  Skill node;
  node.set_v_node(kind);
  node.set_max_level(max_level);
  return node;
}

// Nothing at 200, the sweep's share of each kind at 230, all of it at 260.
TEST(MaxCharacterTest, TheMatrixFillsByKind) {
  const Skill job = Node(V_NODE_KIND_JOB, 30);
  const Skill boost = Node(V_NODE_KIND_BOOST, 60);
  const Skill common = Node(V_NODE_KIND_COMMON, 30);
  EXPECT_EQ(MaxMatrixLevel(boost, 200), 0);
  EXPECT_EQ(MaxMatrixLevel(job, 230), 20);
  EXPECT_EQ(MaxMatrixLevel(boost, 230), 40);
  EXPECT_EQ(MaxMatrixLevel(Node(V_NODE_KIND_ARCHETYPE, 30), 230), 12);
  EXPECT_EQ(MaxMatrixLevel(common, 230), 6);
  EXPECT_EQ(MaxMatrixLevel(common, 260), 30);
}

// Farming stays on the Rare preset a character starts with; bossing reaches
// Legendary at 170.
TEST(MaxCharacterTest, OnlyBossingAbilityClimbs) {
  EXPECT_EQ(MaxAbilityPreset(Activity::kFarming, STAT_FIELD_STR, 260).rank(),
            ABILITY_RANK_RARE);
  EXPECT_EQ(MaxAbilityPreset(Activity::kBossing, STAT_FIELD_STR, 169).rank(),
            ABILITY_RANK_RARE);
  const AbilityPreset boss =
      MaxAbilityPreset(Activity::kBossing, STAT_FIELD_INT, 170);
  EXPECT_EQ(boss.rank(), ABILITY_RANK_LEGENDARY);
  ASSERT_EQ(boss.lines_size(), kAbilityLines);
  EXPECT_EQ(boss.lines(1).type(), ABILITY_LINE_TYPE_MAGIC_ATTACK);
}

// No symbol before the level-200 reward, none from Grandis, and no symbol ever
// loses a level or goes back in the bag.
TEST(MaxCharacterTest, SymbolsClimbWithTheLevel) {
  EXPECT_EQ(MaxSymbolLevel(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 199), 0);
  EXPECT_EQ(MaxSymbolLevel(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 200), 1);
  EXPECT_EQ(MaxSymbolLevel(EQUIP_SLOT_SYMBOL_MORASS, 230), 0);
  EXPECT_EQ(MaxSymbolLevel(EQUIP_SLOT_SYMBOL_ESFERA, 260), 13);
  EXPECT_EQ(MaxSymbolLevel(EQUIP_SLOT_SYMBOL_CERNIUM, 260), 0);
  EXPECT_EQ(MaxSymbolLevel(EQUIP_SLOT_HAT, 260), 0);
  for (int slot = EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY;
       slot <= EQUIP_SLOT_SYMBOL_ESFERA; ++slot) {
    int last = 0;
    for (int level = 1; level <= 260; ++level) {
      const int now = MaxSymbolLevel(static_cast<EquipSlot>(slot), level);
      EXPECT_GE(now, last) << slot << " at level " << level;
      last = now;
    }
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
  EXPECT_EQ(ring.rank(), POTENTIAL_RANK_RARE);
  EXPECT_EQ(LinesOf(ring, POTENTIAL_LINE_TYPE_BONUS_LUK_PCT), 2);
  EXPECT_EQ(ring.lines(2).rank(), POTENTIAL_RANK_RARE);
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

EquipPrototype Flammable(EquipSlot slot, int level) {
  EquipPrototype proto;
  proto.set_equip_slot(slot);
  proto.set_required_level(level);
  return proto;
}

// Four distinct lines the item's own pool holds, at tiers a flame can roll,
// with the job's own stat and attack; nothing before the band or on an item
// that takes no flame.
TEST(MaxCharacterTest, FlamesHoldDistinctLinesFromTheItemsPool) {
  const std::pair<StatField, StatField> kJobs[] = {
      {STAT_FIELD_STR, STAT_FIELD_DEX},
      {STAT_FIELD_DEX, STAT_FIELD_STR},
      {STAT_FIELD_INT, STAT_FIELD_LUK},
      {STAT_FIELD_LUK, STAT_FIELD_DEX}};
  for (EquipSlot slot :
       {EQUIP_SLOT_PRIMARY_WEAPON, EQUIP_SLOT_HAT, EQUIP_SLOT_PENDANT}) {
    const EquipPrototype proto = Flammable(slot, 200);
    const std::vector<FlameStat> pool = FlamePool(proto);
    for (const auto& [primary, secondary] : kJobs) {
      const FlameLines lines = MaxFlameFor(proto, 260, primary, secondary);
      ASSERT_EQ(lines.size(), kFlameLines) << EquipSlot_Name(slot);
      std::vector<FlameStat> seen;
      for (const FlameLine& line : lines) {
        EXPECT_NE(std::find(pool.begin(), pool.end(), line.stat()), pool.end())
            << FlameStat_Name(line.stat());
        EXPECT_EQ(std::count(seen.begin(), seen.end(), line.stat()), 0);
        seen.push_back(line.stat());
        EXPECT_GE(line.tier(), 3);
        EXPECT_LE(line.tier(), 7);
      }
      const FlameStat attack = primary == STAT_FIELD_INT
                                   ? FLAME_STAT_MAGIC_ATTACK
                                   : FLAME_STAT_ATTACK;
      if (slot != EQUIP_SLOT_PENDANT) {
        EXPECT_EQ(std::count(seen.begin(), seen.end(), attack), 1);
      }
    }
    EXPECT_TRUE(
        MaxFlameFor(proto, 250, STAT_FIELD_STR, STAT_FIELD_DEX).empty());
  }
  EXPECT_TRUE(MaxFlameFor(Flammable(EQUIP_SLOT_RING, 200), 260, STAT_FIELD_STR,
                          STAT_FIELD_DEX)
                  .empty());
}

}  // namespace
}  // namespace ms
