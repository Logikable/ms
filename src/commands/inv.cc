#include "src/commands/inv.h"

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "src/character.h"
#include "src/commands/util.h"
#include "src/equip.pb.h"
#include "src/equip_instance.h"
#include "src/frontend.h"

namespace ms {

std::string InvCommand(const CharacterInstance& character) {
  const std::map<EquipSlot, EquipInstance>& eq = character.equipped();
  if (eq.empty()) {
    return "Nothing equipped.";
  }
  std::ostringstream out;
  for (const std::pair<const EquipSlot, EquipInstance>& entry : eq) {
    out << SlotToName(entry.first) << ":\n";
    out << FormatEquip(entry.second);
  }
  return out.str();
}

void RegisterInvCommand(Frontend& frontend, CharacterInstance& character) {
  frontend.Register({
      "inv",
      [&character](std::vector<std::string>) -> std::string {
        return InvCommand(character);
      },
  });
}

}  // namespace ms
