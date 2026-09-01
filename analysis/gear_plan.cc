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

// Whether any attempt this piece will ever take can destroy it. A level whose
// star ceiling sits at or below the first destroying star makes the piece safe
// however far it is taken -- and a spare of it worth nothing but the sale.
bool CanEverDestroy(const EquipPrototype& proto) {
  if (!Supports(proto, UPGRADE_STAR_FORCE)) {
    return false;
  }
  int ceiling = EquipTabItem::MaxStarsForLevel(proto.required_level());
  for (int star = 0; star < ceiling; ++star) {
    if (CanDestroy(star)) {
      return true;
    }
  }
  return false;
}

// What one more copy of `proto` costs. The shop's price where it stocks the
// piece; otherwise the sale a kept spare forgoes, which is what holding one
// against a boom actually costs.
int64_t SpareCost(const EquipPrototype& proto) {
  return proto.shop_price() > 0 ? proto.shop_price() : SellPrice(proto);
}

// Copies of `name` sitting in the bag. Traces are not copies: a trace is what
// a boom leaves behind, not what puts the piece back.
int SparesInBag(const CharacterInstance& character, const std::string& name) {
  int spares = 0;
  const InventoryInstance& bag = character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    const EquipInstance* item = bag.equip_instance(i);
    if (item != nullptr && item->prototype().name() == name) {
      ++spares;
    }
  }
  return spares;
}

// Whether a boom on this piece could be put right. The shop stocking it is
// cover enough -- a copy is one purchase away -- and otherwise it takes a
// spare already in the bag.
bool CanCoverBoom(const CharacterInstance& character,
                  const EquipPrototype& proto) {
  return proto.shop_price() > 0 || SparesInBag(character, proto.name()) > 0;
}

// How many spares of a piece are worth keeping: the booms expected on the
// longest star run the purse could pay for as it stands.
//
// Income decides it, not a constant. A piece that drops faster than the meso
// to boom it with is a piece to sell, and the same piece is worth hoarding
// once the purse can afford the run that destroys it. One is the floor while
// it can boom at all, since the shopper will not walk into a destroying
// attempt with nothing to put back.
int SparesWorthKeeping(const GameState& state, const EquipPrototype& proto,
                       int stars) {
  if (!CanEverDestroy(proto) || proto.shop_price() > 0) {
    return 0;  // safe however far it goes, or a copy is a purchase away
  }
  int level = proto.required_level();
  // A run only gets dearer the further it goes, so the furthest one the purse
  // covers is bisected for rather than walked to -- the walk is a linear
  // system a star, and this runs at every look.
  int lo = stars;
  int hi = EquipTabItem::MaxStarsForLevel(level);
  double booms = 0.0;
  while (lo < hi) {
    int mid = lo + (hi - lo + 1) / 2;
    StarForceRun run = StarForceRunTo(level, stars, mid);
    if (run.meso > static_cast<double>(state.character.meso())) {
      hi = mid - 1;
      continue;
    }
    booms = run.booms;
    lo = mid;
  }
  return std::max(1, static_cast<int>(std::ceil(booms)));
}

// The stars on the worn copy of `name`, or -1 where none is worn.
int WornStars(const CharacterInstance& character, const std::string& name) {
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       character.equipped()) {
    if (entry.second.prototype().name() == name) {
      return entry.second.stars();
    }
  }
  return -1;
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

// The upgrade slot `slot`'s item could fill next, and the hammer that would
// open one where none is left. Nothing where the item has neither, where the
// character cannot buy a trace, or where no scroll suits the piece.
//
// The hammer is offered only where there is NO open slot already: it opens one
// more of what the item still has, so it is the wrong thing to buy while the
// last one is unspent. Priced with the scroll that fills it, since a hammer
// alone lands nothing.
std::optional<GearShopper::Candidate> GearShopper::ScrollOffer(
    GameState& state, const Basis& basis, EquipSlot slot, int level,
    int open_slots, bool can_hammer) {
  if (basis.trace == nullptr || (open_slots <= 0 && !can_hammer)) {
    return std::nullopt;
  }
  const Scroll* scroll = ScrollFor(state, slot);
  if (scroll == nullptr) {
    return std::nullopt;
  }
  Candidate offer;
  offer.slot = slot;
  offer.scroll = scroll;
  offer.cost = static_cast<int64_t>(TraceCost(*scroll, level)) *
               basis.trace->shop_price();
  // What the slot is worth is what it lands times how often it lands: a scroll
  // that fails has still spent the slot, and on a piece nothing sells that slot
  // does not come back.
  offer.gain =
      (PowerWith(state, basis.derived, Plus(basis.worn, scroll->stats())) -
       basis.power) *
      plan_.scroll_rate / 100;
  if (open_slots <= 0) {
    offer.hammer = true;
    offer.cost += kGoldenHammerCost;
  }
  return offer;
}

// The next star `slot`'s item could take. Nothing where it takes none, where
// it has reached the plan's ceiling, where the next click could destroy it, or
// where the item is not scrolled out -- GMS refuses a star while an upgrade
// slot is still open.
std::optional<GearShopper::Candidate> GearShopper::StarOffer(GameState& state,
                                                             const Basis& basis,
                                                             EquipSlot slot,
                                                             int level,
                                                             int stars) {
  if (stars >= plan_.star_ceiling) {
    return std::nullopt;
  }
  StarForceRun run = StarForceRunTo(level, stars, stars + 1);
  if (run.meso <= 0.0) {
    return std::nullopt;
  }
  // Fetched here rather than passed in: ScrollOffer above measures, and
  // measuring puts the character back together from a proto -- every
  // EquipInstance in the map goes with it, this one included.
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr) {
    return std::nullopt;
  }
  // A destroying attempt is only walked into with something to put back. The
  // trace a boom leaves needs a spare body, and where the shop stocks none and
  // the bag holds none, the piece is simply gone -- which is not a price, it
  // is a loss the sim cannot undo.
  if (run.booms > 0.0 && !CanCoverBoom(state.character, item->prototype())) {
    return std::nullopt;
  }
  Candidate offer;
  offer.slot = slot;
  offer.star = true;
  // The expected price of GETTING the star, not of one attempt at it: every
  // click is paid for whether it lands or not, and that gap is most of what
  // makes a late star the wrong thing to buy. Past fifteen the copies the run
  // eats are the larger half of it.
  offer.cost =
      static_cast<int64_t>(run.meso + run.booms * SpareCost(item->prototype()));
  // Against what is already worn, which already holds the stars the item has:
  // only the gap between them is on offer.
  EquipStats added = Minus(item->StarForceStatGains(stars + 1),
                           item->StarForceStatGains(stars));
  offer.gain =
      PowerWith(state, basis.derived, Plus(basis.worn, added)) - basis.power;
  return offer;
}

std::vector<GearShopper::Candidate> GearShopper::Offers(GameState& state) {
  Basis basis;
  basis.trace = TraceItem(state);
  basis.worn = WornAndGranted(state, basis.derived);
  basis.power = PowerWith(state, basis.derived, basis.worn);
  // The hammer's own gate. The shopper buys what a player at this level could,
  // so below it there is nothing to offer.
  bool hammers_open =
      state.character.proto().level() >= UnlockLevel(Feature::kHammer);
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    slots.push_back(entry.first);
  }
  std::vector<Candidate> offers;
  for (EquipSlot slot : slots) {
    const EquipInstance* item = Worn(state, slot);
    if (item == nullptr) {
      continue;
    }
    // Read out before either offer, which measure: putting a scroll on to try
    // it rebuilds the character, and every EquipInstance in the map goes too.
    int level = item->prototype().required_level();
    int stars = item->stars();
    int open_slots = item->equip_state().remaining_upgrade_slots();
    bool can_star = item->CanStarForce();
    bool can_hammer = item->CanHammer() && hammers_open;
    std::optional<Candidate> scroll =
        ScrollOffer(state, basis, slot, level, open_slots, can_hammer);
    if (scroll.has_value()) {
      offers.push_back(*scroll);
    }
    if (!can_star) {
      continue;
    }
    std::optional<Candidate> star = StarOffer(state, basis, slot, level, stars);
    if (star.has_value()) {
      offers.push_back(*star);
    }
  }
  return offers;
}

bool GearShopper::BuyBest(GameState& state, GearSpend& spend) {
  std::vector<Candidate> offers = Offers(state);
  // Cross-multiplied rather than divided, so two candidates a rounding apart
  // are still ordered by what they are worth.
  std::sort(offers.begin(), offers.end(),
            [](const Candidate& a, const Candidate& b) {
              return static_cast<int64_t>(a.gain) * b.cost >
                     static_cast<int64_t>(b.gain) * a.cost;
            });
  // Down the list wherever one is refused. A bag that cannot take the traces
  // for one piece is no reason to stop buying for every other piece -- and
  // stopping is what this did, so a full Etc tab quietly ended the shopping
  // for the rest of the run.
  for (const Candidate& offer : offers) {
    if (offer.gain <= 0 || offer.cost <= 0 ||
        offer.cost > state.character.meso()) {
      continue;
    }
    if (BuyOffer(state, offer, spend)) {
      return true;
    }
  }
  return false;
}

// Pays for one offer and puts it on. False for one the bag or the purse
// refused, which is the caller's cue to try the next.
bool GearShopper::BuyOffer(GameState& state, const Candidate& candidate,
                           GearSpend& spend) {
  const Candidate* best = &candidate;
  if (best->hammer) {
    EquipSlot slot = best->slot;
    if (!state.character.HammerEquipped(slot)) {
      return false;  // the purse or the item refused it
    }
    spend.hammers += kGoldenHammerCost;
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
    spend.scrolls += static_cast<int64_t>(traces) * trace->shop_price();
    ++spend.slots_filled;
    return true;
  }
  // Attempts until it lands or the purse runs dry. The price above was what
  // the star is expected to take; this is what it actually took, and one run
  // is not the average.
  EquipSlot slot = best->slot;
  const EquipInstance* item = Worn(state, slot);
  int before = item == nullptr ? 0 : item->stars();
  // Copied out: a boom takes the EquipInstance with it, and the recovery needs
  // to know what was lost.
  EquipPrototype proto = item == nullptr ? EquipPrototype() : item->prototype();
  while (true) {
    item = Worn(state, slot);
    if (item == nullptr) {
      // The last attempt destroyed it. Trace plus spare body makes the piece
      // again, several stars down and with its scrolls intact, and the run
      // carries on from there -- which is the loop the price above solved.
      if (!RecoverBoom(state, slot, proto, spend)) {
        break;
      }
      continue;
    }
    if (item->stars() > before) {
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
    spend.stars += attempt;
  }
  item = Worn(state, slot);
  if (item != nullptr && item->stars() > before) {
    ++spend.stars_gained;
  }
  return true;
}

bool GearShopper::RecoverBoom(GameState& state, EquipSlot slot,
                              const EquipPrototype& proto, GearSpend& spend) {
  int trace_index = -1;
  int spare_index = -1;
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size() && (trace_index < 0 || spare_index < 0); ++i) {
    if (bag[i].prototype().name() != proto.name()) {
      continue;
    }
    // A trace answers null to equip_instance, which is how the two are told
    // apart: one is what was lost, the other is what puts it back.
    if (bag.equip_instance(i) == nullptr) {
      trace_index = trace_index < 0 ? i : trace_index;
    } else {
      spare_index = spare_index < 0 ? i : spare_index;
    }
  }
  if (trace_index < 0) {
    return false;
  }
  if (spare_index < 0) {
    if (proto.shop_price() <= 0 || !state.character.Buy(proto, 1)) {
      return false;
    }
    spend.replacements += proto.shop_price();
    spare_index = state.character.inventory().size() - 1;
  }
  state.character.RecoverTrace(trace_index, spare_index);
  ++spend.booms;
  // RecoverTrace appends the piece it made, which is what goes back on.
  return state.character.Equip(state.character.inventory().size() - 1);
}

void GearShopper::SellSpares(GameState& state, GearSpend& spend) {
  // Worked out per piece rather than per copy: what a spare is worth depends
  // on the piece and the purse, not on how many of it the bag happens to hold.
  std::map<std::string, int> allowance;
  int i = 0;
  while (i < state.character.inventory().size()) {
    const EquipInstance* item = state.character.inventory().equip_instance(i);
    if (item == nullptr) {
      ++i;  // a trace, which is the other half of a recovery
      continue;
    }
    const EquipPrototype& proto = item->prototype();
    std::map<std::string, int>::iterator kept = allowance.find(proto.name());
    if (kept == allowance.end()) {
      int worn = WornStars(state.character, proto.name());
      // A piece not worn keeps one copy -- it is gear, not a spare. A piece
      // worn keeps as many as a boom could use.
      kept =
          allowance
              .insert({proto.name(),
                       worn < 0 ? 1 : SparesWorthKeeping(state, proto, worn)})
              .first;
    }
    if (state.character.MeetsJob(proto) && kept->second > 0) {
      --kept->second;
      ++i;
      continue;
    }
    spend.sold += state.character.SellEquip(i);
  }
}

GearSpend GearShopper::Spend(GameState& state) {
  GearSpend spend;
  SellSpares(state, spend);
  while (BuyBest(state, spend)) {
  }
  life_.Add(spend);
  return spend;
}

}  // namespace ms
