#include "src/item/stack_tab.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/item/inventory_sort.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {

int StackTab::room() const {
  return kTabCapacity - size();
}

bool StackTab::full() const {
  return room() <= 0;
}

int StackTab::RoomFor(const ItemPrototype& proto) const {
  // A stack that is open but not full takes more without costing a slot.
  int open = 0;
  for (const StackableItem& stack : items_) {
    if (stack.name() == proto.name()) {
      open += stack.max_stack() - stack.count();
    }
  }
  if (full()) {
    return open;
  }
  // Sized from the prototype rather than an existing stack, so an item the
  // tab holds none of still reports what a fresh stack would hold.
  StackableItem fresh(proto, 0);
  return open + room() * fresh.max_stack();
}

int StackTab::Add(const ItemPrototype& proto, int count) {
  if (count <= 0) {
    return 0;
  }
  count = std::min(count, RoomFor(proto));
  int added = count;
  // Top up existing stacks of the same item before opening new ones.
  for (StackableItem& stack : items_) {
    if (count <= 0) {
      break;
    }
    if (stack.name() != proto.name()) {
      continue;
    }
    int open = stack.max_stack() - stack.count();
    if (open <= 0) {
      continue;
    }
    int taken = std::min(open, count);
    stack.add_count(taken);
    count -= taken;
  }
  while (count > 0) {
    StackableItem stack(proto, 0);
    int taken = std::min(stack.max_stack(), count);
    stack.add_count(taken);
    count -= taken;
    items_.push_back(std::move(stack));
  }
  return added;
}

int64_t StackTab::Count(const std::string& name) const {
  int64_t owned = 0;
  for (const StackableItem& stack : items_) {
    if (stack.name() == name) {
      owned += stack.count();
    }
  }
  return owned;
}

bool StackTab::Spend(const std::string& name, int64_t count) {
  if (count <= 0 || Count(name) < count) {
    return false;
  }
  // Emptied stacks are dropped as they go, so spending the last of something
  // leaves no zero row behind.
  for (int i = size() - 1; i >= 0 && count > 0; --i) {
    if (items_[i].name() != name) {
      continue;
    }
    count -=
        Take(i, static_cast<int>(std::min<int64_t>(count, items_[i].count())));
  }
  return true;
}

int StackTab::Take(int index, int count) {
  if (index < 0 || index >= size()) {
    return 0;
  }
  count = std::clamp(count, 0, items_[index].count());
  items_[index].add_count(-count);
  if (items_[index].count() == 0) {
    items_.erase(items_.begin() + index);
  }
  return count;
}

void StackTab::Sort() {
  SortStacks(items_);
}

void StackTab::RestoreFrom(
    const google::protobuf::RepeatedPtrField<StackableStack>& saved,
    const std::map<std::string, const ItemPrototype*>& by_name) {
  items_.clear();
  for (const StackableStack& stack : saved) {
    std::map<std::string, const ItemPrototype*>::const_iterator proto =
        by_name.find(stack.name());
    if (proto == by_name.end()) {
      continue;
    }
    items_.push_back(StackableItem(*proto->second, stack.count()));
  }
}

void StackTab::AppendTo(
    google::protobuf::RepeatedPtrField<StackableStack>* out) const {
  for (const StackableItem& stack : items_) {
    StackableStack* saved = out->Add();
    saved->set_name(stack.name());
    saved->set_count(stack.count());
  }
}

}  // namespace ms
