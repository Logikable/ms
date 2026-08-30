#include "analysis/gear_plan.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "analysis/sim_gear.h"
#include "analysis/star_force_curve.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/damage.h"
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

const EquipInstance* Worn(const GameState& state, EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      state.character.equipped().find(slot);
  return it == state.character.equipped().end() ? nullptr : &it->second;
}

// What the character wears and what their passives grant, summed. The
// expensive half of scoring a candidate, so it is taken once a round and every
// candidate is added to a copy of it.
EquipStats WornAndGranted(const GameState& state, DerivedStats& derived) {
  derived = DerivedStatsFor(state.character, state.skills);
  return TotalEquipStats(state.character, derived);
}

// The character's combat power with `stats` in place of what they wear.
//
// Combat power rather than a played swing, because ranking one star against
// another is a question about the stat block and nothing else: the closed form
// answers it for no simulation at all, where a swing would cost one per
// candidate per round. What it leaves out is the Max HP a star pays, which
// buys survival rather than damage -- a shopper valuing both would need a rate
// of exchange between them that nothing in the game states.
int PowerWith(const GameState& state, const DerivedStats& derived,
              const EquipStats& stats) {
  const Character& proto = state.character.proto();
  // Ranked against bosses, which is what the gear is bought for -- see
  // weapon_sim for the reason every sim asks the same way.
  return CombatPower(
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      stats, state.character.weapon_type(),
                      /*attack_skill=*/nullptr, /*attack_level=*/0,
                      PassiveOffenseFor(derived)),
      /*vs_boss=*/true);
}

EquipStats Plus(const EquipStats& a, const EquipStats& b) {
  const EquipStats sources[] = {a, b};
  return SumEquipStats(sources);
}

// What one more star adds, which is the gap between what the stars are worth
// at each level rather than what the next one is worth on its own -- the
// scaled ones compound, so the eleventh star is not the first star again.
EquipStats Minus(const EquipStats& a, const EquipStats& b) {
  EquipStats d;
  d.set_str(a.str() - b.str());
  d.set_dex(a.dex() - b.dex());
  d.set_int_(a.int_() - b.int_());
  d.set_luk(a.luk() - b.luk());
  d.set_attack(a.attack() - b.attack());
  d.set_magic_attack(a.magic_attack() - b.magic_attack());
  d.set_max_hp(a.max_hp() - b.max_hp());
  d.set_max_mp(a.max_mp() - b.max_mp());
  d.set_def(a.def() - b.def());
  return d;
}

// Whether an attempt from `stars` can destroy the item. The shopper will not
// take one: nothing here recovers a trace, so a destroyed piece is simply
// gone, and pricing a loss the sim cannot undo would be fiction. GMS puts the
// first such attempt at 15 stars, so that is where this stops -- read off the
// rate table rather than written down, so it moves if the table does.
bool CanDestroy(int stars) {
  return EquipInstance::RateAt(stars).destroy > 0;
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

std::vector<GearShopper::Candidate> GearShopper::Offers(GameState& state) {
  std::vector<Candidate> offers;
  const ItemPrototype* trace = TraceItem(state);
  DerivedStats derived;
  EquipStats worn = WornAndGranted(state, derived);
  int power = PowerWith(state, derived, worn);
  // The hammer's own gate. The shopper buys what a player at this level could,
  // so below it there is nothing to offer.
  bool hammers_open =
      state.character.proto().level() >= UnlockLevel(Feature::kHammer);
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    slots.push_back(entry.first);
  }
  for (EquipSlot slot : slots) {
    const EquipInstance* item = Worn(state, slot);
    if (item == nullptr) {
      continue;
    }
    // Read out before ScrollFor, which measures: putting a scroll on to try it
    // rebuilds the character, and every EquipInstance in the map goes with it.
    int level = item->prototype().required_level();
    int stars = item->stars();
    int open_slots = item->equip_state().remaining_upgrade_slots();
    bool can_star = item->CanStarForce();
    bool can_hammer = item->CanHammer() && hammers_open;
    if ((open_slots > 0 || can_hammer) && trace != nullptr) {
      const Scroll* scroll = ScrollFor(state, slot);
      if (scroll != nullptr) {
        int64_t scroll_cost = static_cast<int64_t>(TraceCost(*scroll, level)) *
                              trace->shop_price();
        // What the slot is worth is what it lands times how often it lands: a
        // scroll that fails has still spent the slot, and on a piece nothing
        // sells that slot does not come back.
        int scroll_gain =
            (PowerWith(state, derived, Plus(worn, scroll->stats())) - power) *
            plan_.scroll_rate / 100;
        if (open_slots > 0) {
          Candidate offer;
          offer.slot = slot;
          offer.scroll = scroll;
          offer.cost = scroll_cost;
          offer.gain = scroll_gain;
          offers.push_back(offer);
        } else if (can_hammer) {
          // Only where there is no open slot already: a hammer opens one more
          // of what the item still has, so it is the wrong thing to buy while
          // the last one is unspent. Priced with the scroll that fills it,
          // since a hammer alone lands nothing.
          Candidate offer;
          offer.slot = slot;
          offer.hammer = true;
          offer.scroll = scroll;
          offer.cost = kGoldenHammerCost + scroll_cost;
          offer.gain = scroll_gain;
          offers.push_back(offer);
        }
      }
    }
    // A star waits for the slots: GMS refuses one while an upgrade slot is
    // still open, so an unscrolled item has no star to offer.
    if (!can_star || stars >= plan_.star_ceiling || CanDestroy(stars)) {
      continue;
    }
    StarForceRun run = StarForceRunTo(level, stars, stars + 1);
    if (run.meso <= 0.0) {
      continue;
    }
    // Fetched again: ScrollFor above measures, and measuring puts the
    // character back together from a proto -- every EquipInstance in the map
    // goes with it, this one included.
    item = Worn(state, slot);
    if (item == nullptr) {
      continue;
    }
    Candidate offer;
    offer.slot = slot;
    offer.star = true;
    // The expected price of GETTING the star, not of one attempt at it: every
    // click is paid for whether it lands or not, and that gap is most of what
    // makes a late star the wrong thing to buy.
    offer.cost = static_cast<int64_t>(run.meso);
    // Against what is already worn, which already holds the stars the item has:
    // only the gap between them is on offer.
    EquipStats added = Minus(item->StarForceStatGains(stars + 1),
                             item->StarForceStatGains(stars));
    offer.gain = PowerWith(state, derived, Plus(worn, added)) - power;
    offers.push_back(offer);
  }
  return offers;
}

bool GearShopper::BuyBest(GameState& state, GearSpend& spend) {
  const Candidate* best = nullptr;
  std::vector<Candidate> offers = Offers(state);
  for (const Candidate& offer : offers) {
    if (offer.gain <= 0 || offer.cost <= 0 ||
        offer.cost > state.character.meso()) {
      continue;
    }
    // Cross-multiplied rather than divided, so two candidates a rounding apart
    // are still ordered by what they are worth.
    if (best == nullptr || offer.gain * best->cost > best->gain * offer.cost) {
      best = &offer;
    }
  }
  if (best == nullptr) {
    return false;
  }
  if (best->hammer) {
    EquipSlot slot = best->slot;
    if (!state.character.HammerEquipped(slot)) {
      return false;  // the purse or the item refused it
    }
    spend.meso += kGoldenHammerCost;
    ++spend.hammers_driven;
    return true;
  }
  if (!best->star) {
    const ItemPrototype* trace = TraceItem(state);
    const EquipInstance* item = Worn(state, best->slot);
    if (trace == nullptr || item == nullptr) {
      return false;
    }
    int traces = TraceCost(*best->scroll, item->prototype().required_level());
    if (!state.character.Buy(*trace, traces) ||
        !state.character.ConsumeStackable(ITEM_CATEGORY_ETC, kSpellTraceName,
                                          traces)) {
      return false;  // the bag refused them, which is not the purse's fault
    }
    state.character.ScrollEquipped(best->slot, *best->scroll);
    spend.meso += static_cast<int64_t>(traces) * trace->shop_price();
    ++spend.slots_filled;
    return true;
  }
  // Attempts until it lands or the purse runs dry. The price above was what
  // the star is expected to take; this is what it actually took, and one run
  // is not the average.
  EquipSlot slot = best->slot;
  const EquipInstance* item = Worn(state, slot);
  int before = item == nullptr ? 0 : item->stars();
  while (true) {
    item = Worn(state, slot);
    if (item == nullptr || item->stars() > before) {
      break;
    }
    int64_t attempt =
        StarForceCost(item->prototype().required_level(), item->stars());
    if (attempt <= 0 || attempt > state.character.meso()) {
      break;
    }
    if (state.character.StarForceEquipped(slot) == kStarForceNoMeso) {
      break;
    }
    spend.meso += attempt;
  }
  item = Worn(state, slot);
  if (item != nullptr && item->stars() > before) {
    ++spend.stars_gained;
  }
  return true;
}

GearSpend GearShopper::Spend(GameState& state) {
  GearSpend spend;
  while (BuyBest(state, spend)) {
  }
  return spend;
}

}  // namespace ms
