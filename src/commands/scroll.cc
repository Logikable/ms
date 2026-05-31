#include "src/commands/scroll.h"

#include <map>
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

std::string ScrollCommand(CharacterInstance& character,
                          const std::map<std::string, Scroll>& scrolls,
                          const std::string& scroll_name, std::mt19937& rng) {
  std::map<std::string, Scroll>::const_iterator it = scrolls.find(scroll_name);
  if (it == scrolls.end()) {
    return "Unknown scroll '" + scroll_name + "'.";
  }
  if (!character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON)) {
    return "No weapon equipped.";
  }
  const EquipInstance& item =
      character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  if (item.proto().remaining_upgrade_slots() == 0) {
    return "No upgrade slots remaining on " + item.prototype().name() + ".";
  }
  bool success =
      character.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, it->second, rng);
  std::ostringstream out;
  out << (success ? "Success! " : "Failed.  ");
  out << FormatEquip(character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
  return out.str();
}

void RegisterScrollCommand(Frontend& frontend, CharacterInstance& character,
                            const std::map<std::string, Scroll>& scrolls,
                            std::mt19937& rng) {
  frontend.Register({
      "scroll",
      "Apply a scroll to the equipped primary weapon.",
      [&character, &scrolls, &rng](std::vector<std::string> tokens) -> std::string {
        if (tokens.size() < 2) {
          return "Usage: /scroll <scroll_name>";
        }
        return ScrollCommand(character, scrolls, tokens[1], rng);
      },
  });
}

}  // namespace ms
