/* The three gear setups a character keeps, in the proto.
 *
 * Only the save and the load touch these: everywhere else the worn items are
 * live EquipInstance objects that CharacterInstance holds. What lives here is
 * reading a proto written before presets existed, which is the same job
 * MigrateHyperStats does for allocations.
 */
#ifndef MS_SRC_CHARACTER_EQUIP_PRESETS_H_
#define MS_SRC_CHARACTER_EQUIP_PRESETS_H_

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// Folds a save's one worn map into the first preset and fills the list out to
// kNumStatPresets. Idempotent.
void MigrateEquipPresets(Character& character);

// The gear held in `slot`. An empty preset for a slot a proto that has not
// been migrated does not hold yet.
const EquipPreset& PresetOf(const EquipPresets& presets, StatPreset slot);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EQUIP_PRESETS_H_
