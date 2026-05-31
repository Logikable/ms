/* CharacterInstance represents a player character. It wraps a Character proto
 * (serializable state: level, job, job_stage, unspent AP, and allocated stats)
 * and exposes methods for leveling up, job advancement, AP allocation, and
 * inventory management. Runtime inventory and equipped items are held as
 * EquipInstance objects; the Character proto fields for those are reserved for
 * serialization. character.cc implements all methods.
 */
#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

#include <map>
#include <random>
#include <vector>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/equip_instance.h"
#include "src/protos/scroll.pb.h"

namespace ms {

class CharacterInstance {
 public:
  explicit CharacterInstance(Character character);

  // Increments level and grants 5 AP.
  void LevelUp();
  // Increments job_stage, sets job to `next_job`, and grants 5 bonus AP at
  // stages 3 and 4 (3rd and 4th job advancement).
  void AdvanceJob(Job next_job);
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);
  // Constructs a fresh EquipInstance from `prototype` and appends it to the
  // inventory.
  void PickUp(const EquipPrototype& prototype);
  // Moves the item at `inventory_index` into the slot indicated by its
  // EquipPrototype. If the slot was occupied, the displaced item is appended to
  // inventory. Returns false if `inventory_index` is out of range or the
  // prototype's equip_slot is unspecified.
  bool Equip(int inventory_index);
  // Moves the item in `slot` to inventory. Returns false if `slot` is
  // unspecified or unoccupied.
  bool Unequip(EquipSlot slot);
  // Applies `scroll` to the item in `slot`. Returns false if the slot is empty
  // or the underlying Scroll() call fails (no slots remaining or RNG miss).
  bool ScrollEquipped(EquipSlot slot, const Scroll& scroll, std::mt19937& rng);

  const Character& proto() const { return character_; }
  const std::vector<EquipInstance>& inventory() const { return inventory_; }
  const std::map<EquipSlot, EquipInstance>& equipped() const {
    return equipped_;
  }

 private:
  Character character_;
  std::vector<EquipInstance> inventory_;
  std::map<EquipSlot, EquipInstance> equipped_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
