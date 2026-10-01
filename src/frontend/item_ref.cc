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

std::optional<Potential> RollCubeItem(CharacterInstance& character, ItemRef ref,
                                      CubeType cube, const Potential& from) {
  if (ref.equipped()) {
    return character.BuyCubeFrom(ref.slot(), cube, from, ref.preset());
  }
  return character.BuyInventoryCubeFrom(ref.index(), cube, from);
}

bool KeepPotential(CharacterInstance& character, ItemRef ref,
                   PotentialTrack track, const Potential& potential) {
  if (ref.equipped()) {
    return character.TakePotential(ref.slot(), track, potential, ref.preset());
  }
  return character.TakeInventoryPotential(ref.index(), track, potential);
}

std::optional<FlameLines> RollFlameItem(CharacterInstance& character,
                                        ItemRef ref, FlameType flame,
                                        const FlameLines& from) {
  if (ref.equipped()) {
    return character.BuyFlame(ref.slot(), flame, from, ref.preset());
  }
  return character.BuyInventoryFlame(ref.index(), flame, from);
}

bool KeepFlame(CharacterInstance& character, ItemRef ref,
               const FlameLines& lines) {
  if (ref.equipped()) {
    return character.TakeFlame(ref.slot(), lines, ref.preset());
  }
  return character.TakeInventoryFlame(ref.index(), lines);
}

}  // namespace ms
