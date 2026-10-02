#include "src/item/bank.h"

#include <algorithm>
#include <climits>
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

bool BankInstance::PageFull(int page) const {
  return pages_[page].size() >= kBankPageCapacity;
}

bool BankInstance::AddEquip(int page, std::unique_ptr<EquipTabItem> item) {
  if (item == nullptr || PageFull(page)) {
    return false;
  }
  pages_[page].add(std::move(item));
  return true;
}

std::unique_ptr<EquipTabItem> BankInstance::TakeEquip(int page, int index) {
  if (index < 0 || index >= pages_[page].size()) {
    return nullptr;
  }
  return pages_[page].remove_equip(index);
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

void BankInstance::AddVPoints(int64_t amount) {
  if (amount > 0) {
    v_points_ += amount;
  }
}

bool BankInstance::SpendVPoints(int64_t amount) {
  if (amount <= 0 || v_points_ < amount) {
    return amount <= 0;
  }
  v_points_ -= amount;
  return true;
}

int64_t BankInstance::CountCurrency(const std::string& name) const {
  return currencies_.Count(name);
}

bool BankInstance::SpendCurrency(const std::string& name, int64_t count) {
  return currencies_.Spend(name, count);
}

void BankInstance::SortEquips(
    int page, const std::function<bool(const EquipPrototype&)>& equippable) {
  pages_[page].Sort(equippable);
}

void BankInstance::SortStacks() {
  stacks_.Sort();
}

void BankInstance::RestoreFrom(
    const Bank& saved,
    const std::map<std::string, const EquipPrototype*>& equips,
    const std::map<std::string, const ItemPrototype*>& items) {
  pages_ = {};
  auto restore = [&](int page, const Equip& state) {
    std::map<std::string, const EquipPrototype*>::const_iterator proto =
        equips.find(state.equip_name());
    if (proto != equips.end()) {
      pages_[page].add(EquipItemFromState(*proto->second, state));
    }
  };
  // Positions count before names are resolved, so an item that left the data
  // doesn't shift every later one onto the page before.
  for (int i = 0; i < saved.legacy_equip_tab_size(); ++i) {
    if (i / kBankPageCapacity < kBankPages) {
      restore(i / kBankPageCapacity, saved.legacy_equip_tab(i));
    }
  }
  for (int page = 0; page < std::min(saved.equip_pages_size(), kBankPages);
       ++page) {
    for (const Equip& state : saved.equip_pages(page).equips()) {
      restore(page, state);
    }
  }
  stacks_.RestoreFrom(saved.stacks(), items);
  currencies_.RestoreFrom(saved.currencies(), items);
  meso_ = saved.meso();
  v_points_ = saved.v_points();
}

Bank BankInstance::ToProto() const {
  Bank saved;
  for (const InventoryInstance& page : pages_) {
    BankPage* out = saved.add_equip_pages();
    for (int i = 0; i < page.size(); ++i) {
      *out->add_equips() = page[i].SavedState();
    }
  }
  stacks_.AppendTo(saved.mutable_stacks());
  *saved.mutable_currencies() = currencies_.ToProto();
  saved.set_meso(meso_);
  saved.set_v_points(v_points_);
  return saved;
}

}  // namespace ms
