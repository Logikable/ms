/* ItemRef names where an item the player picked lives: worn in an equip slot,
 * or sitting at an index in the bag. Every modal the game opens on an item --
 * inspect, scroll, star force -- opens on one of these.
 *
 * It exists so "is this item worn or in the bag?" is asked once, when the
 * player picks the item, instead of again at every place that needs the item
 * back. The controller used to keep a slot and an index side by side for each
 * modal and branch on the focused panel to decide which half was live.
 */
#ifndef MS_SRC_FRONTEND_ITEM_REF_H_
#define MS_SRC_FRONTEND_ITEM_REF_H_

#include "src/character/character.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

class ItemRef {
 public:
  // A ref naming nothing. Get() and GetInstance() return nullptr for it.
  ItemRef() = default;
  static ItemRef Equipped(EquipSlot slot);
  static ItemRef InBag(int index);

  bool equipped() const {
    return equipped_;
  }
  EquipSlot slot() const {
    return slot_;
  }
  int index() const {
    return index_;
  }

  // The item, or nullptr if this ref names nothing -- an empty slot, or a bag
  // index past the end. May be a trace.
  const EquipTabItem* Get(const CharacterInstance& character) const;
  // The item as a live EquipInstance, or nullptr if it is a trace or the ref
  // names nothing. A trace has no instance behind it: it is the husk left when
  // star forcing destroyed one.
  const EquipInstance* GetInstance(const CharacterInstance& character) const;

 private:
  bool equipped_ = false;
  EquipSlot slot_ = EQUIP_SLOT_UNSPECIFIED;
  int index_ = 0;
};

// Applies `scroll` to the item `ref` names. The two paths behind this really do
// differ -- scrolling something worn has to recompute the character's totals --
// so CharacterInstance keeps them apart and this only picks between them.
ScrollOutcome ScrollItem(CharacterInstance& character, ItemRef ref,
                         const Scroll& scroll);

// Star forces the item `ref` names. Same split as ScrollItem: a worn item that
// gets destroyed moves to the bag as a trace, a bag item is replaced in place.
StarForceOutcome StarForceItem(CharacterInstance& character, ItemRef ref);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_ITEM_REF_H_
