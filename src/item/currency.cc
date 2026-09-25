#include "src/item/currency.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "google/protobuf/map.h"
#include "src/item/slot_order.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// Sort rank for a currency's kind, lowest first. The Token tab draws tokens and
// shards in separate columns, so this only needs to keep those two apart and
// put the trace (shown as a balance in the tab bar, never a row) ahead of both.
int KindRank(ItemKind kind) {
  switch (kind) {
    case ITEM_KIND_SPELL_TRACE:
      return 0;
    case ITEM_KIND_TOKEN:
      return 1;
    default:
      return 2;
  }
}

// Sort rank for a token: best gear first, and within a level, the weapon before
// the set. A token that buys a whole set names no slot and comes first in its
// level. Only the first two ranks are specific to the shelf; after them it
// follows the Equipped panel's order, so the two agree.
int TokenSlotRank(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_UNSPECIFIED:
      return 0;
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return 1;
    case EQUIP_SLOT_SECONDARY:
      return 2;
    default:
      return 3 + SlotOrder(slot);
  }
}

}  // namespace

bool IsCurrency(const ItemPrototype& proto) {
  switch (proto.kind()) {
    case ITEM_KIND_SPELL_TRACE:
    case ITEM_KIND_TOKEN:
    case ITEM_KIND_SOUL_SHARD:
      return true;
    default:
      return false;
  }
}

void CurrencyPurse::Add(const ItemPrototype& proto, int64_t count) {
  if (count <= 0) {
    return;
  }
  std::vector<CurrencyAmount>::iterator it = std::find_if(
      entries_.begin(), entries_.end(),
      [&proto](const CurrencyAmount& e) { return e.name() == proto.name(); });
  if (it == entries_.end()) {
    entries_.push_back(CurrencyAmount(proto, count));
  } else {
    it->add_count(count);
  }
  // The balance affects the order, so every change re-sorts the purse.
  Sort();
}

int64_t CurrencyPurse::Count(const std::string& name) const {
  for (const CurrencyAmount& entry : entries_) {
    if (entry.name() == name) {
      return entry.count();
    }
  }
  return 0;
}

bool CurrencyPurse::Holds(const std::string& name) const {
  return Count(name) > 0;
}

bool CurrencyPurse::Spend(const std::string& name, int64_t count) {
  if (count <= 0) {
    return false;
  }
  for (std::vector<CurrencyAmount>::iterator it = entries_.begin();
       it != entries_.end(); ++it) {
    if (it->name() != name) {
      continue;
    }
    if (it->count() < count) {
      return false;
    }
    it->add_count(-count);
    if (it->count() == 0) {
      entries_.erase(it);
    }
    Sort();
    return true;
  }
  return false;
}

void CurrencyPurse::RestoreFrom(
    const google::protobuf::Map<std::string, int64_t>& saved,
    const std::map<std::string, const ItemPrototype*>& by_name) {
  entries_.clear();
  for (const std::pair<const std::string, int64_t>& held : saved) {
    std::map<std::string, const ItemPrototype*>::const_iterator proto =
        by_name.find(held.first);
    if (proto == by_name.end() || held.second <= 0) {
      continue;
    }
    entries_.push_back(CurrencyAmount(*proto->second, held.second));
  }
  Sort();
}

google::protobuf::Map<std::string, int64_t> CurrencyPurse::ToProto() const {
  google::protobuf::Map<std::string, int64_t> saved;
  for (const CurrencyAmount& entry : entries_) {
    saved[entry.name()] = entry.count();
  }
  return saved;
}

void CurrencyPurse::Sort() {
  std::sort(entries_.begin(), entries_.end(),
            [](const CurrencyAmount& a, const CurrencyAmount& b) {
              auto key = [](const CurrencyAmount& entry) {
                const ItemPrototype& proto = entry.prototype();
                // A token is sorted by what it buys, not its balance: a shelf
                // the player can't use yet shouldn't come before ones they can.
                // A shard can only be sorted by its count.
                bool token = proto.kind() == ITEM_KIND_TOKEN;
                return std::make_tuple(
                    KindRank(proto.kind()), token ? -proto.currency_level() : 0,
                    token ? TokenSlotRank(proto.currency_slot()) : 0,
                    -entry.count(), entry.name());
              };
              return key(a) < key(b);
            });
}

std::vector<int> CurrenciesOf(const CurrencyPurse& purse, ItemKind kind) {
  std::vector<int> rows;
  const std::vector<CurrencyAmount>& entries = purse.entries();
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (entries[i].prototype().kind() == kind) {
      rows.push_back(i);
    }
  }
  return rows;
}

}  // namespace ms
