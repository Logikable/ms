/* The currencies a character holds: spell traces, the shops' tokens and the
 * bosses' soul shards.
 *
 * A currency is a balance, not a stack. It costs no slot on the Etc tab, no
 * per-stack limit caps it, and it is saved as a number beside meso rather
 * than as a row in the bag -- which is the whole difference between this and
 * //src/item/item.h's StackableItem. Which items are currencies is the
 * prototype's to say; see ItemKind.
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

// Whether `proto` is counted rather than carried: a currency the player
// spends -- traces and tokens -- or banks, as a soul shard is banked one per
// clear. An ordinary drop names no kind at all.
bool IsCurrency(const ItemPrototype& proto);

// Whether `proto` may leave the character holding it -- across a trade, or
// into the bank. A currency may not: it is a balance rather than a row, and
// both screens move it on a line of its own instead. Nothing else on the
// Equip or Etc tabs is held back, so this is the one place an item that
// cannot be handed on would say so.
bool CanTrade(const ItemPrototype& proto);

// One currency and what the character has of it. Holds the prototype by value
// the way StackableItem does, so a row can be drawn without the catalog.
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
  // Moves the balance by `delta`. Callers keep the result non-negative; this
  // does not clamp.
  void add_count(int64_t delta) {
    count_ += delta;
  }

 private:
  ItemPrototype prototype_;
  int64_t count_;
};

// Every currency the character holds any of, filed in the order the Token tab
// reads them. A balance that reaches zero leaves: the purse lists what there
// is, never a row of nothing.
class CurrencyPurse {
 public:
  // Banks `count` of `proto`. Nothing refuses it -- there is no room to run
  // out of -- so unlike the bag this returns nothing.
  void Add(const ItemPrototype& proto, int64_t count);
  // The balance in the named currency, or 0 for one the character has none
  // of. Matched on display name, as everything crossing a save does.
  int64_t Count(const std::string& name) const;
  // Spends `count`. All or nothing: false and nothing taken on a short purse.
  bool Spend(const std::string& name, int64_t count);
  // Whether the purse has a balance under this name at all. What tells a
  // currency from an Etc stack where only the name is in hand.
  bool Holds(const std::string& name) const;

  const std::vector<CurrencyAmount>& entries() const {
    return entries_;
  }

  // Reads `saved` -- Character.currencies -- resolving names against the item
  // catalog. A name no longer in data/ is dropped, as a stack naming a missing
  // item is.
  void RestoreFrom(const google::protobuf::Map<std::string, int64_t>& saved,
                   const std::map<std::string, const ItemPrototype*>& by_name);
  // The purse as the save holds it.
  google::protobuf::Map<std::string, int64_t> ToProto() const;

 private:
  // Puts `entries_` back in the Token tab's order. The balance is one of the
  // keys, so every mutation re-files the purse: there is no Sort button for
  // it, and nothing holds an index into it across a frame.
  void Sort();

  std::vector<CurrencyAmount> entries_;
};

// The entries of `purse` whose kind is `kind`, as indices into entries(). The
// Token tab draws two of these side by side.
std::vector<int> CurrenciesOf(const CurrencyPurse& purse, ItemKind kind);

}  // namespace ms

#endif  // MS_SRC_ITEM_CURRENCY_H_
