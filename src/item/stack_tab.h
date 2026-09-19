/* StackTab holds the Etc tab: the stacks a bag has on it, and the rules that
 * fill and empty one.
 *
 * A tab has kTabCapacity slots, and a stack takes one however many copies are
 * in it -- so topping an open stack up costs nothing and a full tab can still
 * absorb part of a drop. An emptied stack leaves rather than sitting there as
 * a row of nothing.
 *
 * Currencies are NOT here: a currency is a balance in a purse and costs no
 * slot. See //src/item/currency.h. Callers route between the two on the
 * prototype's kind; the tab takes whatever it is handed.
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

  // Slots left on the tab, and whether any are.
  int room() const;
  bool full() const;

  // How many more copies of `proto` the tab could take: the room in every
  // open stack of it, plus a full stack per free slot.
  int RoomFor(const ItemPrototype& proto) const;

  // Adds `count` copies and returns how many went in. Open stacks are topped
  // up before new ones are opened, and what does not fit is lost.
  int Add(const ItemPrototype& proto, int count);

  // Copies of `name` held, summed across every stack of it.
  int64_t Count(const std::string& name) const;

  // Spends `count` of `name`, newest stack first. All or nothing: false and
  // nothing taken when the tab holds less than that.
  bool Spend(const std::string& name, int64_t count);

  // Takes `count` off the `index`-th stack, clamped to what is in it, and
  // returns how many came out. The stack leaves the tab when it empties.
  int Take(int index, int count);

  void Sort();

  // Reads the stacks a save holds, resolving names against the item catalog.
  // A name no longer in data/ is dropped, as the purse drops one.
  void RestoreFrom(
      const google::protobuf::RepeatedPtrField<StackableStack>& saved,
      const std::map<std::string, const ItemPrototype*>& by_name);
  // Appends the tab to `out` as the save holds it.
  void AppendTo(google::protobuf::RepeatedPtrField<StackableStack>* out) const;

 private:
  std::vector<StackableItem> items_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_STACK_TAB_H_
