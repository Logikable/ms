#include "src/item/bank.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/account.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

bool BankInstance::AddEquip(std::unique_ptr<EquipTabItem> item) {
  if (item == nullptr || equips_.full()) {
    return false;
  }
  equips_.add(std::move(item));
  return true;
}

std::unique_ptr<EquipTabItem> BankInstance::TakeEquip(int index) {
  if (index < 0 || index >= equips_.size()) {
    return nullptr;
  }
  return equips_.remove_equip(index);
}

int BankInstance::AddItem(const ItemPrototype& proto, int count) {
  if (count <= 0) {
    return 0;
  }
  if (IsCurrency(proto)) {
    currencies_.Add(proto, count);
    return count;
  }
  return stacks_.Add(proto, count);
}

int BankInstance::RoomFor(const ItemPrototype& proto) const {
  // A currency has no limit: it's a number in the save, not a row on a tab.
  return IsCurrency(proto) ? INT_MAX : stacks_.RoomFor(proto);
}

int BankInstance::TakeStack(int index, int count) {
  return stacks_.Take(index, count);
}

void BankInstance::AddMeso(int64_t amount) {
  if (amount > 0) {
    meso_ += amount;
  }
}

bool BankInstance::SpendMeso(int64_t amount) {
  if (amount <= 0 || meso_ < amount) {
    return amount <= 0;
  }
  meso_ -= amount;
  return true;
}

int64_t BankInstance::CountCurrency(const std::string& name) const {
  return currencies_.Count(name);
}

bool BankInstance::SpendCurrency(const std::string& name, int64_t count) {
  return currencies_.Spend(name, count);
}

void BankInstance::SortEquips(
    const std::function<bool(const EquipPrototype&)>& equippable) {
  equips_.Sort(equippable);
}

void BankInstance::SortStacks() {
  stacks_.Sort();
}

void BankInstance::RestoreFrom(
    const Bank& saved,
    const std::map<std::string, const EquipPrototype*>& equips,
    const std::map<std::string, const ItemPrototype*>& items) {
  equips_ = InventoryInstance();
  for (const Equip& state : saved.equip_tab()) {
    std::map<std::string, const EquipPrototype*>::const_iterator proto =
        equips.find(state.equip_name());
    if (proto == equips.end()) {
      continue;
    }
    equips_.add(EquipItemFromState(*proto->second, state));
  }
  stacks_.RestoreFrom(saved.stacks(), items);
  currencies_.RestoreFrom(saved.currencies(), items);
  meso_ = saved.meso();
}

Bank BankInstance::ToProto() const {
  Bank saved;
  for (int i = 0; i < equips_.size(); ++i) {
    *saved.add_equip_tab() = equips_[i].SavedState();
  }
  stacks_.AppendTo(saved.mutable_stacks());
  *saved.mutable_currencies() = currencies_.ToProto();
  saved.set_meso(meso_);
  return saved;
}

}  // namespace ms
