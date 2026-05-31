/* Equip command: /equip <index> [slot]
 * Moves the item at inventory_index into its equip slot. equip.cc implements
 * EquipCommand and RegisterEquipCommand.
 */
#ifndef MS_COMMANDS_EQUIP_H_
#define MS_COMMANDS_EQUIP_H_

#include <string>
#include <vector>

#include "src/character.h"
#include "src/frontend.h"

namespace ms {

// Equips the inventory item at index. Returns an error string on failure.
std::string EquipCommand(CharacterInstance& character, int inventory_index);
void RegisterEquipCommand(Frontend& frontend, CharacterInstance& character);

}  // namespace ms

#endif  // MS_COMMANDS_EQUIP_H_
