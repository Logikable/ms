/* Unequip command: /unequip <slot>
 * Moves the item in slot back to inventory. unequip.cc implements
 * UnequipCommand and RegisterUnequipCommand.
 */
#ifndef MS_COMMANDS_UNEQUIP_H_
#define MS_COMMANDS_UNEQUIP_H_

#include <string>
#include <vector>

#include "src/character.h"
#include "src/protos/equip.pb.h"
#include "src/frontend.h"

namespace ms {

// Unequips the item in slot. Returns an error string on failure.
std::string UnequipCommand(CharacterInstance& character, EquipSlot slot);
void RegisterUnequipCommand(Frontend& frontend, CharacterInstance& character);

}  // namespace ms

#endif  // MS_COMMANDS_UNEQUIP_H_
