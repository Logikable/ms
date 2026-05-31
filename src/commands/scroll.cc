#include "src/commands/scroll.h"

#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "src/character.h"
#include "src/commands/util.h"
#include "src/equip.pb.h"
#include "src/equip_instance.h"
#include "src/frontend.h"
#include "src/scroll.pb.h"

namespace ms {

std::string ScrollCommand(CharacterInstance& character, const Scroll& scroll,
                          std::mt19937& rng) {
  if (!character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON)) {
    return "No weapon equipped.";
  }
  const EquipInstance& item =
      character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  if (item.proto().remaining_upgrade_slots() == 0) {
    return "No upgrade slots remaining on " + item.prototype().name() + ".";
  }
  bool success =
      character.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll, rng);
  std::ostringstream out;
  out << (success ? "Success! " : "Failed.  ");
  out << FormatEquip(character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
  return out.str();
}

void RegisterScrollCommand(Frontend& frontend, CharacterInstance& character,
                            const Scroll& scroll, std::mt19937& rng) {
  frontend.Register({
      "scroll",
      [&character, &scroll, &rng](std::vector<std::string>) -> std::string {
        return ScrollCommand(character, scroll, rng);
      },
  });
}

}  // namespace ms
