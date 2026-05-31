#include "src/equip_stats.h"

namespace ms {

EquipStats SumEquipStats(std::initializer_list<EquipStats> sources) {
  EquipStats result;
  for (const EquipStats& s : sources) {
    result.set_str(result.str() + s.str());
    result.set_dex(result.dex() + s.dex());
    result.set_int_(result.int_() + s.int_());
    result.set_luk(result.luk() + s.luk());
    result.set_attack(result.attack() + s.attack());
    result.set_magic_attack(result.magic_attack() + s.magic_attack());
    result.set_max_hp(result.max_hp() + s.max_hp());
    result.set_def(result.def() + s.def());
  }
  return result;
}

}  // namespace ms
