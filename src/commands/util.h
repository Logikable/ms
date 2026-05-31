/* Display and parsing utilities shared across equipment commands. Provides
 * FormatEquip for rendering EquipInstance stats, and SlotFromName/SlotToName
 * for converting between EquipSlot enum values and user-facing names.
 * util.cc implements all functions.
 */
#ifndef MS_COMMANDS_UTIL_H_
#define MS_COMMANDS_UTIL_H_

#include <string>

#include "src/equip.pb.h"
#include "src/equip_instance.h"

namespace ms {

// indent is prepended to each stat line; defaults to two spaces.
std::string FormatEquip(const EquipInstance& item,
                        const std::string& indent = "  ");

// Returns EQUIP_SLOT_UNSPECIFIED for unrecognised names.
EquipSlot SlotFromName(const std::string& name);
// Returns a display label for a slot, or "Unknown" for unrecognised values.
std::string SlotToName(EquipSlot slot);

}  // namespace ms

#endif  // MS_COMMANDS_UTIL_H_
