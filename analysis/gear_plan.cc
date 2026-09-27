#include "analysis/gear_plan.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/cube_plan.h"
#include "analysis/sim_gear.h"
#include "analysis/star_force_curve.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/character/symbol.h"
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

// The Spell Trace's catalog entry, which holds its shop price.
const ItemPrototype* TraceItem(const GameState& state) {
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find("spell_trace");
  return it == state.items.end() ? nullptr : &it->second;
}

// What boss fights wear in `slot`, which every upgrade but a farm cube is for.
const EquipInstance* Worn(const GameState& state, EquipSlot slot) {
  return state.character.WornAt(kBossGear, slot);
}

// The preset that owns what boss fights wear in `slot`: the Boss preset's own
// piece, or the farm piece it inherits. A star run must know, since a boom on
// the Boss preset's own piece leaves the farm piece showing through.
StatPreset OwnerOf(const CharacterInstance& character, EquipSlot slot) {
  return character.InheritsSlot(kBossGear, slot) ? kFarmGear : kBossGear;
}

// The piece `owner` holds in `slot` as its own, or null.
const EquipInstance* Owned(const CharacterInstance& character, StatPreset owner,
                           EquipSlot slot) {
  const std::map<EquipSlot, EquipInstance>& own = character.own_gear(owner);
  std::map<EquipSlot, EquipInstance>::const_iterator it = own.find(slot);
  return it == own.end() ? nullptr : &it->second;
}

// Stats the character wears plus what their passives grant. This is the
// expensive half of scoring a candidate, so it's computed once a round and each
// candidate is added to a copy.
EquipStats WornAndGranted(const GameState& state, DerivedStats& derived) {
  derived = DerivedStatsFor(state.character, state.skills, {}, {},
                            Activity::kBossing);
  return TotalEquipStats(state.character, derived);
}

// Damage against the yardstick with `stats` in place of what the character
// wears, by the closed form. It leaves out the Max HP a star gives, which has
// no exchange rate against damage.
double PowerWith(const GameState& state, const Yardstick& yard,
                 const DerivedStats& derived, const EquipStats& stats) {
  return WorthOf(state, yard, stats, PassiveOffenseFor(derived));
}

EquipStats Plus(const EquipStats& a, const EquipStats& b) {
  const EquipStats sources[] = {a, b};
  return SumEquipStats(sources);
}

// Difference of two stat blocks. Used for what one more star adds: the gap
// between the star gains at each level, not the next star alone, since scaled
// stars compound.
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

// Whether an attempt from `stars` can destroy the item. GMS starts at 15 stars;
// this reads the rate table so it follows any change there.
bool CanDestroy(int stars) {
  return EquipInstance::RateAt(stars).destroy > 0;
}

// Whether any attempt this piece could ever take can destroy it. A piece whose
// star cap is at or below the first destroying star is safe however far it
// goes, and a spare of it is worth only its sale price.
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

// Cost of one more copy of `proto`: the shop price if the shop stocks it,
// otherwise the sale price a kept spare gives up.
int64_t SpareCost(const EquipPrototype& proto) {
  return proto.shop_price() > 0 ? proto.shop_price() : SellPrice(proto);
}

// Copies of `name` in the bag. Traces don't count: a trace is what a boom
// leaves, not what restores the piece.
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

// Whether a boom on this piece could be recovered from. The shop stocking it is
// enough, since a copy is one purchase away; otherwise it needs a spare in the
// bag.
bool CanCoverBoom(const CharacterInstance& character,
                  const EquipPrototype& proto) {
  return proto.shop_price() > 0 || SparesInBag(character, proto.name()) > 0;
}

// Spares worth keeping of a piece: the expected booms on the longest star run
// the character could afford, and at least one while it can boom at all.
int SparesWorthKeeping(const GameState& state, const EquipPrototype& proto,
                       int stars) {
  if (!CanEverDestroy(proto) || proto.shop_price() > 0) {
    return 0;  // safe however far it goes, or a copy is one purchase away
  }
  int level = proto.required_level();
  // A run only gets more expensive the further it goes, so bisect for the
  // furthest affordable one instead of walking to it. Each step solves a linear
  // system, and this runs at every look.
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

// Stars on the worn copy of `name`, or -1 if none is worn.
int WornStars(const CharacterInstance& character, const std::string& name) {
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       character.equipped(kBossGear)) {
    if (entry.second->prototype().name() == name) {
      return entry.second->stars();
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
  // Copy the name: the measurement below rebuilds the character from a proto,
  // destroying every EquipInstance in the map.
  std::string name = item->prototype().name();
  std::map<std::string, const Scroll*>::const_iterator held =
      chosen_.find(name);
  if (held != chosen_.end()) {
    return held->second;
  }
  // Measure every worn slot at once, since trying on scrolls costs a measured
  // fight per candidate either way and the character must be restored
  // afterwards.
  std::map<EquipSlot, const Scroll*> picked =
      ChooseScrolls(state, plan_.scroll_rate);
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    std::map<EquipSlot, const Scroll*>::const_iterator found =
        picked.find(entry.first);
    chosen_[entry.second->prototype().name()] =
        found == picked.end() ? nullptr : found->second;
  }
  return chosen_[name];
}

// The upgrade slot `slot`'s item could fill next, or the hammer that opens one
// if none is left, priced together with the scroll that fills it.
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
  // A slot's value is its gain times its success rate. A failed scroll still
  // uses up the slot, and on a piece nothing sells that slot is gone.
  offer.gain = (PowerWith(state, basis.yard, basis.derived,
                          Plus(basis.worn, scroll->stats())) -
                basis.power) *
               plan_.scroll_rate / 100.0;
  if (open_slots <= 0) {
    offer.hammer = true;
    offer.cost += kGoldenHammerCost;
  }
  return offer;
}

// The next star `slot`'s item could take. Nothing if it takes no stars, has
// reached the plan's limit, or isn't fully scrolled, since GMS refuses a star
// while an upgrade slot is open.
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
  // Looked up here rather than passed in: ScrollOffer measures, and measuring
  // rebuilds the character from a proto, destroying every EquipInstance in the
  // map, this one included.
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr) {
    return std::nullopt;
  }
  // Only risk destruction with a way to recover. The trace a boom leaves needs
  // a spare copy, and if neither the shop nor the bag has one, the piece would
  // simply be lost, which the sim can't undo.
  if (run.booms > 0.0 && !CanCoverBoom(state.character, item->prototype())) {
    return std::nullopt;
  }
  Candidate offer;
  offer.slot = slot;
  offer.star = true;
  // The expected price of getting the star, not of one attempt. Past 15 stars
  // the copies consumed by booms are the larger part.
  offer.cost =
      static_cast<int64_t>(run.meso + run.booms * SpareCost(item->prototype()));
  // Worn stats already include the item's current stars, so only the gap is on
  // offer.
  EquipStats added = Minus(item->StarForceStatGains(stars + 1),
                           item->StarForceStatGains(stars));
  offer.gain =
      PowerWith(state, basis.yard, basis.derived, Plus(basis.worn, added)) -
      basis.power;
  return offer;
}

std::optional<GearShopper::Candidate> GearShopper::SymbolOffer(
    GameState& state, const Basis& basis, EquipSlot slot) {
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr || !IsSymbol(item->prototype())) {
    return std::nullopt;
  }
  // Meso alone can't level a symbol: the level also needs duplicates from
  // drops. So a slot short of them offers nothing, however much meso the
  // character has.
  const ms::Equip& worn = item->equip_state();
  if (!SymbolCanLevelUp(item->prototype(), worn)) {
    return std::nullopt;
  }
  int level = SymbolLevel(worn);
  Candidate offer;
  offer.slot = slot;
  offer.symbol = true;
  offer.cost = SymbolLevelUpCost(item->prototype(), level);
  if (offer.cost <= 0) {
    return std::nullopt;
  }
  // The level's value is the primary stat it adds. Its force is left
  // out, since that affects which maps the character can fight on rather than
  // the character itself, like ignored defence (see CubeBasis.yard).
  StatField primary = PrimaryStatField(state.character.proto().job());
  EquipStats added =
      Minus(SymbolStatsFor(item->prototype(), primary, level + 1),
            SymbolStatsFor(item->prototype(), primary, level));
  offer.gain =
      PowerWith(state, basis.yard, basis.derived, Plus(basis.worn, added)) -
      basis.power;
  return offer;
}

double GearShopper::Power(GameState& state) {
  DerivedStats derived;
  EquipStats worn = WornAndGranted(state, derived);
  return PowerWith(state, yard_.For(state), derived, worn);
}

std::vector<GearShopper::Candidate> GearShopper::Offers(GameState& state) {
  Basis basis;
  basis.trace = TraceItem(state);
  basis.worn = WornAndGranted(state, basis.derived);
  basis.yard = yard_.For(state);
  basis.power = PowerWith(state, basis.yard, basis.derived, basis.worn);
  // The hammer's unlock level. The shopper only buys what a player at this
  // level could.
  bool hammers_open =
      state.character.proto().level() >= UnlockLevel(Feature::kHammer);
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    slots.push_back(entry.first);
  }
  std::vector<Candidate> offers;
  for (EquipSlot slot : slots) {
    const EquipInstance* item = Worn(state, slot);
    if (item == nullptr) {
      continue;
    }
    std::optional<Candidate> symbol = SymbolOffer(state, basis, slot);
    if (symbol.has_value()) {
      offers.push_back(*symbol);
      continue;  // a symbol takes neither scrolls nor stars
    }
    // Read these before either offer, since both measure: trying on a scroll
    // rebuilds the character, destroying every EquipInstance in the map.
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
  // Cube offers come after the rest, which set what a meso is worth. An income
  // line pays in meso, and only that rate lets it rank against damage lines.
  // See CubeOffers.
  double best = 0.0;
  for (const Candidate& offer : offers) {
    if (offer.cost > 0) {
      best = std::max(best, static_cast<double>(offer.gain) / offer.cost);
    }
  }
  std::vector<Candidate> cubes = CubeOffers(state, best);
  offers.insert(offers.end(), cubes.begin(), cubes.end());
  return offers;
}

// Cubing has its own unlock level and its own pass, since every candidate is
// priced against one CubeBasis, which needs a rebuild.
std::vector<GearShopper::Candidate> GearShopper::CubeOffers(GameState& state,
                                                            double best) {
  std::vector<Candidate> offers;
  if (!plan_.cubes ||
      state.character.proto().level() < UnlockLevel(Feature::kPotential)) {
    return offers;
  }
  // The meso rate only sets an income line's rank. Whether it pays for itself
  // depends on its earnings over the horizon against the cube's cost, which
  // needs no rate. Stored so the accept decision uses the same value.
  income_.power_per_meso = best;
  CubeBasis basis = CubeBasisFor(state, yard_.For(state));
  const bool green =
      state.character.proto().level() >= UnlockLevel(Feature::kBonusPotential);
  // Every piece boss fights wear, then every farm piece they don't.
  std::vector<std::pair<StatPreset, EquipSlot>> pieces;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    pieces.push_back({kBossGear, entry.first});
  }
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kFarmGear)) {
    if (state.character.WornAt(kBossGear, entry.first) != entry.second) {
      pieces.push_back({kFarmGear, entry.first});
    }
  }
  for (const std::pair<StatPreset, EquipSlot>& piece : pieces) {
    const EquipInstance* item =
        state.character.WornAt(piece.first, piece.second);
    if (item == nullptr || !item->CanCube()) {
      continue;
    }
    for (const Cube& shelf : kCubes) {
      if (shelf.type == CubeType::kGreen && !green) {
        continue;
      }
      CubeProgram run = BestCubeProgram(state, basis, piece.first, piece.second,
                                        shelf.type, income_, rng_);
      if (!run.worth()) {
        continue;
      }
      Candidate offer;
      offer.slot = piece.second;
      offer.gear = piece.first;
      offer.cube = true;
      offer.cube_type = shelf.type;
      // Price and value the whole run, so a slot needing a dozen rolls is
      // ranked on the dozen's cost. Cubes are bought one at a time; the next
      // pass reprices the rest.
      offer.cost = run.cost;
      offer.gain = run.gain;
      offers.push_back(offer);
    }
  }
  return offers;
}

bool GearShopper::BuyBest(GameState& state, GearSpend& spend) {
  std::vector<Candidate> offers = Offers(state);
  // Compare by cross-multiplying rather than dividing, so candidates within
  // rounding of each other still sort by value.
  std::sort(offers.begin(), offers.end(),
            [](const Candidate& a, const Candidate& b) {
              return a.gain * b.cost > b.gain * a.cost;
            });
  // Move down the list when an offer is refused. One piece's refusal shouldn't
  // stop buying for every other piece. It once did, and a full Etc tab silently
  // ended shopping for the rest of the run.
  for (const Candidate& offer : offers) {
    if (offer.gain <= 0.0 || offer.cost <= 0 ||
        offer.cost > state.character.meso()) {
      continue;
    }
    if (BuyOffer(state, offer, spend)) {
      return true;
    }
  }
  return false;
}

// Keep-better, as the game offers: a roll that doesn't beat the item's current
// lines is declined, and the cube is spent either way.
bool GearShopper::BuyCube(GameState& state, EquipSlot slot, StatPreset gear,
                          CubeType cube, GearSpend& spend) {
  // Computed before buying the cube, since the comparison is against the
  // character as they are now and buying changes them.
  CubeBasis basis = CubeBasisFor(state, yard_.For(state));
  std::optional<Potential> rolled = state.character.BuyCube(slot, cube, gear);
  if (!rolled.has_value()) {
    return false;  // refused for meso or by the item
  }
  const Cube& shelf = CubeOf(cube);
  const bool green = cube == CubeType::kGreen;
  spend.cubes += shelf.cost;
  ++spend.cubes_bought;
  spend.green_cubes_bought += green;
  const bool farm =
      gear == kFarmGear && state.character.WornAt(kBossGear, slot) !=
                               state.character.WornAt(kFarmGear, slot);
  spend.farm_cubes_bought += farm;
  if (WorthTaking(state, basis, gear, slot, shelf.track, *rolled, income_)) {
    state.character.TakePotential(slot, shelf.track, *rolled, gear);
    ++spend.cubes_kept;
    spend.green_cubes_kept += green;
    spend.farm_cubes_kept += farm;
  }
  return true;
}

bool GearShopper::BuyHammer(GameState& state, EquipSlot slot,
                            GearSpend& spend) {
  if (!state.character.HammerEquipped(slot, kBossGear)) {
    return false;  // refused for meso or by the item
  }
  spend.hammers += kGoldenHammerCost;
  ++spend.hammers_driven;
  return true;
}

bool GearShopper::BuyScroll(GameState& state, const Candidate& candidate,
                            GearSpend& spend) {
  const ItemPrototype* trace = TraceItem(state);
  const EquipInstance* item = Worn(state, candidate.slot);
  if (trace == nullptr || item == nullptr) {
    return false;
  }
  int traces = TraceCost(*candidate.scroll, item->prototype().required_level());
  if (!state.character.Buy(*trace, traces) ||
      !state.character.SpendItem(kSpellTraceName, traces)) {
    return false;  // the bag refused them, not a lack of meso
  }
  state.character.ScrollEquipped(candidate.slot, *candidate.scroll, kBossGear);
  spend.scrolls += static_cast<int64_t>(traces) * trace->shop_price();
  ++spend.slots_filled;
  return true;
}

// Attempts until the star lands or meso runs out. The offer's price was the
// expected cost; this is the actual cost, and one run isn't the average.
bool GearShopper::BuyStar(GameState& state, EquipSlot slot, GearSpend& spend) {
  const StatPreset owner = OwnerOf(state.character, slot);
  const EquipInstance* item = Owned(state.character, owner, slot);
  int before = item == nullptr ? 0 : item->stars();
  // Copy the prototype: a boom destroys the EquipInstance, and recovery needs
  // to know what was lost.
  EquipPrototype proto = item == nullptr ? EquipPrototype() : item->prototype();
  while (true) {
    item = Owned(state.character, owner, slot);
    if (item == nullptr) {
      // The last attempt destroyed it. The trace plus a spare copy rebuilds the
      // piece, several stars lower with its scrolls intact, and the run
      // continues from there. That loop is what the offer's price solved.
      if (!RecoverBoom(state, owner, proto, spend)) {
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
    if (state.character.StarForceEquipped(slot, owner) == kStarForceNoMeso) {
      break;
    }
    spend.stars += attempt;
  }
  item = Owned(state.character, owner, slot);
  if (item != nullptr && item->stars() > before) {
    ++spend.stars_gained;
  }
  return true;
}

bool GearShopper::BuySymbol(GameState& state, EquipSlot slot,
                            GearSpend& spend) {
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr) {
    return false;
  }
  int64_t cost =
      SymbolLevelUpCost(item->prototype(), SymbolLevel(item->equip_state()));
  if (!state.character.LevelUpSymbol(slot, kBossGear)) {
    return false;
  }
  spend.symbols += cost;
  ++spend.symbol_levels;
  return true;
}

bool GearShopper::BuyOffer(GameState& state, const Candidate& candidate,
                           GearSpend& spend) {
  if (candidate.cube) {
    return BuyCube(state, candidate.slot, candidate.gear, candidate.cube_type,
                   spend);
  }
  if (candidate.hammer) {
    return BuyHammer(state, candidate.slot, spend);
  }
  if (candidate.symbol) {
    return BuySymbol(state, candidate.slot, spend);
  }
  if (!candidate.star) {
    return BuyScroll(state, candidate, spend);
  }
  return BuyStar(state, candidate.slot, spend);
}

bool GearShopper::RecoverBoom(GameState& state, StatPreset owner,
                              const EquipPrototype& proto, GearSpend& spend) {
  int trace_index = -1;
  int spare_index = -1;
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size() && (trace_index < 0 || spare_index < 0); ++i) {
    if (bag[i].prototype().name() != proto.name()) {
      continue;
    }
    // A trace returns null from equip_instance, which tells the two apart: one
    // is what was lost, the other is what restores it.
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
  // RecoverTrace appends the restored piece, which is what goes back on.
  return state.character.Equip(state.character.inventory().size() - 1, owner);
}

void GearShopper::SellSpares(GameState& state, GearSpend& spend) {
  // Computed per piece rather than per copy: a spare's worth depends on the
  // piece and the character's meso, not on how many the bag holds.
  std::map<std::string, int> allowance;
  int i = 0;
  while (i < state.character.inventory().size()) {
    const EquipInstance* item = state.character.inventory().equip_instance(i);
    if (item == nullptr) {
      ++i;  // a trace, the other half of a recovery
      continue;
    }
    const EquipPrototype& proto = item->prototype();
    // Never sell a spare of a worn symbol: it levels the worn one. An unworn
    // symbol keeps one copy through the allowance, or symbols from unreachable
    // areas would pile up.
    if (IsSymbol(proto) && WornStars(state.character, proto.name()) >= 0) {
      ++i;
      continue;
    }
    std::map<std::string, int>::iterator kept = allowance.find(proto.name());
    if (kept == allowance.end()) {
      int worn = WornStars(state.character, proto.name());
      // A piece not worn keeps one copy, since it's gear, not a spare. A worn
      // piece keeps as many as booms could use.
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
