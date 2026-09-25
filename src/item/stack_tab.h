/* StackTab holds the Etc tab: a bag's stacks, and the rules for filling and
 * emptying them.
 *
 * A tab has kTabCapacity slots, and a stack uses one however many copies it
 * holds, so topping up a stack is free and a full tab can still take part of a
 * drop. An emptied stack is removed instead of staying as a zero row.
 *
 * Currencies aren't here: a currency is a balance in a purse and uses no slot.
 * See //src/item/currency.h. Callers route between the two by the prototype's
 * kind; the tab accepts whatever it's given.
 */
#ifndef MS_SRC_ITEM_STACK_TAB_H_
#define MS_SRC_ITEM_STACK_TAB_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {

class StackTab {
 public:
  int size() const {
    return static_cast<int>(items_.size());
  }
  bool empty() const {
    return items_.empty();
  }
  const StackableItem& operator[](int index) const {
    return items_[index];
  }
  const std::vector<StackableItem>& items() const {
    return items_;
  }

  // Slots left on the tab, and whether there are any.
  int room() const;
  bool full() const;

  // How many more copies of `proto` the tab could take: the space in every open
  // stack of it, plus a full stack per free slot.
  int RoomFor(const ItemPrototype& proto) const;

  // Adds `count` copies and returns how many were added. Open stacks are filled
  // before new ones are started, and what doesn't fit is lost.
  int Add(const ItemPrototype& proto, int count);

  // Copies of `name` held, summed across all its stacks.
  int64_t Count(const std::string& name) const;

  // Spends `count` of `name`, newest stack first. All or nothing: returns false
  // and takes nothing if the tab holds fewer.
  bool Spend(const std::string& name, int64_t count);

  // Takes `count` from the `index`-th stack, clamped to what it holds, and
  // returns how many were taken. The stack is removed when it empties.
  int Take(int index, int count);

  void Sort();

  // Loads the stacks a save holds, resolving names against the item catalog. A
  // name no longer in data/ is dropped, as the purse does.
  void RestoreFrom(
      const google::protobuf::RepeatedPtrField<StackableStack>& saved,
      const std::map<std::string, const ItemPrototype*>& by_name);
  // Appends the tab to `out` in save format.
  void AppendTo(google::protobuf::RepeatedPtrField<StackableStack>* out) const;

 private:
  std::vector<StackableItem> items_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_STACK_TAB_H_
