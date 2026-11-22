// --mode=bis against the shipped catalog: every maximum reached, every rolled
// line one the game's rolls could give, and a character no weaker than max
// mode's.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/character/account/familiar.h"
#include "src/character/account/legion.h"
#include "src/character/character.h"
#include "src/character/force/symbol.h"
#include "src/character/inner_ability.h"
#include "src/character/matrix/hexa_matrix.h"
#include "src/character/matrix/hexa_stat.h"
#include "src/character/stat_preset.h"
#include "src/combat/bis_rate.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/upgrade/flame.h"
#include "src/item/upgrade/potential.h"
#include "src/proto_loader.h"

namespace ms {
namespace {

constexpr int kLevel = 260;

std::unique_ptr<GameState> NewState(GameMode mode, int level) {
  TestOptions options;
  options.job = JOB_ADVANCEMENT_HERO;
  options.level = level;
  options.autoswap_presets = true;
  options.bis_rate = BisFightRate;
  return std::make_unique<GameState>(
      LoadTextProtoMap<EquipPrototype>(EmbeddedEquips()),
      LoadTextProtoMap<Scroll>(EmbeddedScrolls()),
      LoadTextProtoMap<ItemPrototype>(EmbeddedItems()),
      LoadTextProtoMap<Mob>(EmbeddedMobs()),
      LoadTextProtoMap<MapData>(EmbeddedMaps()),
      LoadTextProtoMap<Skill>(EmbeddedSkills()), mode, options, 1,
      LoadTextProtoMap<EquipSet>(EmbeddedSets()),
      LoadTextProtoMap<Boss>(EmbeddedBosses()));
}

// One search for the whole file: it takes seconds.
class BisTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    bis_ = NewState(GameMode::kBis, kLevel).release();
  }
  static void TearDownTestSuite() {
    delete bis_;
    bis_ = nullptr;
  }
  static const CharacterInstance& c() {
    return bis_->character;
  }
  static GameState* bis_;
};

GameState* BisTest::bis_ = nullptr;

TEST_F(BisTest, OutdamagesMaxMode) {
  std::unique_ptr<GameState> max = NewState(GameMode::kMax, kLevel);
  EXPECT_GT(BisFightRate(*bis_), BisFightRate(*max));
}

TEST_F(BisTest, FillsEverySlotMaxModeFills) {
  std::unique_ptr<GameState> max = NewState(GameMode::kMax, kLevel);
  for (const auto& [slot, worn] : max->character.equipped()) {
    EXPECT_NE(c().WornAt(StatPreset::kFirst, slot), nullptr)
        << EquipSlot_Name(slot);
  }
}

TEST_F(BisTest, ScrollsEverySlotAndStarsToTheCap) {
  for (const auto& [slot, worn] : c().equipped()) {
    const EquipPrototype& proto = worn->prototype();
    EXPECT_EQ(worn->equip_state().remaining_upgrade_slots(), 0) << proto.name();
    if (Supports(proto, UPGRADE_STAR_FORCE) && !IsSymbol(proto)) {
      EXPECT_EQ(worn->stars(), worn->max_stars()) << proto.name();
    }
  }
}

TEST_F(BisTest, PotentialsAreLegendaryAndPrime) {
  for (const auto& [slot, worn] : c().equipped()) {
    if (!worn->CanCube()) {
      continue;
    }
    for (PotentialTrack track :
         {PotentialTrack::kMain, PotentialTrack::kBonus}) {
      const Potential& potential = PotentialOf(worn->equip_state(), track);
      const std::vector<PotentialLineType> pool = PotentialPool(
          track, PotentialGroupOf(slot), POTENTIAL_RANK_LEGENDARY);
      EXPECT_EQ(potential.rank(), POTENTIAL_RANK_LEGENDARY);
      ASSERT_EQ(potential.lines_size(), kPotentialLines)
          << worn->prototype().name();
      for (const PotentialLine& line : potential.lines()) {
        EXPECT_EQ(line.rank(), POTENTIAL_RANK_LEGENDARY);
        EXPECT_NE(std::find(pool.begin(), pool.end(), line.type()), pool.end())
            << worn->prototype().name() << " "
            << PotentialLineType_Name(line.type());
        EXPECT_NE(line.type(), POTENTIAL_LINE_TYPE_MESO_RATE);
        EXPECT_NE(line.type(), POTENTIAL_LINE_TYPE_ITEM_DROP_RATE);
      }
    }
  }
}

TEST_F(BisTest, FlamesAreFourDistinctTopTierLines) {
  int flamed = 0;
  for (const auto& [slot, worn] : c().equipped()) {
    if (!worn->CanFlame()) {
      continue;
    }
    ++flamed;
    const FlameLines& lines = worn->equip_state().flame();
    const std::vector<FlameStat> pool = FlamePool(worn->prototype());
    std::set<FlameStat> seen;
    EXPECT_EQ(lines.size(), kFlameLines) << worn->prototype().name();
    for (const FlameLine& line : lines) {
      EXPECT_EQ(line.tier(), 7);
      EXPECT_TRUE(seen.insert(line.stat()).second) << worn->prototype().name();
      EXPECT_NE(std::find(pool.begin(), pool.end(), line.stat()), pool.end());
    }
  }
  EXPECT_GT(flamed, 0);
}

TEST_F(BisTest, InnerAbilityIsWhatARollCanGive) {
  const AbilityPreset& preset =
      c().ability(AutoswapSlotFor(Activity::kBossing));
  ASSERT_EQ(preset.lines_size(), kAbilityLines);
  EXPECT_EQ(preset.rank(), ABILITY_RANK_LEGENDARY);
  std::set<AbilityLineType> seen;
  for (int i = 0; i < kAbilityLines; ++i) {
    const AbilityLine& line = preset.lines(i);
    EXPECT_EQ(line.rank(),
              i == 0 ? ABILITY_RANK_LEGENDARY : ABILITY_RANK_UNIQUE);
    EXPECT_GT(AbilityTypeWeight(line.type(), line.rank()), 0);
    EXPECT_TRUE(seen.insert(line.type()).second);
    EXPECT_NE(line.type(), ABILITY_LINE_TYPE_MESO);
    EXPECT_NE(line.type(), ABILITY_LINE_TYPE_ITEM_DROP);
  }
}

TEST_F(BisTest, EveryFamiliarIsLegendaryWithTwoPrimeLines) {
  const FamiliarBook& book = c().familiars();
  EXPECT_EQ(book.familiars_size(), static_cast<int>(FamiliarRoster().size()));
  for (const Familiar& familiar : book.familiars()) {
    EXPECT_EQ(familiar.level(), kFamiliarMaxLevel);
    ASSERT_EQ(familiar.lines_size(), kFamiliarLines);
    for (const FamiliarLine& line : familiar.lines()) {
      EXPECT_EQ(line.rank(), POTENTIAL_RANK_LEGENDARY);
      EXPECT_NE(line.type(), FAMILIAR_LINE_TYPE_BOSS_DROP_RATE);
    }
  }
}

TEST_F(BisTest, HoldsAFullLegion) {
  EXPECT_EQ(c().legion_summary().members, LegionMemberSlots(kLegionRanks));
  EXPECT_EQ(static_cast<int>(bis_->inactive_characters.size()) + 1,
            LegionMemberSlots(kLegionRanks));
}

TEST_F(BisTest, WearsEverySymbolItsLevelCanAtItsTop) {
  std::set<std::string> worn;
  for (const auto& [slot, piece] : c().equipped()) {
    if (IsSymbol(piece->prototype())) {
      worn.insert(piece->prototype().name());
      EXPECT_EQ(piece->equip_state().symbol_level(),
                SymbolMaxLevel(piece->prototype()));
    }
  }
  for (const auto& [key, proto] : bis_->equips) {
    if (IsSymbol(proto) && c().MeetsLevel(proto)) {
      EXPECT_EQ(worn.count(proto.name()), 1u) << proto.name();
    }
  }
}

TEST_F(BisTest, BuysTheWholeVMatrix) {
  for (const auto& [name, skill] : bis_->skills) {
    if (skill.v_node() != V_NODE_KIND_UNSPECIFIED && c().ReachesVNode(skill)) {
      EXPECT_EQ(c().skill_level(skill), SkillMaxLevel(skill)) << name;
    }
  }
}

TEST(BisSixthJobTest, BuysTheHexaMatrixAndOpensEveryStatNode) {
  if (!kSixthJobEnabled) {
    GTEST_SKIP() << "the 6th job is behind --config=sixth";
  }
  constexpr int kHexaLevel = 280;
  std::unique_ptr<GameState> state = NewState(GameMode::kBis, kHexaLevel);
  const CharacterInstance& character = state->character;
  EXPECT_EQ(character.hexa_stats().nodes_size(), HexaStatNodesOpen(kHexaLevel));
  EXPECT_TRUE(HexaStatsAllowed(character.hexa_stats()));
  for (const auto& [name, skill] : state->skills) {
    if (skill.hexa_node() != HEXA_NODE_KIND_UNSPECIFIED &&
        character.ReachesHexaNode(skill)) {
      EXPECT_EQ(
          character.skill_level(skill),
          std::min(SkillMaxLevel(skill), MaxHexaNodeLevel(skill.hexa_node())))
          << name;
    }
  }
}

}  // namespace
}  // namespace ms
