#include "src/inventory.h"

#include <memory>
#include <vector>

namespace ms {

EquipInstance* InventoryInstance::equip_instance(int index) {
  if (index < 0 || index >= static_cast<int>(equip_items_.size())) {
    return nullptr;
  }
  return dynamic_cast<EquipInstance*>(equip_items_[index].get());
}

const EquipInstance* InventoryInstance::equip_instance(int index) const {
  if (index < 0 || index >= static_cast<int>(equip_items_.size())) {
    return nullptr;
  }
  return dynamic_cast<const EquipInstance*>(equip_items_[index].get());
}

const EquipTabItem& InventoryInstance::operator[](int index) const {
  return *equip_items_[index];
}

int InventoryInstance::size() const {
  return static_cast<int>(equip_items_.size());
}

bool InventoryInstance::empty() const {
  return equip_items_.empty();
}

std::vector<const EquipTrace*> InventoryInstance::traces() const {
  std::vector<const EquipTrace*> result;
  for (const std::unique_ptr<EquipTabItem>& item : equip_items_) {
    if (const EquipTrace* t = dynamic_cast<const EquipTrace*>(item.get())) {
      result.push_back(t);
    }
  }
  return result;
}

void InventoryInstance::add(std::unique_ptr<EquipTabItem> item, int index) {
  if (index == -1) {
    equip_items_.push_back(std::move(item));
  } else {
    equip_items_.insert(equip_items_.begin() + index, std::move(item));
  }
}

std::unique_ptr<EquipTabItem> InventoryInstance::remove_equip(int index) {
  std::unique_ptr<EquipTabItem> item = std::move(equip_items_[index]);
  equip_items_.erase(equip_items_.begin() + index);
  return item;
}

void InventoryInstance::set(int index, std::unique_ptr<EquipTabItem> item) {
  equip_items_[index] = std::move(item);
}

}  // namespace ms
