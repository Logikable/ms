#include "src/combat/boss/liberation.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/character/character.h"
#include "src/character/force/symbol.h"
#include "src/character/stat_preset.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {

MissionRules LiberationRules(int index) {
  MissionRules rules;
  if (index < 0 || index >= kLiberationMissionCount) {
    return rules;
  }
  const LiberationMission& mission = kLiberationMissions[index];
  rules.damage_factor = mission.damage_factor;
  if (index >= kTanadianRuinFromMission) {
    rules.damage_factor *= kTanadianRuinFactor;
  }
  rules.hp_factor = mission.hp_factor;
  rules.sealed = mission.sealed;
  return rules;
}

bool Liberated(const CharacterInstance& character) {
  return character.HasDefeated(kLiberationKey);
}

const EquipPrototype* GenesisWeaponFor(const GameState& state) {
  const EquipPrototype* worn = EquippedWeapon(state, Activity::kBossing);
  if (worn == nullptr) {
    return nullptr;
  }
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state.equips) {
    const EquipPrototype& proto = entry.second;
    if (IsGenesisWeapon(proto) && proto.equip_type() == worn->equip_type()) {
      return &proto;
    }
  }
  return nullptr;
}

std::map<EquipSlot, EquipInstance> SealedGear(const GameState& state,
                                              const EquipPrototype& genesis) {
  // Named, so the instance isn't taken for a fresh drop and given the
  // finished weapon's stars and scrolls.
  auto bare = [](const EquipPrototype& proto) {
    Equip state;
    state.set_equip_name(proto.name());
    return EquipInstance(proto, state);
  };
  // GMS's sealed weapon has neither skill nor the Lucky Item effect; the
  // awakenings bring them.
  EquipPrototype sealed = genesis;
  sealed.clear_weapon_skills();
  sealed.set_lucky_item(false);
  std::map<EquipSlot, EquipInstance> gear;
  gear.emplace(EQUIP_SLOT_PRIMARY_WEAPON, bare(sealed));
  const CharacterInstance& character = state.character;
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       character.equipped(
           character.SlotFor(PresetKind::kEquip, Activity::kBossing))) {
    const EquipPrototype& proto = kv.second->prototype();
    if (kv.first == EQUIP_SLOT_SECONDARY) {
      gear.emplace(kv.first, bare(proto));
    } else if (IsSymbol(proto) || kv.first == EQUIP_SLOT_PROJECTILE) {
      gear.emplace(kv.first, EquipInstance(proto, kv.second->equip_state()));
    }
  }
  return gear;
}

bool GrantLiberation(GameState& state, const EquipPrototype& genesis) {
  if (!state.character.PickUp(std::make_unique<EquipInstance>(genesis))) {
    return false;
  }
  state.character.RecordDefeat(kLiberationKey);
  return true;
}

}  // namespace ms
