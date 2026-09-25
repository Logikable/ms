/* ItemRef says where an item the player picked is: worn in an equip slot, or at
 * an index in the bag. Every modal opened on an item (inspect, scroll, star
 * force, hammer) opens on one of these.
 *
 * It means "is this item worn or in the bag?" is answered once, when the player
 * picks the item, instead of at every place that needs the item later.
 */
#ifndef MS_SRC_FRONTEND_ITEM_REF_H_
#define MS_SRC_FRONTEND_ITEM_REF_H_

#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

class ItemRef {
 public:
  // A ref to nothing. Get() and GetInstance() return nullptr for it.
  ItemRef() = default;
  // `preset` is the gear preset the item was picked from, which decides which
  // item a slot holds once presets differ.
  static ItemRef Equipped(EquipSlot slot,
                          StatPreset preset = StatPreset::kFirst);
  static ItemRef InBag(int index);

  bool equipped() const {
    return equipped_;
  }
  EquipSlot slot() const {
    return slot_;
  }
  StatPreset preset() const {
    return preset_;
  }
  int index() const {
    return index_;
  }

  // The item, or nullptr if this ref names nothing (an empty slot, or a bag
  // index past the end). May be a trace.
  const EquipTabItem* Get(const CharacterInstance& character) const;
  // The item as a live EquipInstance, or nullptr if it is a trace or the ref
  // names nothing. A trace has no instance: it is what is left after star force
  // destroyed an item.
  const EquipInstance* GetInstance(const CharacterInstance& character) const;

 private:
  bool equipped_ = false;
  EquipSlot slot_ = EQUIP_SLOT_UNSPECIFIED;
  StatPreset preset_ = StatPreset::kFirst;
  int index_ = 0;
};

// Applies `scroll` to the item `ref` names. The two paths really differ, since
// scrolling a worn item must recompute the character's totals, so
// CharacterInstance keeps them separate and this just picks one.
ScrollOutcome ScrollItem(CharacterInstance& character, ItemRef ref,
                         const Scroll& scroll);

// Star forces the item `ref` names. Same split as ScrollItem: a destroyed worn
// item moves to the bag as a trace, and a bag item is replaced in place.
StarForceOutcome StarForceItem(CharacterInstance& character, ItemRef ref);

// Uses a golden hammer on the item `ref` names and returns whether it worked.
// Same split again, because a worn item's totals are rebuilt.
bool HammerItem(CharacterInstance& character, ItemRef ref);

// Charges for one `cube` and applies its roll to the item `ref` names. Returns
// false, spending nothing, if the item can't have potential or the character
// can't afford it.
bool CubeItem(CharacterInstance& character, ItemRef ref, CubeType cube);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_ITEM_REF_H_
