/* The currencies a character holds: spell traces, shop tokens and boss soul
 * shards.
 *
 * A currency is a balance, not a stack. It uses no Etc tab slot, has no stack
 * limit, and is saved as a number next to meso instead of a row in the bag.
 * That's the whole difference from //src/item/item.h's StackableItem. The
 * prototype decides which items are currencies; see ItemKind.
 */
#ifndef MS_SRC_ITEM_CURRENCY_H_
#define MS_SRC_ITEM_CURRENCY_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "google/protobuf/map.h"
#include "src/protos/item.pb.h"

namespace ms {

// Whether `proto` is counted instead of carried: a currency the player spends
// (traces and tokens) or collects (soul shards, one per clear). An ordinary
// drop has no kind.
bool IsCurrency(const ItemPrototype& proto);

// One currency and how much of it the character has. Holds the prototype by
// value like StackableItem does, so a row can be drawn without the catalog.
class CurrencyAmount {
 public:
  CurrencyAmount(ItemPrototype prototype, int64_t count)
      : prototype_(std::move(prototype)), count_(count) {
  }

  const std::string& name() const {
    return prototype_.name();
  }
  const ItemPrototype& prototype() const {
    return prototype_;
  }
  int64_t count() const {
    return count_;
  }
  // Changes the balance by `delta`. Callers keep it non-negative; this doesn't
  // clamp.
  void add_count(int64_t delta) {
    count_ += delta;
  }

 private:
  ItemPrototype prototype_;
  int64_t count_;
};

// Every currency the character has any of, sorted in the Token tab's order. A
// balance that reaches zero is removed: the purse lists what exists, never a
// zero row.
class CurrencyPurse {
 public:
  // Adds `count` of `proto`. Nothing can refuse it, since there's no room
  // limit, so unlike the bag this returns nothing.
  void Add(const ItemPrototype& proto, int64_t count);
  // The balance of the named currency, or 0 if the character has none. Matched
  // by display name, as everything in a save is.
  int64_t Count(const std::string& name) const;
  // Spends `count`. All or nothing: returns false and takes nothing if the
  // balance is short.
  bool Spend(const std::string& name, int64_t count);
  // Whether the purse has a balance under this name. Tells a currency from an
  // Etc stack when only the name is known.
  bool Holds(const std::string& name) const;

  const std::vector<CurrencyAmount>& entries() const {
    return entries_;
  }

  // Loads `saved` (Character.currencies), resolving names against the item
  // catalog. A name no longer in data/ is dropped, as a stack naming a missing
  // item is.
  void RestoreFrom(const google::protobuf::Map<std::string, int64_t>& saved,
                   const std::map<std::string, const ItemPrototype*>& by_name);
  // The purse as the save stores it.
  google::protobuf::Map<std::string, int64_t> ToProto() const;

 private:
  // Restores `entries_` to the Token tab's order. The balance is a sort key, so
  // every change re-sorts the purse: there's no Sort button for it, and nothing
  // keeps an index into it across frames.
  void Sort();

  std::vector<CurrencyAmount> entries_;
};

// Indices into entries() of `purse`'s currencies of kind `kind`. The Token tab
// draws two of these side by side.
std::vector<int> CurrenciesOf(const CurrencyPurse& purse, ItemKind kind);

}  // namespace ms

#endif  // MS_SRC_ITEM_CURRENCY_H_
