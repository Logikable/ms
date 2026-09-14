#include "src/character/equip_presets.h"

#include <gtest/gtest.h>

#include <string>

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

Equip Hat(const std::string& name) {
  Equip equip;
  equip.set_equip_name(name);
  return equip;
}

TEST(EquipPresetsTest, FoldsALegacyWornMapIntoTheFirstPreset) {
  Character character;
  (*character.mutable_legacy_equipped())[EQUIP_SLOT_HAT] = Hat("Zakum Helmet");

  MigrateEquipPresets(character);

  EXPECT_TRUE(character.legacy_equipped().empty());
  ASSERT_EQ(character.equip_presets().presets_size(), kNumStatPresets);
  const EquipPreset& first =
      PresetOf(character.equip_presets(), StatPreset::kFirst);
  EXPECT_EQ(first.equipped().at(EQUIP_SLOT_HAT).equip_name(), "Zakum Helmet");
  EXPECT_TRUE(PresetOf(character.equip_presets(), StatPreset::kThird)
                  .equipped()
                  .empty());
}

TEST(EquipPresetsTest, LeavesPresetsAloneAndIsIdempotent) {
  Character character;
  (*character.mutable_equip_presets()
        ->add_presets()
        ->mutable_equipped())[EQUIP_SLOT_HAT] = Hat("Zakum Helmet");
  (*character.mutable_equip_presets()
        ->add_presets()
        ->mutable_equipped())[EQUIP_SLOT_HAT] = Hat("Royal Warrior Helm");

  MigrateEquipPresets(character);
  MigrateEquipPresets(character);

  ASSERT_EQ(character.equip_presets().presets_size(), kNumStatPresets);
  EXPECT_EQ(PresetOf(character.equip_presets(), StatPreset::kSecond)
                .equipped()
                .at(EQUIP_SLOT_HAT)
                .equip_name(),
            "Royal Warrior Helm");
}

TEST(EquipPresetsTest, AnUnmigratedProtoAnswersWithAnEmptyPreset) {
  EquipPresets presets;
  EXPECT_TRUE(PresetOf(presets, StatPreset::kFirst).equipped().empty());
}

}  // namespace
}  // namespace ms
