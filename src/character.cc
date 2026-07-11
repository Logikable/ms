#include "src/character.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/exp_table.h"
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

void CharacterInstance::AddExp(int64_t amount) {
  character_.set_exp(character_.exp() + amount);
  while (character_.level() < kMaxLevel) {
    int64_t threshold = ExpToNextLevel(character_.level());
    if (character_.exp() < threshold) {
      break;
    }
    character_.set_exp(character_.exp() - threshold);
    LevelUp();
  }
  if (character_.level() >= kMaxLevel) {
    character_.set_exp(0);
  }
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

void CharacterInstance::PickUp(std::unique_ptr<EquipTabItem> item) {
  inventory_.add(std::move(item));
}

void CharacterInstance::AddStackable(const ItemPrototype& proto, int count) {
  if (count <= 0) {
    return;
  }
  // Top up existing stacks of the same item before opening new ones.
  for (StackableItem& stack : stackables_) {
    if (count <= 0) {
      break;
    }
    if (stack.name() != proto.name()) {
      continue;
    }
    int room = stack.max_stack() - stack.count();
    if (room <= 0) {
      continue;
    }
    int added = std::min(room, count);
    stack.add_count(added);
    count -= added;
  }
  // Open new stacks for any remaining overflow.
  while (count > 0) {
    StackableItem stack(proto, 0);
    int added = std::min(stack.max_stack(), count);
    stack.add_count(added);
    count -= added;
    stackables_.push_back(std::move(stack));
  }
}

void CharacterInstance::AddMeso(int64_t amount) {
  if (amount <= 0) {
    return;
  }
  character_.set_meso(character_.meso() + amount);
}

int64_t CharacterInstance::SellStackable(int stack_index, int count) {
  if (stack_index < 0 || stack_index >= static_cast<int>(stackables_.size())) {
    return 0;
  }
  StackableItem& stack = stackables_[stack_index];
  count = std::clamp(count, 0, stack.count());
  int price = stack.prototype().sell_price();
  if (count <= 0 || price <= 0) {
    return 0;  // Nothing to sell, or the item cannot be sold.
  }
  int64_t earned = static_cast<int64_t>(count) * price;
  stack.add_count(-count);
  if (stack.count() <= 0) {
    stackables_.erase(stackables_.begin() + stack_index);
  }
  AddMeso(earned);
  return earned;
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

bool CharacterInstance::MeetsLevel(const EquipPrototype& proto) const {
  return proto.required_level() == 0 ||
         character_.level() >= proto.required_level();
}

bool CharacterInstance::MeetsJob(const EquipPrototype& proto) const {
  if (proto.equip_job_categories_size() == 0) {
    return true;
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
