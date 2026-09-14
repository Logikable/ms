#include "src/character/equip_presets.h"

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

void MigrateEquipPresets(Character& character) {
  EquipPresets& presets = *character.mutable_equip_presets();
  if (!character.legacy_equipped().empty()) {
    presets.clear_presets();
    *presets.add_presets()->mutable_equipped() = character.legacy_equipped();
    character.clear_legacy_equipped();
  }
  while (presets.presets_size() < kNumStatPresets) {
    presets.add_presets();
  }
}

const EquipPreset& PresetOf(const EquipPresets& presets, StatPreset slot) {
  if (IndexOf(slot) >= presets.presets_size()) {
    return EquipPreset::default_instance();
  }
  return presets.presets(IndexOf(slot));
}

}  // namespace ms
