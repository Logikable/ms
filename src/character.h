/* CharacterInstance represents a player character. It wraps a Character proto
 * (serializable state: level, job, job_stage, unspent AP and SP, allocated
 * stats, and learned skill levels) and exposes methods for leveling up, job
 * advancement, AP allocation, and inventory management. Inventory holds
 * EquipTabItem objects (EquipInstance for live items, EquipTrace for destroyed
 * items); equipped items are always EquipInstance. Character proto fields for
 * items are reserved for serialization. character.cc implements all methods.
 */
#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <vector>

#include "src/equip_instance.h"
#include "src/inventory.h"
#include "src/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

class CharacterInstance {
 public:
  CharacterInstance(std::mt19937& rng, Character character);

  // Increments level and grants 5 AP, plus 3 SP into the job stage whose level
  // band the new level falls in (levels 11-30 -> 1st job, 31-60 -> 2nd, ...).
  void LevelUp();
  // Adds amount to the character's accumulated EXP, leveling up as many times
  // as the new total allows. No-op once kMaxLevel is reached.
  void AddExp(int64_t amount);
  // Increments job_stage, sets job to `next_job`, grants 5 bonus AP at stages
  // 3 and 4 (3rd and 4th job advancement), and grants a batch of SP for the
  // newly opened skill set.
  void AdvanceJob(Job next_job);
  // The job the character is eligible to advance into now, or JOB_UNSPECIFIED
  // if no advancement is currently available. The UI offers the advancement
  // when this is set; passing the result to AdvanceJob performs it.
  Job PendingJobAdvancement() const;
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);
  // Spends `amount` of the skill's job-stage SP to raise its learned level by
  // that much. Returns false if `amount` <= 0, that stage has less SP than
  // `amount`, or it would raise the level past skill.max_level().
  bool LearnSkill(const Skill& skill, int amount = 1);
  // Returns true if the character meets the level and job requirements to
  // equip the item described by `proto`.
  bool CanEquip(const EquipPrototype& proto) const;
  // Returns true if the character's level meets proto's required level.
  bool MeetsLevel(const EquipPrototype& proto) const;
  // Returns true if the character's job category matches proto's job filter.
  bool MeetsJob(const EquipPrototype& proto) const;
  // Appends item to inventory. Accepts any EquipTabItem subclass (EquipInstance
  // or EquipTrace); the caller is responsible for constructing the item.
  void PickUp(std::unique_ptr<EquipTabItem> item);
  // Adds `count` of the item described by `proto` to the Use/Etc stacks. Tops
  // up existing stacks of the same item first, then opens new stacks for any
  // overflow, each capped at the item's max_stack(). No-op if count <= 0.
  void AddStackable(const ItemPrototype& proto, int count);
  // Adds `amount` meso to the character's balance. No-op if amount <= 0.
  void AddMeso(int64_t amount);
  // Sells up to `count` copies from the `index`-th stack in `category`,
  // crediting count * sell_price meso and removing the sold copies; erases the
  // stack entirely once it empties. No-op returning 0 if the index is out of
  // range, count <= 0, or the item is unsellable (sell_price 0). Returns the
  // meso earned.
  int64_t SellStackable(ItemCategory category, int index, int count);
  // Moves the item at `inventory_index` into the slot indicated by its
  // EquipPrototype. If the slot was occupied, the displaced item is appended
  // to inventory. Returns false if `inventory_index` is out of range or the
  // prototype's equip_slot is unspecified.
  bool Equip(int inventory_index);
  // Moves the item in `slot` to inventory. Returns false if `slot` is
  // unspecified or unoccupied.
  bool Unequip(EquipSlot slot);
  // Applies `scroll` to the item in `slot`. Returns kScrollFail if the slot
  // is empty; otherwise returns the result of the underlying Scroll() call.
  ScrollOutcome ScrollEquipped(EquipSlot slot, const Scroll& scroll);
  // Applies `scroll` to the inventory item at `index`. Returns kScrollFail if
  // `index` is out of range; otherwise returns the result of Scroll().
  ScrollOutcome ScrollInventory(int index, const Scroll& scroll);
  // Applies a star force attempt to the item in `slot`. On kStarForceDestroy,
  // removes the item from equipped and recomputes equip stats.
  StarForceOutcome StarForceEquipped(EquipSlot slot);
  // Applies a star force attempt to the inventory item at `index`. On
  // kStarForceDestroy, removes the item from inventory.
  StarForceOutcome StarForceInventory(int index);
  // Recovers the EquipTrace at `trace_index` using the EquipInstance at
  // `base_item_index` as the replacement body. Both items are removed from
  // inventory and replaced with a new EquipInstance carrying the trace's scroll
  // stats and the star count from RecoveryStars(). The indices must refer to
  // valid, compatible inventory slots. Returns the recovery star count.
  int RecoverTrace(int trace_index, int base_item_index);

  const Character& proto() const {
    return character_;
  }
  const InventoryInstance& inventory() const {
    return inventory_;
  }
  std::vector<const EquipTrace*> traces() const;
  const std::map<EquipSlot, EquipInstance>& equipped() const {
    return equipped_;
  }
  // The `category`'s item stacks (ITEM_CATEGORY_USE or ITEM_CATEGORY_ETC), in
  // pickup order.
  const std::vector<StackableItem>& stackables(ItemCategory category) const {
    return StacksFor(category);
  }
  int64_t meso() const {
    return character_.meso();
  }
  // Available skill points for the given job stage (1 = 1st job, ...), or 0 if
  // none have been earned for it.
  int sp(int stage) const {
    return character_.sp_by_stage().contains(stage)
               ? character_.sp_by_stage().at(stage)
               : 0;
  }
  // The character's learned level in `skill` (0 = unlearned).
  int skill_level(const Skill& skill) const {
    return character_.skill_levels().contains(skill.name())
               ? character_.skill_levels().at(skill.name())
               : 0;
  }
  // Sum of stats from all currently equipped items. Updated automatically by
  // Equip, Unequip, and ScrollEquipped.
  const EquipStats& equip_stats() const {
    return equip_stats_;
  }

 private:
  // Recomputes equip_stats_ from the current equipped map.
  void RecomputeEquipStats();
  // The stack vector backing `category`. USE and ETC each have their own;
  // anything else falls back to the Etc stacks (fail safe).
  std::vector<StackableItem>& StacksFor(ItemCategory category);
  const std::vector<StackableItem>& StacksFor(ItemCategory category) const;

  std::mt19937& rng_;
  Character character_;
  InventoryInstance inventory_;
  std::map<EquipSlot, EquipInstance> equipped_;
  std::vector<StackableItem> use_items_;
  std::vector<StackableItem> etc_items_;
  EquipStats equip_stats_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
