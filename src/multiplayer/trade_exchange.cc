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

// The Etc tab as a list of stacks, modified separately from the character. It
// mirrors SpendItem and AddItem (drain in order and drop empty stacks, top up
// open stacks before starting new ones), because the question is what those two
// would do, and the character can't be asked without actually doing it.
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

// Whether `count` of `proto` all fit, adding what does.
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
  // Spell traces aren't counted: a currency is a balance, not a row, so any
  // number of them fits.
  Stacks stacks = character.stackables();
  for (const TradeStack& stack : given.stacks()) {
    TakeFrom(stacks, stack.name(), stack.count());
  }
  for (const TradeStack& stack : received.stacks()) {
    const ItemPrototype* proto = FindItemByName(items, stack.name());
    if (proto == nullptr || !PutIn(stacks, *proto, stack.count())) {
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
  // Remove before adding, and from the back: every row before the removed one
  // keeps its position, so the list of row indices stays valid during the loop.
  std::vector<int> rows = given_equips;
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (int row : rows) {
    character.TakeEquip(row);
  }
  for (const TradeStack& stack : given.stacks()) {
    character.SpendItem(stack.name(), stack.count());
  }
  character.SpendItem(kSpellTraceName, given.spell_traces());
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
      character.AddItem(*proto, stack.count());
    }
  }
  const ItemPrototype* trace = FindItemByName(items, kSpellTraceName);
  if (trace != nullptr && received.spell_traces() > 0) {
    character.AddItem(*trace, static_cast<int>(received.spell_traces()));
  }
}

}  // namespace ms
