#include "analysis/gear_plan.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "analysis/cube_plan.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_gear.h"
#include "analysis/soul_plan.h"
#include "analysis/star_force_curve.h"
#include "analysis/yardstick.h"
#include "src/character/arcane_force.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/character/symbol.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/soul.h"
#include "src/item/spell_trace_cost.h"
#include "src/item/star_force_cost.h"
#include "src/map_force.h"
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

// Stars on the least-starred worn copy of `name`, or -1 if none is worn. The
// farm copy of a split piece has the longer run ahead of it.
int WornStars(const CharacterInstance& character, const std::string& name) {
  int fewest = -1;
  for (StatPreset gear : {kBossGear, kFarmGear}) {
    for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
         character.equipped(gear)) {
      if (entry.second->prototype().name() == name &&
          (fewest < 0 || entry.second->stars() < fewest)) {
        fewest = entry.second->stars();
      }
    }
  }
  return fewest;
}

// Whether a worn copy of `name` could still be split for farming, which is
// what a bag copy of it would do.
bool AwaitsSplit(const CharacterInstance& character, const std::string& name) {
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       character.equipped(kBossGear)) {
    if (entry.second->prototype().name() == name &&
        SharesFarmPiece(character, entry.first)) {
      return true;
    }
  }
  return false;
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

// The upgrade slot `slot`'s item could fill next.
std::optional<GearShopper::Candidate> GearShopper::ScrollOffer(
    GameState& state, const Basis& basis, EquipSlot slot, int level,
    int open_slots) {
  if (basis.trace == nullptr || open_slots <= 0) {
    return std::nullopt;
  }
  const Scroll* scroll = ScrollFor(state, slot);
  if (scroll == nullptr) {
    return std::nullopt;
  }
  Candidate offer;
  offer.slot = slot;
  offer.gear = basis.gear;
  offer.scroll = scroll;
  offer.cost = static_cast<int64_t>(TraceCost(*scroll, level)) *
               basis.trace->shop_price();
  // A slot's value is its gain times its success rate. A failed scroll still
  // uses up the slot, and on a piece nothing sells that slot is gone.
  offer.gain = (PowerWith(state, basis.yard, basis.derived,
                          Plus(basis.worn, scroll->stats())) -
                basis.power) *
               basis.scale * plan_.scroll_rate / 100.0;
  return offer;
}

// The next star `slot`'s item could take. Nothing if it takes no stars, has
// reached the plan's limit, or isn't fully scrolled, since GMS refuses a star
// while an upgrade slot is open. Valued as the first step of the best run it
// starts; see BestStarRun.
std::optional<GearShopper::Candidate> GearShopper::StarOffer(GameState& state,
                                                             const Basis& basis,
                                                             EquipSlot slot,
                                                             int level,
                                                             int stars) {
  const int ceiling =
      std::min(plan_.star_ceiling, EquipTabItem::MaxStarsForLevel(level));
  if (stars >= ceiling) {
    return std::nullopt;
  }
  // Looked up here rather than passed in: ScrollOffer measures, and measuring
  // rebuilds the character from a proto, destroying every EquipInstance in the
  // map, this one included.
  const EquipInstance* item = state.character.WornAt(basis.gear, slot);
  if (item == nullptr) {
    return std::nullopt;
  }
  // Only risk destruction with a way to recover. The trace a boom leaves needs
  // a spare copy, and if neither the shop nor the bag has one, the piece would
  // simply be lost, which the sim can't undo.
  const bool coverable = CanCoverBoom(state.character, item->prototype());
  const double spare = static_cast<double>(SpareCost(item->prototype()));
  const EquipStats now = item->StarForceStatGains(stars);
  StarRunChoice run = BestStarRun(
      level, stars, ceiling, spare, coverable,
      [&state, &basis, item, &now](int to) {
        return PowerWith(
                   state, basis.yard, basis.derived,
                   Plus(basis.worn, Minus(item->StarForceStatGains(to), now))) -
               basis.power;
      });
  if (run.to == 0 || run.cost <= 0.0) {
    return std::nullopt;
  }
  // The whole run, bought through once started as a player sits at the Star
  // Force window, so one star's price is all the purse must hold to begin.
  Candidate offer;
  offer.slot = slot;
  offer.gear = basis.gear;
  offer.star = true;
  offer.star_to = run.to;
  offer.cost = static_cast<int64_t>(run.cost);
  offer.outlay = static_cast<int64_t>(run.step_cost);
  offer.gain = run.gain * basis.scale;
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
  // The level's value is the primary stat it adds. Its force is priced by
  // ForceRunOffer, against the target fight's brackets.
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

GearShopper::Basis GearShopper::FarmBasis(GameState& state, const Basis& boss,
                                          double power_per_meso) {
  Basis farm;
  farm.trace = boss.trace;
  farm.gear = kFarmGear;
  farm.derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                 Activity::kFarming);
  farm.worn = TotalEquipStats(state.character, farm.derived);
  farm.yard = CrowdYardstick(boss.yard, state.character.proto().level());
  farm.power = PowerWith(state, farm.yard, farm.derived, farm.worn);
  farm.scale = 0.0;
  if (income_.rate && income_.seconds_left > 0.0 && farm.power > 0.0) {
    farm.scale =
        income_.rate(MesoBonus(farm.derived), farm.derived.item_drop_pct) *
        income_.seconds_left * power_per_meso / farm.power;
  }
  return farm;
}

void GearShopper::PieceOffers(GameState& state, const Basis& basis,
                              EquipSlot slot, std::vector<Candidate>& offers) {
  const EquipInstance* item = state.character.WornAt(basis.gear, slot);
  if (item == nullptr) {
    return;
  }
  // Read these before either offer, since both measure: trying on a scroll
  // rebuilds the character, destroying every EquipInstance in the map.
  int level = item->prototype().required_level();
  int stars = item->stars();
  int open_slots = item->equip_state().remaining_upgrade_slots();
  bool can_star = item->CanStarForce();
  std::optional<Candidate> scroll =
      ScrollOffer(state, basis, slot, level, open_slots);
  if (scroll.has_value()) {
    offers.push_back(*scroll);
  }
  if (!can_star) {
    return;
  }
  std::optional<Candidate> star = StarOffer(state, basis, slot, level, stars);
  if (star.has_value()) {
    offers.push_back(*star);
  }
}

std::optional<GearShopper::Candidate> GearShopper::ForceRunOffer(
    GameState& state, const Basis& basis) {
  std::pair<std::string, int> fight;
  if (!AimedFight(state, &fight)) {
    return std::nullopt;
  }
  std::map<std::string, Boss>::const_iterator boss =
      state.bosses.find(fight.first);
  if (boss == state.bosses.end()) {
    return std::nullopt;
  }
  const MapForce force = BossForceFor(boss->second.difficulties(fight.second),
                                      state.character, state.skills);
  if (force.required <= 0) {
    return std::nullopt;
  }
  struct Held {
    EquipSlot slot;
    EquipPrototype proto;
    ms::Equip worn;
  };
  std::vector<Held> held;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    if (IsArcaneSymbol(entry.second->prototype())) {
      held.push_back({entry.first, entry.second->prototype(),
                      entry.second->equip_state()});
    }
  }
  // The yardstick is read at a factor of 1, so every other offer's gain is
  // damage over the current factor; the run's is put in the same units.
  const double now = force.factors.damage_dealt;
  const double top =
      ArcaneFactorsFor(force.required * 2, force.required).damage_dealt;
  const StatField primary = PrimaryStatField(state.character.proto().job());
  EquipStats worn = basis.worn;
  int owned = force.owned;
  double factor = now;
  Candidate run;
  run.symbol = true;
  std::optional<Candidate> best;
  while (factor < top) {
    Held* cheapest = nullptr;
    int64_t price = 0;
    for (Held& symbol : held) {
      if (!SymbolCanLevelUp(symbol.proto, symbol.worn)) {
        continue;
      }
      const int64_t cost =
          SymbolLevelUpCost(symbol.proto, SymbolLevel(symbol.worn));
      if (cost > 0 && (cheapest == nullptr || cost < price)) {
        cheapest = &symbol;
        price = cost;
      }
    }
    if (cheapest == nullptr) {
      break;
    }
    const int level = SymbolLevel(cheapest->worn);
    LevelUpSymbol(cheapest->proto, cheapest->worn);
    worn = Plus(worn, Minus(SymbolStatsFor(cheapest->proto, primary, level + 1),
                            SymbolStatsFor(cheapest->proto, primary, level)));
    owned += SymbolForce(cheapest->proto, level + 1) -
             SymbolForce(cheapest->proto, level);
    if (run.symbol_run.empty()) {
      run.slot = cheapest->slot;
      run.outlay = price;
    }
    run.symbol_run.push_back(cheapest->slot);
    run.cost += price;
    const double reached = ArcaneFactorsFor(owned, force.required).damage_dealt;
    if (reached <= factor) {
      continue;
    }
    factor = reached;
    run.gain =
        PowerWith(state, basis.yard, basis.derived, worn) * factor / now -
        basis.power;
    if (!best.has_value() || run.gain / run.cost > best->gain / best->cost) {
      best = run;
    }
  }
  return best;
}

std::vector<GearShopper::Candidate> GearShopper::Offers(GameState& state) {
  Basis basis;
  basis.trace = TraceItem(state);
  basis.worn = WornAndGranted(state, basis.derived);
  basis.yard = yard_.For(state);
  basis.power = PowerWith(state, basis.yard, basis.derived, basis.worn);
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    slots.push_back(entry.first);
  }
  std::vector<Candidate> offers;
  for (EquipSlot slot : slots) {
    std::optional<Candidate> symbol = SymbolOffer(state, basis, slot);
    if (symbol.has_value()) {
      offers.push_back(*symbol);
      continue;  // a symbol takes neither scrolls nor stars
    }
    PieceOffers(state, basis, slot, offers);
  }
  std::optional<Candidate> force_run = ForceRunOffer(state, basis);
  if (force_run.has_value()) {
    offers.push_back(*force_run);
  }
  // Farm, split and cube offers come after the rest, which set what a meso is
  // worth. They pay in income, and only that rate lets it rank against damage.
  double best = 0.0;
  for (const Candidate& offer : offers) {
    if (offer.cost > 0) {
      best = std::max(best, static_cast<double>(offer.gain) / offer.cost);
    }
  }
  // What a meso buys elsewhere is what a roll costs in power, so it sets where
  // cubing stops. Set before the splits, whose price is a cube run.
  income_.power_per_meso = best;
  const Basis farm = FarmBasis(state, basis, best);
  if (farm.scale > 0.0) {
    std::vector<EquipSlot> farm_only;
    for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
         state.character.equipped(kFarmGear)) {
      if (state.character.WornAt(kBossGear, entry.first) != entry.second) {
        farm_only.push_back(entry.first);
      }
    }
    for (EquipSlot slot : farm_only) {
      PieceOffers(state, farm, slot, offers);
    }
    if (!splits_.has_value()) {
      splits_ = SplitOffers(state, farm);
    }
    offers.insert(offers.end(), splits_->begin(), splits_->end());
  }
  std::vector<Candidate> cubes = CubeOffers(state, best);
  offers.insert(offers.end(), cubes.begin(), cubes.end());
  std::vector<Candidate> flames = FlameOffers(state, best);
  offers.insert(offers.end(), flames.begin(), flames.end());
  return offers;
}

std::vector<GearShopper::Candidate> GearShopper::SplitOffers(
    GameState& state, const Basis& farm) {
  std::vector<Candidate> offers;
  if (!plan_.cubes) {
    return offers;  // a bare copy is only worth the meso line cubed onto it
  }
  std::vector<std::pair<EquipSlot, std::string>> shared;
  std::set<std::string> seen;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    const EquipPrototype& proto = entry.second->prototype();
    if (SharesFarmPiece(state.character, entry.first) &&
        entry.second->CanCube() && seen.insert(proto.name()).second &&
        (proto.shop_price() > 0 ||
         SparesInBag(state.character, proto.name()) > 0)) {
      shared.push_back({entry.first, proto.name()});
    }
  }
  if (shared.empty()) {
    return offers;
  }
  // Every trial ends by putting this back, which destroys every EquipInstance
  // the character held; nothing below keeps one across a trial.
  const Character before = state.character.ToProto();
  for (const std::pair<EquipSlot, std::string>& piece : shared) {
    const EquipPrototype proto =
        state.character.WornAt(kBossGear, piece.first)->prototype();
    const int64_t copy = SpareCost(proto);
    if (SparesInBag(state.character, piece.second) == 0 &&
        !state.character.Buy(proto, 1)) {
      continue;
    }
    const EquipSlot at = SplitFarmPiece(state.character, piece.first);
    if (at != EQUIP_SLOT_UNSPECIFIED) {
      DerivedStats derived = DerivedStatsFor(state.character, state.skills, {},
                                             {}, Activity::kFarming);
      const double lost =
          (farm.power - PowerWith(state, farm.yard, derived,
                                  TotalEquipStats(state.character, derived))) *
          farm.scale;
      CubeProgram run = BestMainCubeProgram(state, at);
      if (run.worth() && run.gain > lost) {
        Candidate offer;
        offer.slot = piece.first;
        offer.split = true;
        offer.cost = copy + run.cost;
        offer.gain = run.gain - lost;
        offers.push_back(offer);
      }
    }
    state.character.RestoreFrom(before, state.equips, state.items);
  }
  return offers;
}

// Whether `cube` is on the shelf at the character's level. The sim plays one
// character, so its level is the account's.
bool OnShelf(const GameState& state, CubeType cube) {
  return state.character.proto().level() >= UnlockLevel(CubeFeature(cube));
}

CubeProgram GearShopper::BestMainCubeProgram(const GameState& state,
                                             EquipSlot slot) {
  const CubeBasis basis = CubeBasisFor(state, yard_.For(state));
  CubeProgram best;
  for (const Cube& shelf : kCubes) {
    if (shelf.track != PotentialTrack::kMain || !OnShelf(state, shelf.type)) {
      continue;
    }
    CubeProgram run = BestCubeProgram(state, basis, kFarmGear, slot, shelf.type,
                                      income_, rng_);
    if (run.worth() &&
        (!best.worth() || run.gain * best.cost > best.gain * run.cost)) {
      best = run;
    }
  }
  return best;
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
  // Stored so the keep decision uses the same value as the pricing.
  income_.power_per_meso = best;
  CubeBasis basis = CubeBasisFor(state, yard_.For(state));
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
      if (!OnShelf(state, shelf.type)) {
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
      offer.cube_program = run;
      // Price and value the whole run, so a slot needing a dozen rolls is
      // ranked on the dozen's cost. The run stops where the purse does, so one
      // cube is all it must cover to start.
      offer.cost = run.cost;
      offer.outlay = shelf.cost;
      offer.gain = run.gain;
      offers.push_back(offer);
    }
  }
  return offers;
}

std::vector<GearShopper::Candidate> GearShopper::FlameOffers(GameState& state,
                                                             double best) {
  std::vector<Candidate> offers;
  if (!plan_.flames ||
      state.character.proto().level() < UnlockLevel(Feature::kFlame)) {
    return offers;
  }
  const CubeBasis basis = CubeBasisFor(state, yard_.For(state));
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    if (entry.second->CanFlame()) {
      slots.push_back(entry.first);
    }
  }
  for (EquipSlot slot : slots) {
    for (const Flame& shelf : kFlames) {
      FlameProgram run =
          BestFlameProgram(state, basis, slot, shelf.type, best, rng_);
      if (!run.worth()) {
        continue;
      }
      Candidate offer;
      offer.slot = slot;
      offer.flame = true;
      offer.flame_type = shelf.type;
      offer.flame_program = run;
      offer.cost = run.cost;
      offer.outlay = shelf.cost;
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
        (offer.outlay > 0 ? offer.outlay : offer.cost) >
            state.character.meso()) {
      continue;
    }
    if (BuyOffer(state, offer, spend)) {
      return true;
    }
  }
  return false;
}

// Rolls `program`'s run through: a replacing cube's roll always goes on, a
// choosing cube's when the run's rule would rather be there, and rolling stops
// where the rule does or the purse runs dry. A player cubes a piece until it's
// good enough rather than weighing the whole shelf between cubes.
bool GearShopper::BuyCube(GameState& state, EquipSlot slot, StatPreset gear,
                          CubeType cube, const CubeProgram& program,
                          GearSpend& spend) {
  // Read before the first cube, since the run is valued against the piece as
  // it is now and every roll changes it.
  const CubeRun run(state, yard_.For(state), gear, slot, cube, income_,
                    program);
  const Cube& shelf = CubeOf(cube);
  const int which = static_cast<int>(cube);
  const bool farm =
      gear == kFarmGear && state.character.WornAt(kBossGear, slot) !=
                               state.character.WornAt(kFarmGear, slot);
  bool bought = false;
  do {
    std::optional<Potential> rolled = state.character.BuyCube(slot, cube, gear);
    if (!rolled.has_value()) {
      break;  // refused for meso or by the item
    }
    bought = true;
    spend.cubes += shelf.cost;
    ++spend.cubes_bought;
    ++spend.bought_by_cube[which];
    spend.farm_cubes_bought += farm;
    if (!shelf.choose || run.Takes(state, *rolled)) {
      state.character.TakePotential(slot, shelf.track, *rolled, gear);
      ++spend.cubes_kept;
      ++spend.kept_by_cube[which];
      spend.farm_cubes_kept += farm;
    }
  } while (run.Continues(state));
  return bought;
}

// Rolls `program`'s run through as BuyCube does: Burning's roll always goes on,
// Black's when it beats what the piece holds.
bool GearShopper::BuyFlame(GameState& state, EquipSlot slot, FlameType flame,
                           const FlameProgram& program, GearSpend& spend) {
  const FlameRun run(state, yard_.For(state), slot, program);
  const Flame& shelf = FlameOf(flame);
  const int which = static_cast<int>(flame);
  bool bought = false;
  do {
    const EquipInstance* item = state.character.WornAt(kBossGear, slot);
    if (item == nullptr) {
      break;
    }
    std::optional<FlameLines> rolled = state.character.BuyFlame(
        slot, flame, item->equip_state().flame(), kBossGear);
    if (!rolled.has_value()) {
      break;  // refused for meso or by the item
    }
    bought = true;
    spend.flames += shelf.cost;
    ++spend.bought_by_flame[which];
    ++spend.flames_by_slot[slot][which];
    if (!shelf.choose || run.Takes(state, *rolled)) {
      state.character.TakeFlame(slot, *rolled, kBossGear);
      ++spend.kept_by_flame[which];
    }
  } while (run.Continues(state));
  return bought;
}

bool GearShopper::BuyScroll(GameState& state, const Candidate& candidate,
                            GearSpend& spend) {
  const ItemPrototype* trace = TraceItem(state);
  const EquipInstance* item =
      state.character.WornAt(candidate.gear, candidate.slot);
  if (trace == nullptr || item == nullptr) {
    return false;
  }
  int traces = TraceCost(*candidate.scroll, item->prototype().required_level());
  if (!state.character.Buy(*trace, traces) ||
      !state.character.SpendItem(kSpellTraceName, traces)) {
    return false;  // the bag refused them, not a lack of meso
  }
  state.character.ScrollEquipped(candidate.slot, *candidate.scroll,
                                 candidate.gear);
  spend.scrolls += static_cast<int64_t>(traces) * trace->shop_price();
  ++spend.slots_filled;
  return true;
}

// Attempts until the star lands or meso runs out. The offer's price was the
// expected cost; this is the actual cost, and one run isn't the average.
bool GearShopper::BuyStar(GameState& state, EquipSlot slot, StatPreset gear,
                          int to, GearSpend& spend) {
  // A farm-only piece is the first preset's own, like everything it wears.
  const StatPreset owner =
      gear == kFarmGear ? kFarmGear : OwnerOf(state.character, slot);
  const EquipInstance* item = Owned(state.character, owner, slot);
  const int before = item == nullptr ? 0 : item->stars();
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
    if (item->stars() >= to) {
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
    spend.stars_gained += item->stars() - before;
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

bool GearShopper::BuySymbolRun(GameState& state,
                               const std::vector<EquipSlot>& run,
                               GearSpend& spend) {
  int bought = 0;
  for (EquipSlot slot : run) {
    if (!BuySymbol(state, slot, spend)) {
      break;
    }
    ++bought;
  }
  return bought > 0;
}

bool GearShopper::BuySplit(GameState& state, EquipSlot slot, GearSpend& spend) {
  const EquipInstance* worn = state.character.WornAt(kBossGear, slot);
  if (worn == nullptr) {
    return false;
  }
  const EquipPrototype proto = worn->prototype();
  const bool bought = SparesInBag(state.character, proto.name()) == 0;
  if (bought && !state.character.Buy(proto, 1)) {
    return false;
  }
  if (SplitFarmPiece(state.character, slot) == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  spend.copies += bought ? proto.shop_price() : 0;
  ++spend.farm_splits;
  splits_.reset();
  return true;
}

bool GearShopper::BuyOffer(GameState& state, const Candidate& candidate,
                           GearSpend& spend) {
  if (candidate.split) {
    return BuySplit(state, candidate.slot, spend);
  }
  if (candidate.cube) {
    return BuyCube(state, candidate.slot, candidate.gear, candidate.cube_type,
                   candidate.cube_program, spend);
  }
  if (candidate.flame) {
    return BuyFlame(state, candidate.slot, candidate.flame_type,
                    candidate.flame_program, spend);
  }
  if (!candidate.symbol_run.empty()) {
    return BuySymbolRun(state, candidate.symbol_run, spend);
  }
  if (candidate.symbol) {
    return BuySymbol(state, candidate.slot, spend);
  }
  if (!candidate.star) {
    return BuyScroll(state, candidate, spend);
  }
  return BuyStar(state, candidate.slot, candidate.gear, candidate.star_to,
                 spend);
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
      // piece keeps as many as booms could use, and one to split for farming.
      int keep = worn < 0 ? 1 : SparesWorthKeeping(state, proto, worn);
      if (AwaitsSplit(state.character, proto.name())) {
        keep = std::max(keep, 1);
      }
      kept = allowance.insert({proto.name(), keep}).first;
    }
    if (state.character.MeetsJob(proto) && kept->second > 0) {
      --kept->second;
      ++i;
      continue;
    }
    spend.sold += state.character.SellEquip(i);
  }
}

// The souls in hand by tier, and a boss of each to name a trial soul after.
// Every boss of a tier rolls the same lines, so a tier's shards pool.
struct SoulRolls {
  std::map<SoulTier, int> rolls;
  std::map<SoulTier, std::string> boss;
};

namespace {

SoulRolls SoulsInHand(const CharacterInstance& character) {
  SoulRolls hand;
  for (const CurrencyAmount& held : character.currencies().entries()) {
    const ItemPrototype& shard = held.prototype();
    if (shard.kind() == ITEM_KIND_SOUL_SHARD &&
        held.count() >= kShardsPerSoul) {
      hand.rolls[shard.soul_tier()] += held.count() / kShardsPerSoul;
      hand.boss[shard.soul_tier()] = shard.short_name();
    }
  }
  return hand;
}

std::string SoulsKey(int level, const Soul& held,
                     const std::map<SoulTier, int>& rolls) {
  std::string key = std::to_string(level) + "|" + held.SerializeAsString();
  for (const auto& [tier, count] : rolls) {
    if (count > 0) {
      key += "|" + std::to_string(tier) + ":" + std::to_string(count);
    }
  }
  return key;
}

// A shard of `tier` with a soul's worth, copied: spending the last of a boss
// drops its purse entry.
std::optional<ItemPrototype> ShardToSpend(const CharacterInstance& character,
                                          SoulTier tier) {
  for (const CurrencyAmount& entry : character.currencies().entries()) {
    if (entry.prototype().kind() == ITEM_KIND_SOUL_SHARD &&
        entry.prototype().soul_tier() == tier &&
        entry.count() >= kShardsPerSoul) {
      return entry.prototype();
    }
  }
  return std::nullopt;
}

constexpr EquipSlot kSoulSlot = EQUIP_SLOT_PRIMARY_WEAPON;

}  // namespace

std::map<SoulTier, std::vector<double>> GearShopper::SoulLineWorths(
    GameState& state, const SoulRolls& hand) {
  CharacterInstance& character = state.character;
  const Soul held =
      character.WornAt(kBossGear, kSoulSlot)->equip_state().soul();
  std::map<SoulTier, std::vector<double>> worths;
  for (const auto& [tier, count] : hand.rolls) {
    Soul trial;
    trial.set_boss(hand.boss.at(tier));
    trial.set_tier(tier);
    for (int line = SOUL_LINE_ATTACK; line <= SOUL_LINE_BOSS_DAMAGE; ++line) {
      trial.set_line(static_cast<SoulLine>(line));
      character.TakeSoul(kSoulSlot, trial, kBossGear);
      worths[tier].push_back(Power(state));
    }
  }
  character.TakeSoul(kSoulSlot, held, kBossGear);
  return worths;
}

void GearShopper::ApplySouls(GameState& state, GearSpend& spend) {
  CharacterInstance& character = state.character;
  const EquipInstance* weapon = character.WornAt(kBossGear, kSoulSlot);
  if (!Unlocked(Feature::kSoul, character, state.account) ||
      weapon == nullptr || !weapon->CanTakeSoul()) {
    return;
  }
  SoulRolls hand = SoulsInHand(character);
  const int level = character.proto().level();
  if (hand.rolls.empty() || SoulsKey(level, weapon->equip_state().soul(),
                                     hand.rolls) == souls_settled_) {
    return;
  }
  const std::map<SoulTier, std::vector<double>> worths =
      SoulLineWorths(state, hand);
  double held_worth = Power(state);
  while (true) {
    SoulTier best = SOUL_TIER_UNSPECIFIED;
    double best_worth = 0.0;
    for (const auto& [tier, count] : hand.rolls) {
      const double worth = SoulRollWorth(worths.at(tier), count);
      if (worth > best_worth) {
        best = tier;
        best_worth = worth;
      }
    }
    if (best == SOUL_TIER_UNSPECIFIED ||
        !ShouldRollSoul(held_worth, worths.at(best), hand.rolls[best])) {
      break;
    }
    std::optional<ItemPrototype> shard = ShardToSpend(character, best);
    if (!shard.has_value() ||
        !character.ApplySoul(kSoulSlot, *shard, kBossGear)) {
      break;
    }
    --hand.rolls[best];
    ++spend.souls;
    const SoulLine line =
        character.WornAt(kBossGear, kSoulSlot)->equip_state().soul().line();
    held_worth = worths.at(best)[line - SOUL_LINE_ATTACK];
  }
  souls_settled_ = SoulsKey(
      level, character.WornAt(kBossGear, kSoulSlot)->equip_state().soul(),
      hand.rolls);
}

GearSpend GearShopper::Spend(GameState& state) {
  GearSpend spend;
  splits_.reset();
  SellSpares(state, spend);
  ApplySouls(state, spend);
  while (BuyBest(state, spend)) {
  }
  life_.Add(spend);
  return spend;
}

}  // namespace ms
