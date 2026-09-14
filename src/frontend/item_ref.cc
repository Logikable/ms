#include "src/frontend/item_ref.h"

namespace ms {

ItemRef ItemRef::Equipped(EquipSlot slot, StatPreset preset) {
  ItemRef ref;
  ref.equipped_ = true;
  ref.slot_ = slot;
  ref.preset_ = preset;
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
    return character.WornAt(preset_, slot_);
  }
  if (index_ < 0 || index_ >= character.inventory().size()) {
    return nullptr;
  }
  return &character.inventory()[index_];
}

const EquipInstance* ItemRef::GetInstance(
    const CharacterInstance& character) const {
  if (equipped_) {
    return character.WornAt(preset_, slot_);
  }
  return character.inventory().equip_instance(index_);
}

ScrollOutcome ScrollItem(CharacterInstance& character, ItemRef ref,
                         const Scroll& scroll) {
  if (ref.equipped()) {
    return character.ScrollEquipped(ref.slot(), scroll, ref.preset());
  }
  return character.ScrollInventory(ref.index(), scroll);
}

StarForceOutcome StarForceItem(CharacterInstance& character, ItemRef ref) {
  if (ref.equipped()) {
    return character.StarForceEquipped(ref.slot(), ref.preset());
  }
  return character.StarForceInventory(ref.index());
}

bool HammerItem(CharacterInstance& character, ItemRef ref) {
  if (ref.equipped()) {
    return character.HammerEquipped(ref.slot(), ref.preset());
  }
  return character.HammerInventory(ref.index());
}

bool CubeItem(CharacterInstance& character, ItemRef ref, CubeType cube) {
  if (ref.equipped()) {
    return character.CubeEquipped(ref.slot(), cube, ref.preset());
  }
  return character.CubeInventory(ref.index(), cube);
}

}  // namespace ms
