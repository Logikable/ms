/* CharacterInstance represents a player character. It wraps a Character proto
 * (serializable state: level, job, job_stage, unspent AP, and allocated stats)
 * and exposes methods for leveling up, job advancement, AP allocation, and
 * inventory management. Inventory holds EquipTabItem objects (EquipInstance for
 * live items, EquipTrace for destroyed items); equipped items are always
 * EquipInstance. Character proto fields for items are reserved for
 * serialization. character.cc implements all methods.
 */
#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

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

namespace ms {

class CharacterInstance {
 public:
  CharacterInstance(std::mt19937& rng, Character character);

  // Increments level and grants 5 AP.
  void LevelUp();
  // Increments job_stage, sets job to `next_job`, and grants 5 bonus AP at
  // stages 3 and 4 (3rd and 4th job advancement).
  void AdvanceJob(Job next_job);
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);
  // Assigns all available AP to `field`. Respects per-job stat caps (not yet
  // implemented). Returns false if `field` is unspecified or AP is 0.
  bool AllocateAllStat(StatField field);
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
  // Sum of stats from all currently equipped items. Updated automatically by
  // Equip, Unequip, and ScrollEquipped.
  const EquipStats& equip_stats() const {
    return equip_stats_;
  }

 private:
  // Recomputes equip_stats_ from the current equipped map.
  void RecomputeEquipStats();

  std::mt19937& rng_;
  Character character_;
  InventoryInstance inventory_;
  std::map<EquipSlot, EquipInstance> equipped_;
  EquipStats equip_stats_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
