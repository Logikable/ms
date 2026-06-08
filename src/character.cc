#include "src/character.h"

#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

namespace {

constexpr int kApPerLevel = 5;
constexpr int kApJobAdvancementBonus = 5;

EquipJobCategory JobToCategory(Job job) {
  switch (job) {
    case JOB_BEGINNER:
      return EQUIP_JOB_CATEGORY_BEGINNER;
    case JOB_WARRIOR:
      return EQUIP_JOB_CATEGORY_WARRIOR;
    default:
      return EQUIP_JOB_CATEGORY_UNSPECIFIED;
  }
}

}  // namespace

CharacterInstance::CharacterInstance(std::mt19937& rng, Character character)
    : rng_(rng), character_(std::move(character)) {
}

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
  if (field == STAT_FIELD_UNSPECIFIED) {
    return false;
  }
  if (amount > character_.ap()) {
    return false;
  }
  AllocatedStats* stats = character_.mutable_allocated_stats();
  switch (field) {
    case STAT_FIELD_STR:
      stats->set_str(stats->str() + amount);
      break;
    case STAT_FIELD_DEX:
      stats->set_dex(stats->dex() + amount);
      break;
    case STAT_FIELD_INT:
      stats->set_int_(stats->int_() + amount);
      break;
    case STAT_FIELD_LUK:
      stats->set_luk(stats->luk() + amount);
      break;
    case STAT_FIELD_HP:
      // Demon Avenger gains 15 HP per AP instead of 1.
      stats->set_hp(stats->hp() + amount);
      break;
    case STAT_FIELD_MP:
      stats->set_mp(stats->mp() + amount);
      break;
    default:
      return false;
  }
  character_.set_ap(character_.ap() - amount);
  return true;
}

bool CharacterInstance::AllocateAllStat(StatField field) {
  if (field == STAT_FIELD_UNSPECIFIED) {
    return false;
  }
  int ap = character_.ap();
  if (ap == 0) {
    return false;
  }
  // TODO: clamp `ap` to the per-stat cap for classes like Xenon, where each
  // job advancement defines a maximum allocation per stat and the surplus AP
  // must be assigned to a different stat.
  return AllocateStat(field, ap);
}

void CharacterInstance::RecomputeEquipStats() {
  std::vector<EquipStats> list;
  for (const std::pair<const EquipSlot, EquipInstance>& kv : equipped_) {
    list.push_back(kv.second.stats());
  }
  equip_stats_ = SumEquipStats(absl::MakeSpan(list));
}

void CharacterInstance::PickUp(const EquipPrototype& prototype) {
  inventory_.emplace_back(prototype);
}

bool CharacterInstance::Equip(int inventory_index) {
  if (inventory_index < 0 ||
      inventory_index >= static_cast<int>(inventory_.size())) {
    return false;
  }
  EquipSlot slot = inventory_[inventory_index].prototype().equip_slot();
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  EquipInstance item = std::move(inventory_[inventory_index]);
  inventory_.erase(inventory_.begin() + inventory_index);
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it != equipped_.end()) {
    inventory_.push_back(std::move(it->second));
    equipped_.erase(it);
  }
  equipped_.emplace(slot, std::move(item));
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::Unequip(EquipSlot slot) {
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return false;
  }
  inventory_.push_back(std::move(it->second));
  equipped_.erase(it);
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::ScrollEquipped(EquipSlot slot, const Scroll& scroll) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return false;
  }
  bool success = it->second.Scroll(scroll, rng_);
  if (success) {
    RecomputeEquipStats();
  }
  return success;
}

bool CharacterInstance::ScrollInventory(int index, const Scroll& scroll) {
  if (index < 0 || index >= static_cast<int>(inventory_.size())) {
    return false;
  }
  return inventory_[index].Scroll(scroll, rng_);
}

StarForceOutcome CharacterInstance::StarForceEquipped(EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return kStarForceFail;
  }
  StarForceOutcome outcome = it->second.StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    equipped_.erase(it);
  }
  RecomputeEquipStats();
  return outcome;
}

StarForceOutcome CharacterInstance::StarForceInventory(int index) {
  if (index < 0 || index >= static_cast<int>(inventory_.size())) {
    return kStarForceFail;
  }
  StarForceOutcome outcome = inventory_[index].StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    inventory_.erase(inventory_.begin() + index);
  }
  return outcome;
}

bool CharacterInstance::CanEquip(const EquipPrototype& proto) const {
  if (proto.required_level() > 0 &&
      character_.level() < proto.required_level()) {
    return false;
  }
  EquipJobCategory char_cat = JobToCategory(character_.job());
  if (char_cat == EQUIP_JOB_CATEGORY_UNSPECIFIED) {
    return false;
  }
  for (int cat : proto.equip_job_categories()) {
    if (cat == EQUIP_JOB_CATEGORY_UNIVERSAL || cat == char_cat) {
      return true;
    }
  }
  return false;
}

}  // namespace ms
