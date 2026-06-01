#include "src/commands/inv.h"

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "src/character.h"
#include "src/commands/util.h"
#include "src/equip_instance.h"
#include "src/frontend.h"
#include "src/protos/equip.pb.h"

namespace ms {

std::string InvCommand(const CharacterInstance& character) {
  const std::map<EquipSlot, EquipInstance>& eq = character.equipped();
  const std::vector<EquipInstance>& bag = character.inventory();
  if (eq.empty() && bag.empty()) {
    return "Nothing to show.";
  }
  std::ostringstream out;
  if (!eq.empty()) {
    out << "Equipped:\n";
    for (const std::pair<const EquipSlot, EquipInstance>& entry : eq) {
      std::string prefix = SlotToName(entry.first) + ":  ";
      out << prefix
          << FormatEquip(entry.second, std::string(prefix.size(), ' '));
    }
  }
  if (!bag.empty()) {
    if (!eq.empty()) {
      out << "\n";
    }
    out << "Bag:\n";
    for (int i = 0; i < static_cast<int>(bag.size()); ++i) {
      std::string prefix = "[" + std::to_string(i) + "] ";
      out << prefix << FormatEquip(bag[i], std::string(prefix.size(), ' '));
    }
  }
  return out.str();
}

void RegisterInvCommand(Frontend& frontend, CharacterInstance& character) {
  frontend.Register({
      "inv",
      "Show equipped items and bag contents.",
      [&character](std::vector<std::string>) -> std::string {
        return InvCommand(character);
      },
  });
}

}  // namespace ms
