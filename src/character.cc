#include "src/character.h"

#include "absl/log/log.h"
#include "google/protobuf/map.h"

namespace ms {

namespace {

using google::protobuf::Map;

constexpr int kApPerLevel = 5;
constexpr int kApJobAdvancementBonus = 5;

}  // namespace

CharacterInstance::CharacterInstance(Character character)
    : character_(std::move(character)) {}

void CharacterInstance::LevelUp() {
  character_.set_level(character_.level() + 1);
  character_.set_ap(character_.ap() + kApPerLevel);
}

void CharacterInstance::AdvanceJob(Job next_job) {
  int stage = character_.job_stage() + 1;
  character_.set_job_stage(stage);
  character_.set_job(next_job);
  if (stage == 3 || stage == 4) {
    character_.set_ap(character_.ap() + kApJobAdvancementBonus);
  }
}

bool CharacterInstance::AllocateStat(StatField field, int amount) {
  if (field == STAT_FIELD_UNSPECIFIED) { return false; }
  if (amount > character_.ap()) { return false; }
  AllocatedStats* stats = character_.mutable_allocated_stats();
  switch (field) {
    case STAT_FIELD_STR: stats->set_str(stats->str() + amount); break;
    case STAT_FIELD_DEX: stats->set_dex(stats->dex() + amount); break;
    case STAT_FIELD_INT: stats->set_int_(stats->int_() + amount); break;
    case STAT_FIELD_LUK: stats->set_luk(stats->luk() + amount); break;
    case STAT_FIELD_HP:  stats->set_hp(stats->hp() + amount); break;
    case STAT_FIELD_MP:  stats->set_mp(stats->mp() + amount); break;
    default: return false;
  }
  character_.set_ap(character_.ap() - amount);
  return true;
}

bool CharacterInstance::Equip(EquipSlot slot, int inventory_index) {
  if (slot == EQUIP_SLOT_UNSPECIFIED) { return false; }
  Inventory* inv = character_.mutable_inventory();
  if (inventory_index < 0 || inventory_index >= inv->equip_tab_size()) {
    return false;
  }

  ms::Equip item = inv->equip_tab(inventory_index);
  inv->mutable_equip_tab()->DeleteSubrange(inventory_index, 1);

  Map<int32_t, ms::Equip>& slots = *character_.mutable_equipped();
  Map<int32_t, ms::Equip>::iterator it = slots.find(static_cast<int>(slot));
  if (it != slots.end()) {
    *inv->add_equip_tab() = it->second;
    slots.erase(it);
  }

  slots[static_cast<int>(slot)] = std::move(item);
  return true;
}

bool CharacterInstance::Unequip(EquipSlot slot) {
  if (slot == EQUIP_SLOT_UNSPECIFIED) { return false; }
  Map<int32_t, ms::Equip>& slots = *character_.mutable_equipped();
  Map<int32_t, ms::Equip>::iterator it = slots.find(static_cast<int>(slot));
  if (it == slots.end()) { return false; }
  *character_.mutable_inventory()->add_equip_tab() = it->second;
  slots.erase(it);
  return true;
}

void CharacterInstance::PickUp(const EquipPrototype& prototype) {
  ms::Equip* item = character_.mutable_inventory()->add_equip_tab();
  item->set_equip_name(prototype.name());
  item->set_remaining_upgrade_slots(prototype.upgrade_slots());
}

}  // namespace ms
