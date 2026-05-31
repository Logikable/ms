#include "src/commands/util.h"

#include <sstream>
#include <string>

#include "src/equip.pb.h"
#include "src/equip_instance.h"

namespace ms {

std::string FormatEquip(const EquipInstance& item) {
  std::ostringstream out;
  const EquipStats& s = item.stats();
  out << item.prototype().name()
      << "  [" << item.proto().remaining_upgrade_slots() << " upgrade slots]\n";
  if (s.attack())       { out << "  ATT:  " << s.attack()       << "\n"; }
  if (s.magic_attack()) { out << "  MATT: " << s.magic_attack() << "\n"; }
  if (s.str())          { out << "  STR:  " << s.str()          << "\n"; }
  if (s.dex())          { out << "  DEX:  " << s.dex()          << "\n"; }
  if (s.int_())         { out << "  INT:  " << s.int_()         << "\n"; }
  if (s.luk())          { out << "  LUK:  " << s.luk()          << "\n"; }
  if (s.max_hp())       { out << "  HP:   " << s.max_hp()       << "\n"; }
  if (s.def())          { out << "  DEF:  " << s.def()          << "\n"; }
  return out.str();
}

EquipSlot SlotFromName(const std::string& name) {
  if (name == "primary_weapon") {
    return EQUIP_SLOT_PRIMARY_WEAPON;
  }
  return EQUIP_SLOT_UNSPECIFIED;
}

std::string SlotToName(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON: return "Primary Weapon";
    default: return "Unknown";
  }
}

}  // namespace ms
