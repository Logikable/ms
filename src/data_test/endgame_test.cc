// The --mode=endgame character on the game's own data: everything GMS's
// ceiling holds is worn, levelled and allocated.
#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/hexa_stat.h"
#include "src/character/legion.h"
#include "src/character/legion_artifact.h"
#include "src/character/max_character.h"
#include "src/character/stat_preset.h"
#include "src/character/symbol.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/legion.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

class EndgameTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    TestOptions options;
    options.job = JOB_ADVANCEMENT_BOW_MASTER;
    options.autoswap_presets = true;
    options.playable_roster = false;
    shared_ = new GameState(
        LoadTestData<EquipPrototype>("equip"), LoadTestData<Scroll>("scrolls"),
        LoadTestData<ItemPrototype>("items"), LoadTestData<Mob>("mobs"),
        LoadTestData<MapData>("maps"), LoadTestData<Skill>("skills"),
        GameMode::kEndgame, options, /*seed=*/1, LoadTestData<EquipSet>("sets"),
        LoadTestData<Boss>("bosses"));
  }
  static void TearDownTestSuite() {
    delete shared_;
    shared_ = nullptr;
  }
  static const CharacterInstance& character() {
    return shared_->character;
  }
  static const EquipInstance* Worn(EquipSlot slot) {
    return character().WornAt(AutoswapSlotFor(Activity::kBossing), slot);
  }
  static GameState* shared_;
};

GameState* EndgameTest::shared_ = nullptr;

TEST_F(EndgameTest, IsAFifthJobAtThreeHundred) {
  EXPECT_EQ(character().proto().level(), kEndgameLevel);
  EXPECT_EQ(character().proto().job(), JOB_BOW_MASTER);
}

// The Destiny Weapon keeps its fixed 22 stars; the rest is at 25.
TEST_F(EndgameTest, WearsTheEndgameSetsAtTheirTop) {
  const EquipInstance* weapon = Worn(EQUIP_SLOT_PRIMARY_WEAPON);
  ASSERT_NE(weapon, nullptr);
  EXPECT_EQ(weapon->prototype().name(), "Destiny Bow");
  EXPECT_EQ(weapon->stars(), 22);
  const EquipInstance* hat = Worn(EQUIP_SLOT_HAT);
  ASSERT_NE(hat, nullptr);
  EXPECT_EQ(hat->prototype().name(), "Eternal Archer Hat");
  EXPECT_EQ(hat->stars(), 25);
  EXPECT_EQ(hat->potential().rank(), POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(hat->bonus_potential().rank(), POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(hat->equip_state().flame_size(), 4);
  const EquipInstance* face = Worn(EQUIP_SLOT_FACE_ACCESSORY);
  ASSERT_NE(face, nullptr);
  EXPECT_EQ(face->prototype().name(), "Original Sin of Pride");
  EXPECT_EQ(face->equip_state().exceptional_enhancements(), 3);
  const StatPreset boss = AutoswapSlotFor(Activity::kBossing);
  for (const auto& [name, pieces] :
       std::map<EquipSetName, int>{{EQUIP_SET_NAME_ETERNAL_BOWMAN, 8},
                                   {EQUIP_SET_NAME_BRILLIANT_BOSS, 5},
                                   {EQUIP_SET_NAME_PITCHED_BOSS, 9}}) {
    bool found = false;
    for (const auto& [key, set] : character().equip_sets()) {
      if (set.name() == name) {
        found = true;
        EXPECT_EQ(character().PiecesWornOf(set, boss), pieces)
            << EquipSetName_Name(name);
      }
    }
    EXPECT_TRUE(found) << EquipSetName_Name(name);
  }
}

TEST_F(EndgameTest, EverySymbolIsMaxed) {
  int symbols = 0;
  for (const auto& [slot, item] : character().equipped()) {
    if (IsSymbol(item->prototype())) {
      ++symbols;
      EXPECT_TRUE(SymbolMaxed(item->prototype(), item->equip_state()))
          << item->prototype().name();
    }
  }
  EXPECT_EQ(symbols, 14);
}

TEST_F(EndgameTest, TheAccountSystemsAreFull) {
  const HexaStats& hexa = character().proto().hexa_stats();
  EXPECT_EQ(hexa.nodes_size(), kHexaStatNodes);
  EXPECT_TRUE(HexaStatsAllowed(hexa));
  const Legion& legion = shared_->account.legion();
  EXPECT_EQ(legion.artifact().crystals_size(), kArtifactCrystals);
  EXPECT_TRUE(ArtifactAllowed(legion.artifact()));
  ASSERT_EQ(legion.champions_size(), kChampionSlots);
  for (const LegionChampion& champion : legion.champions()) {
    EXPECT_EQ(champion.rank(), CHAMPION_RANK_SSS);
  }
  EXPECT_EQ(character().legion_summary().legion_level, 10500);
}

}  // namespace
}  // namespace ms
