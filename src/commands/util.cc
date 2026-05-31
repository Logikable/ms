#include "src/commands/util.h"

#include <sstream>
#include <string>

#include "src/protos/equip.pb.h"
#include "src/equip_instance.h"

namespace ms {

std::string FormatEquip(const EquipInstance& item, const std::string& indent) {
  std::ostringstream out;
  const EquipStats& s = item.stats();
  out << item.prototype().name()
      << "  [" << item.proto().remaining_upgrade_slots() << " upgrade slots]\n";
  if (s.attack())       { out << indent << "ATT:  " << s.attack()       << "\n"; }
  if (s.magic_attack()) { out << indent << "MATT: " << s.magic_attack() << "\n"; }
  if (s.str())          { out << indent << "STR:  " << s.str()          << "\n"; }
  if (s.dex())          { out << indent << "DEX:  " << s.dex()          << "\n"; }
  if (s.int_())         { out << indent << "INT:  " << s.int_()         << "\n"; }
  if (s.luk())          { out << indent << "LUK:  " << s.luk()          << "\n"; }
  if (s.max_hp())       { out << indent << "HP:   " << s.max_hp()       << "\n"; }
  if (s.def())          { out << indent << "DEF:  " << s.def()          << "\n"; }
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
