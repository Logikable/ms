/* CharacterInstance represents a player character. It wraps a Character proto
 * (serializable state: level, job, job_stage, unspent AP, and allocated stats)
 * and exposes methods for leveling up, job advancement, and AP allocation.
 * character.cc implements those methods and the AP bonus rules.
 */
#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

#include "src/character.pb.h"
#include "src/equip.pb.h"

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
  // Constructs a fresh Equip state from `prototype` and appends it to the
  // equip inventory.
  void PickUp(const EquipPrototype& prototype);
  // Moves the item at `inventory_index` from the equip tab into `slot`. If the
  // slot was occupied, the displaced item is appended to the equip tab. Returns
  // false if `slot` is unspecified or `inventory_index` is out of range.
  bool Equip(EquipSlot slot, int inventory_index);
  // Moves the item in `slot` to the equip tab. Returns false if `slot` is
  // unspecified or unoccupied.
  bool Unequip(EquipSlot slot);

  const Character& proto() const { return character_; }

 private:
  Character character_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
