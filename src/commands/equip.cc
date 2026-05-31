#include "src/commands/equip.h"

#include <string>
#include <vector>

#include "src/character.h"
#include "src/frontend.h"

namespace ms {

std::string EquipCommand(CharacterInstance& character, int inventory_index) {
  if (!character.Equip(inventory_index)) {
    return "Could not equip item at index " +
           std::to_string(inventory_index) + ".";
  }
  return "Equipped.";
}

void RegisterEquipCommand(Frontend& frontend, CharacterInstance& character) {
  frontend.Register({
      "equip",
      "Equip the bag item at <index>. Optional [slot] for rings/pendants.",
      [&character](std::vector<std::string> args) -> std::string {
        if (args.size() < 2) {
          return "Usage: /equip <index> [slot]";
        }
        int index = 0;
        try {
          index = std::stoi(args[1]);
        } catch (...) {
          return "Invalid index '" + args[1] + "'.";
        }
        // Optional slot arg reserved for multi-slot items (rings, pendants).
        return EquipCommand(character, index);
      },
  });
}

}  // namespace ms
