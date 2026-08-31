#include "src/item/equip_stats.h"

#include "absl/types/span.h"
#include "src/protos/equip.pb.h"

namespace ms {

EquipStats SumEquipStats(absl::Span<const EquipStats> sources) {
  EquipStats result;
  for (const EquipStats& s : sources) {
    result.set_str(result.str() + s.str());
    result.set_dex(result.dex() + s.dex());
    result.set_int_(result.int_() + s.int_());
    result.set_luk(result.luk() + s.luk());
    result.set_attack(result.attack() + s.attack());
    result.set_magic_attack(result.magic_attack() + s.magic_attack());
    result.set_max_hp(result.max_hp() + s.max_hp());
    result.set_max_mp(result.max_mp() + s.max_mp());
    result.set_def(result.def() + s.def());
    result.set_boss_damage(result.boss_damage() + s.boss_damage());
    result.set_ignore_enemy_defense(result.ignore_enemy_defense() +
                                    s.ignore_enemy_defense());
    result.set_item_drop_rate(result.item_drop_rate() + s.item_drop_rate());
    result.set_max_hp_pct(result.max_hp_pct() + s.max_hp_pct());
    result.set_max_mp_pct(result.max_mp_pct() + s.max_mp_pct());
  }
  return result;
}

}  // namespace ms
