#include "src/item/slot_order.h"

#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// A table instead of the enum's order: a slot number is how a save identifies a
// worn item and can never change, so the rings and second pendant would come at
// the end instead of with their families. Only the first slot of each family is
// listed.
constexpr EquipSlot kSlotOrder[] = {
    EQUIP_SLOT_PRIMARY_WEAPON,
    EQUIP_SLOT_HAT,
    EQUIP_SLOT_TOP,
    EQUIP_SLOT_BOTTOM,
    EQUIP_SLOT_SHOES,
    EQUIP_SLOT_GLOVES,
    EQUIP_SLOT_CAPE,
    EQUIP_SLOT_SHOULDER,
    EQUIP_SLOT_BELT,
    EQUIP_SLOT_FACE_ACCESSORY,
    EQUIP_SLOT_EYE_ACCESSORY,
    EQUIP_SLOT_EARRINGS,
    EQUIP_SLOT_PENDANT,
    EQUIP_SLOT_RING,
    EQUIP_SLOT_EMBLEM,
    EQUIP_SLOT_BADGE,
    EQUIP_SLOT_MEDAL,
    EQUIP_SLOT_POCKET,
    EQUIP_SLOT_PROJECTILE,
    EQUIP_SLOT_SECONDARY,
    EQUIP_SLOT_HEART,
    EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY,
    EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
    EQUIP_SLOT_SYMBOL_LACHELEIN,
    EQUIP_SLOT_SYMBOL_ARCANA,
    EQUIP_SLOT_SYMBOL_MORASS,
    EQUIP_SLOT_SYMBOL_ESFERA,
};

constexpr int kSlotOrderSize =
    static_cast<int>(sizeof(kSlotOrder) / sizeof(kSlotOrder[0]));

// Every slot except UNSPECIFIED and the four extra family slots. A slot added
// without a place in the list would silently sort to the bottom.
static_assert(EquipSlot_ARRAYSIZE == kSlotOrderSize + 5,
              "a new slot needs a place in kSlotOrder");

}  // namespace

int SlotOrder(EquipSlot slot) {
  EquipSlot base = BaseSlot(slot);
  for (int i = 0; i < kSlotOrderSize; ++i) {
    if (kSlotOrder[i] == base) {
      return i * 10 + SlotIndex(slot);
    }
  }
  return kSlotOrderSize * 10;
}

}  // namespace ms
