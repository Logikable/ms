#include "analysis/gear_plan.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "analysis/sim_gear.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/spell_trace_cost.h"
#include "src/item/star_force_cost.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// The Spell Trace's own catalog entry, which is where its shop price is.
const ItemPrototype* TraceItem(const GameState& state) {
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find("spell_trace");
  return it == state.items.end() ? nullptr : &it->second;
}

// Everything worn but the weapon, which is spent on first and on its own.
std::vector<EquipSlot> OtherSlots(const GameState& state) {
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    if (entry.first != EQUIP_SLOT_PRIMARY_WEAPON) {
      slots.push_back(entry.first);
    }
  }
  return slots;
}

const EquipInstance* Worn(const GameState& state, EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      state.character.equipped().find(slot);
  return it == state.character.equipped().end() ? nullptr : &it->second;
}

}  // namespace

const Scroll* GearShopper::ScrollFor(GameState& state, EquipSlot slot) {
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr) {
    return nullptr;
  }
  // By value: the measurement below puts the character back together from a
  // proto, and every EquipInstance in the map goes with it.
  std::string name = item->prototype().name();
  std::map<std::string, const Scroll*>::const_iterator held =
      chosen_.find(name);
  if (held != chosen_.end()) {
    return held->second;
  }
  // Measured for every worn slot at once: the try-on costs a swing per
  // candidate either way, and the character has to be put back afterwards.
  std::map<EquipSlot, const Scroll*> picked =
      ChooseScrolls(state, plan_.scroll_rate);
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    std::map<EquipSlot, const Scroll*>::const_iterator found =
        picked.find(entry.first);
    chosen_[entry.second.prototype().name()] =
        found == picked.end() ? nullptr : found->second;
  }
  return chosen_[name];
}

bool GearShopper::FillSlots(GameState& state, EquipSlot slot,
                            GearSpend& spend) {
  const ItemPrototype* trace = TraceItem(state);
  if (trace == nullptr) {
    return true;
  }
  while (true) {
    const EquipInstance* item = Worn(state, slot);
    if (item == nullptr || item->equip_state().remaining_upgrade_slots() <= 0) {
      return true;
    }
    // Read out before the measurement, for the reason ScrollFor copies a name.
    int required_level = item->prototype().required_level();
    const Scroll* scroll = ScrollFor(state, slot);
    if (scroll == nullptr) {
      return true;  // nothing this item takes is worth wearing
    }
    int traces = TraceCost(*scroll, required_level);
    int64_t cost = static_cast<int64_t>(traces) * trace->shop_price();
    if (cost > state.character.meso()) {
      return false;
    }
    if (!state.character.Buy(*trace, traces) ||
        !state.character.ConsumeStackable(ITEM_CATEGORY_ETC, kSpellTraceName,
                                          traces)) {
      return true;  // the bag refused them, which is not the purse's fault
    }
    state.character.ScrollEquipped(slot, *scroll);
    spend.meso += cost;
    ++spend.slots_filled;
  }
}

bool GearShopper::AddStars(GameState& state, EquipSlot slot, GearSpend& spend) {
  while (true) {
    const EquipInstance* item = Worn(state, slot);
    if (item == nullptr || !item->CanStarForce() ||
        item->stars() >= plan_.star_target) {
      return true;
    }
    int64_t cost =
        StarForceCost(item->prototype().required_level(), item->stars());
    if (cost <= 0 || cost > state.character.meso()) {
      return cost <= 0;  // a price nothing is charged is not a purse problem
    }
    int before = item->stars();
    StarForceOutcome outcome = state.character.StarForceEquipped(slot);
    if (outcome == kStarForceNoMeso) {
      return false;
    }
    spend.meso += cost;
    const EquipInstance* after = Worn(state, slot);
    if (after != nullptr && after->stars() > before) {
      ++spend.stars_gained;
    }
  }
}

GearSpend GearShopper::Spend(GameState& state) {
  GearSpend spend;
  if (!FillSlots(state, EQUIP_SLOT_PRIMARY_WEAPON, spend) ||
      !AddStars(state, EQUIP_SLOT_PRIMARY_WEAPON, spend)) {
    return spend;
  }
  std::vector<EquipSlot> others = OtherSlots(state);
  for (EquipSlot slot : others) {
    if (!FillSlots(state, slot, spend)) {
      return spend;
    }
  }
  for (EquipSlot slot : others) {
    if (!AddStars(state, slot, spend)) {
      return spend;
    }
  }
  return spend;
}

}  // namespace ms
