#include "src/frontend/item_ref.h"

#include <map>

namespace ms {

ItemRef ItemRef::Equipped(EquipSlot slot) {
  ItemRef ref;
  ref.equipped_ = true;
  ref.slot_ = slot;
  return ref;
}

ItemRef ItemRef::InBag(int index) {
  ItemRef ref;
  ref.equipped_ = false;
  ref.index_ = index;
  return ref;
}

const EquipTabItem* ItemRef::Get(const CharacterInstance& character) const {
  if (equipped_) {
    std::map<EquipSlot, EquipInstance>::const_iterator it =
        character.equipped().find(slot_);
    return it == character.equipped().end() ? nullptr : &it->second;
  }
  if (index_ < 0 || index_ >= character.inventory().size()) {
    return nullptr;
  }
  return &character.inventory()[index_];
}

const EquipInstance* ItemRef::GetInstance(
    const CharacterInstance& character) const {
  if (equipped_) {
    std::map<EquipSlot, EquipInstance>::const_iterator it =
        character.equipped().find(slot_);
    return it == character.equipped().end() ? nullptr : &it->second;
  }
  return character.inventory().equip_instance(index_);
}

ScrollOutcome ScrollItem(CharacterInstance& character, ItemRef ref,
                         const Scroll& scroll) {
  if (ref.equipped()) {
    return character.ScrollEquipped(ref.slot(), scroll);
  }
  return character.ScrollInventory(ref.index(), scroll);
}

StarForceOutcome StarForceItem(CharacterInstance& character, ItemRef ref) {
  if (ref.equipped()) {
    return character.StarForceEquipped(ref.slot());
  }
  return character.StarForceInventory(ref.index());
}

}  // namespace ms
