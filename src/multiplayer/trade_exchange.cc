#include "src/multiplayer/trade_exchange.h"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"

namespace ms {
namespace {

// The Etc tab as a list of stacks, worked on away from the character. It
// MIRRORS ConsumeStackable and AddStackable -- drain in order and drop what
// empties, top up open stacks before opening new ones -- because the question
// is what those two would do, and the character cannot be asked to do it
// without doing it.
using Stacks = std::vector<StackableItem>;

void TakeFrom(Stacks& stacks, const std::string& name, int count) {
  for (Stacks::iterator it = stacks.begin(); it != stacks.end() && count > 0;) {
    if (it->name() != name) {
      ++it;
      continue;
    }
    int taken = std::min(it->count(), count);
    it->add_count(-taken);
    count -= taken;
    it = it->count() == 0 ? stacks.erase(it) : it + 1;
  }
}

// Whether `count` of `proto` all fit, putting in what does.
bool PutIn(Stacks& stacks, const ItemPrototype& proto, int count) {
  for (StackableItem& stack : stacks) {
    if (count <= 0) {
      break;
    }
    if (stack.name() != proto.name()) {
      continue;
    }
    int room = std::min(stack.max_stack() - stack.count(), count);
    if (room <= 0) {
      continue;
    }
    stack.add_count(room);
    count -= room;
  }
  while (count > 0) {
    if (static_cast<int>(stacks.size()) >= kTabCapacity) {
      return false;
    }
    StackableItem stack(proto, 0);
    stack.add_count(std::min(stack.max_stack(), count));
    count -= stack.count();
    stacks.push_back(std::move(stack));
  }
  return true;
}

}  // namespace

bool HasRoomForTrade(const CharacterInstance& character,
                     const std::map<std::string, ItemPrototype>& items,
                     const TradeOffer& given, const TradeOffer& received) {
  if (received.equips_size() >
      character.inventory().room() + given.equips_size()) {
    return false;
  }
  Stacks stacks = character.stackables();
  for (const TradeStack& stack : given.stacks()) {
    TakeFrom(stacks, stack.name(), stack.count());
  }
  // The spell trace is a stack like any other, however it is offered.
  const ItemPrototype* trace = FindItemByName(items, kSpellTraceName);
  if (given.spell_traces() > 0) {
    TakeFrom(stacks, kSpellTraceName, given.spell_traces());
  }
  for (const TradeStack& stack : received.stacks()) {
    const ItemPrototype* proto = FindItemByName(items, stack.name());
    if (proto == nullptr || !PutIn(stacks, *proto, stack.count())) {
      return false;
    }
  }
  if (received.spell_traces() > 0) {
    if (trace == nullptr ||
        !PutIn(stacks, *trace, static_cast<int>(received.spell_traces()))) {
      return false;
    }
  }
  return true;
}

void ApplyTrade(CharacterInstance& character,
                const std::map<std::string, EquipPrototype>& equips,
                const std::map<std::string, ItemPrototype>& items,
                const std::vector<int>& given_equips, const TradeOffer& given,
                const TradeOffer& received) {
  // Out before in, and from the back: every row before the one taken keeps
  // its place, so a list of rows stays true while it is being worked.
  std::vector<int> rows = given_equips;
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (int row : rows) {
    character.TakeEquip(row);
  }
  for (const TradeStack& stack : given.stacks()) {
    character.ConsumeStackable(stack.name(), stack.count());
  }
  character.ConsumeStackable(kSpellTraceName,
                             static_cast<int>(given.spell_traces()));
  character.SpendMeso(given.meso());

  character.AddMeso(received.meso());
  for (const Equip& equip : received.equips()) {
    const EquipPrototype* proto = FindEquipByName(equips, equip.equip_name());
    if (proto == nullptr) {
      continue;
    }
    character.PickUp(EquipItemFromState(*proto, equip));
  }
  for (const TradeStack& stack : received.stacks()) {
    const ItemPrototype* proto = FindItemByName(items, stack.name());
    if (proto != nullptr) {
      character.AddStackable(*proto, stack.count());
    }
  }
  const ItemPrototype* trace = FindItemByName(items, kSpellTraceName);
  if (trace != nullptr && received.spell_traces() > 0) {
    character.AddStackable(*trace, static_cast<int>(received.spell_traces()));
  }
}

}  // namespace ms
