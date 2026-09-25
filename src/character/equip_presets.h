/* The three gear setups a character keeps, as stored in the proto.
 *
 * Only save and load use these; everywhere else the worn items are live
 * EquipInstance objects held by CharacterInstance. This file handles reading a
 * proto written before presets existed, the same job MigrateHyperStats does for
 * Hyper Stat allocations.
 */
#ifndef MS_SRC_CHARACTER_EQUIP_PRESETS_H_
#define MS_SRC_CHARACTER_EQUIP_PRESETS_H_

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// Moves a save's single worn map into the first preset and pads the list to
// kNumStatPresets. Safe to call more than once.
void MigrateEquipPresets(Character& character);

// The gear in `slot`. Returns an empty preset if an unmigrated proto doesn't
// have that slot yet.
const EquipPreset& PresetOf(const EquipPresets& presets, StatPreset slot);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EQUIP_PRESETS_H_
