#include "src/character.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/inventory.h"
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
      // TODO: Demon Avenger gains 15 HP per AP instead of 1.
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

void CharacterInstance::PickUp(const EquipPrototype& prototype,
                               const ::ms::Equip& state) {
  inventory_.add(std::make_unique<EquipInstance>(prototype, state));
}

void CharacterInstance::PickUpTrace(const EquipPrototype& prototype,
                                    const ::ms::Equip& state) {
  inventory_.add(std::make_unique<EquipTrace>(prototype, state));
}

std::vector<const EquipTrace*> CharacterInstance::traces() const {
  return inventory_.traces();
}

bool CharacterInstance::Equip(int inventory_index) {
  EquipInstance* raw = inventory_.equip_instance(inventory_index);
  if (raw == nullptr) {
    return false;
  }
  EquipSlot slot = raw->prototype().equip_slot();
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  // Remove the item from inventory; move it out before the unique_ptr drops.
  std::unique_ptr<EquipTabItem> ptr = inventory_.remove_equip(inventory_index);
  EquipInstance item = std::move(static_cast<EquipInstance&>(*ptr));

  // If the slot was occupied, put the displaced item in the vacated position.
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it != equipped_.end()) {
    inventory_.add(std::make_unique<EquipInstance>(std::move(it->second)),
                   inventory_index);
    equipped_.erase(it);
  }

  // Equip the item.
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
  inventory_.add(std::make_unique<EquipInstance>(std::move(it->second)));
  equipped_.erase(it);
  RecomputeEquipStats();
  return true;
}

ScrollOutcome CharacterInstance::ScrollEquipped(EquipSlot slot,
                                                const Scroll& scroll) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return kScrollFail;
  }
  ScrollOutcome result = it->second.Scroll(scroll, rng_);
  if (result == kScrollSuccess) {
    RecomputeEquipStats();
  }
  return result;
}

ScrollOutcome CharacterInstance::ScrollInventory(int index,
                                                 const Scroll& scroll) {
  EquipInstance* item = inventory_.equip_instance(index);
  if (item == nullptr) {
    return kScrollFail;
  }
  return item->Scroll(scroll, rng_);
}

StarForceOutcome CharacterInstance::StarForceEquipped(EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return kStarForceFail;
  }
  StarForceOutcome outcome = it->second.StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    // equip_state() captures the item's state before the destroy attempt
    // (stars at the doomed level, not stars+1).
    inventory_.add(std::make_unique<EquipTrace>(it->second.prototype(),
                                                it->second.equip_state()));
    equipped_.erase(it);
  }
  RecomputeEquipStats();
  return outcome;
}

StarForceOutcome CharacterInstance::StarForceInventory(int index) {
  EquipInstance* item = inventory_.equip_instance(index);
  if (item == nullptr) {
    return kStarForceFail;
  }
  StarForceOutcome outcome = item->StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    // Arguments to make_unique are evaluated before set() destructs the old
    // item, so item->prototype() and item->equip_state() are safe to call here.
    inventory_.set(index, std::make_unique<EquipTrace>(item->prototype(),
                                                       item->equip_state()));
  }
  return outcome;
}

int CharacterInstance::RecoverTrace(int trace_index, int base_item_index) {
  int recovery_stars =
      EquipInstance::RecoveryStars(inventory_[trace_index].stars());
  EquipPrototype proto = inventory_[trace_index].prototype();
  ::ms::Equip new_state = inventory_[trace_index].equip_state();
  new_state.set_equip_name(proto.name());
  new_state.set_stars(recovery_stars);

  // Remove the higher index first to keep the lower index valid.
  int lo = std::min(trace_index, base_item_index);
  int hi = std::max(trace_index, base_item_index);
  inventory_.remove_equip(hi);
  inventory_.remove_equip(lo);
  inventory_.add(std::make_unique<EquipInstance>(proto, new_state));
  return recovery_stars;
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
