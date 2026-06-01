#include "src/frontend/util.h"

#include <string>

namespace ms {

std::string PadRight(const std::string& s, int width) {
  if ((int)s.size() >= width) { return s.substr(0, width); }
  return s + std::string(width - (int)s.size(), ' ');
}

std::string PadLeft(const std::string& s, int width) {
  if ((int)s.size() >= width) { return s.substr(0, width); }
  return std::string(width - (int)s.size(), ' ') + s;
}

void AppendStat(std::string& out, int val, const std::string& name) {
  if (val <= 0) return;
  if (!out.empty()) out += "  ";
  out += "+" + std::to_string(val) + " " + name;
}

std::string FormatJobCategories(const EquipPrototype& proto) {
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (static_cast<EquipJobCategory>(proto.equip_job_categories(i)) ==
        EQUIP_JOB_CATEGORY_UNIVERSAL) {
      return "All";
    }
  }
  std::string result;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (!result.empty()) { result += "/"; }
    switch (static_cast<EquipJobCategory>(proto.equip_job_categories(i))) {
      case EQUIP_JOB_CATEGORY_WARRIOR:  result += "Warrior";  break;
      case EQUIP_JOB_CATEGORY_BOWMAN:   result += "Bowman";   break;
      case EQUIP_JOB_CATEGORY_MAGICIAN: result += "Magician"; break;
      case EQUIP_JOB_CATEGORY_THIEF:    result += "Thief";    break;
      case EQUIP_JOB_CATEGORY_PIRATE:   result += "Pirate";   break;
      default: break;
    }
  }
  return result.empty() ? "All" : result;
}

}  // namespace ms
