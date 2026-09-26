#include "src/item/tradeable.h"

#include "src/character/symbol.h"
#include "src/item/currency.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

bool CanTrade(const ItemPrototype& proto) {
  return !IsCurrency(proto);
}

bool CanTrade(const EquipPrototype& proto) {
  return !IsSymbol(proto);
}

}  // namespace ms
