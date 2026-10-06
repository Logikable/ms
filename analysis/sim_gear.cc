#include "analysis/sim_gear.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/progression.h"
#include "src/character/symbol.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/item/projectile.h"
#include "src/item/ring_box.h"
#include "src/item/shop.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Map and mob for weapon tryouts, made up rather than taken from the catalog: a
// real map's crowd would skew a comparison meant for one weapon against one
// mob.
constexpr char kTryoutMap[] = "__sim_gear_tryout";
constexpr char kTryoutMob[] = "__sim_gear_tryout_mob";

// How long each candidate attacks, in game seconds: long enough that a
// four-second cooldown fires a dozen times.
constexpr double kTryoutSeconds = 60.0;

// Required level of what is worn in `slot`, which orders tiers against each
// other: the shop's weapon tiers are ordered by it.
int HeldTier(const CharacterInstance& character, EquipSlot slot) {
  WornGear::const_iterator it = character.equipped().find(slot);
  return it == character.equipped().end()
             ? 0
             : it->second->prototype().required_level();
}

// Wears `proto` without charging for it, as trying on a weapon costs a player
// nothing. Reuses a copy already in the bag rather than adding another, since
// the bag is finite and this runs at every level.
bool WearCopy(CharacterInstance& character, const EquipPrototype& proto) {
  if (EquipByName(character, proto.name())) {
    return true;
  }
  if (!character.PickUp(std::make_unique<EquipInstance>(proto))) {
    return false;
  }
  return character.Equip(character.inventory().size() - 1);
}

// Both shelves of one stock list, meso first then tokens. Token items count as
// stock like any other, since the sims play the fights that drop the tokens and
// the Frozen tier is as reachable for them as for a player.
std::vector<std::string> BothShelves(std::vector<std::string> meso,
                                     const std::vector<std::string>& tokens) {
  meso.insert(meso.end(), tokens.begin(), tokens.end());
  return meso;
}

std::vector<std::string> WeaponShelf(const GameState& state) {
  return BothShelves(ShopWeaponStock(state.equips, kPaidInMeso),
                     ShopWeaponStock(state.equips, kPaidInTokens));
}

std::vector<std::string> EquipShelf(const GameState& state) {
  return BothShelves(ShopEquipStock(state.equips, kPaidInMeso),
                     ShopEquipStock(state.equips, kPaidInTokens));
}

// Off-hands only. The shelf also has rings, the emblem and the medal, which
// fill their own slots and aren't what a branch equips in its off-hand.
std::vector<std::string> SecondaryShelf(const GameState& state) {
  std::vector<std::string> keys = EquipShelf(state);
  keys.erase(std::remove_if(keys.begin(), keys.end(),
                            [&state](const std::string& key) {
                              return state.equips.at(key).equip_slot() !=
                                     EQUIP_SLOT_SECONDARY;
                            }),
             keys.end());
  return keys;
}

// Whether an unworn copy of `name` sits in the bag, as a box leaves one.
bool HeldInBag(const CharacterInstance& character, const std::string& name) {
  const InventoryInstance& bag = character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    if (!bag[i].is_trace() && bag[i].prototype().name() == name) {
      return true;
    }
  }
  return false;
}

// The first of `proto`'s token prices the character can pay, or null when
// they can pay none of them.
const TokenPrice* PayablePrice(const GameState& state,
                               const EquipPrototype& proto) {
  for (const TokenPrice& price : proto.token_prices()) {
    std::map<std::string, ItemPrototype>::const_iterator token =
        state.items.find(price.token_item());
    if (token != state.items.end() &&
        price.count() <= state.character.CountItem(token->second.name())) {
      return &price;
    }
  }
  return nullptr;
}

// Whether the character can currently afford `proto`. A token price is paid
// from tokens that fights dropped, so this checks the purse rather than meso.
bool CanPayFor(const GameState& state, const EquipPrototype& proto) {
  if (proto.token_prices().empty()) {
    return proto.shop_price() <= state.character.meso();
  }
  return PayablePrice(state, proto) != nullptr ||
         HeldInBag(state.character, proto.name());
}

// Buys one `proto` in whichever currency it's priced in. A token piece a box
// already gave costs nothing more.
bool BuyOne(GameState& state, const EquipPrototype& proto) {
  if (!proto.token_prices().empty() &&
      HeldInBag(state.character, proto.name())) {
    return true;
  }
  if (proto.token_prices().empty()) {
    return state.character.Buy(proto, 1);
  }
  const TokenPrice* price = PayablePrice(state, proto);
  return price != nullptr &&
         state.character.BuyWithToken(proto, price->token_item(),
                                      state.items.at(price->token_item()), 1);
}

// Best tier of `type` the character can use, or null if the shop stocks none
// they can equip. With `budget`, it must also be affordable.
const EquipPrototype* BestRung(const GameState& state, EquipType type,
                               bool budget) {
  const EquipPrototype* best = nullptr;
  for (const std::string& key : WeaponShelf(state)) {
    const EquipPrototype& proto = state.equips.at(key);
    if (proto.equip_type() != type || !state.character.CanEquip(proto)) {
      continue;
    }
    if (budget && !CanPayFor(state, proto)) {
      continue;
    }
    if (best == nullptr || proto.required_level() > best->required_level()) {
      best = &proto;
    }
  }
  return best;
}

// Top tier of every weapon type the character can use. Only the top of each is
// worth trying: within a type tiers only improve, so the question is which
// type, not which tier.
std::vector<const EquipPrototype*> Ladders(const GameState& state,
                                           bool budget) {
  std::vector<const EquipPrototype*> tops;
  std::vector<EquipType> seen;
  for (const std::string& key : WeaponShelf(state)) {
    const EquipPrototype& proto = state.equips.at(key);
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
        !state.character.CanEquip(proto)) {
      continue;
    }
    if (std::find(seen.begin(), seen.end(), proto.equip_type()) != seen.end()) {
      continue;
    }
    seen.push_back(proto.equip_type());
    const EquipPrototype* best = BestRung(state, proto.equip_type(), budget);
    if (best != nullptr) {
      tops.push_back(best);
    }
  }
  return tops;
}

// Creates the tryout map and mob and moves the character there. The mob is the
// character's own level and has the measurement's HP, so a held attack isn't
// released early. Returns the map they came from.
std::string OpenTryout(GameState& state) {
  Mob mob;
  mob.set_name("Tryout");
  mob.set_level(state.character.proto().level());
  mob.set_max_hp(kMeasuredMobHp);
  state.mobs[kTryoutMob] = mob;
  MapData map;
  map.set_name("Tryout");
  Spawn* spawn = map.add_spawns();
  spawn->set_mob(kTryoutMob);
  spawn->set_count(1);
  state.maps[kTryoutMap] = map;
  std::string farming = state.current_map;
  state.current_map = kTryoutMap;
  return farming;
}

// Removes the tryout map and mob, which are scratch data.
void CloseTryout(GameState& state, const std::string& farming) {
  state.current_map = farming;
  state.maps.erase(kTryoutMap);
  state.mobs.erase(kTryoutMob);
}

// Damage per second the character now deals to a lone mob of their own level:
// their attacks plus anything of theirs on its own clock.
double MeasureRate(GameState& state) {
  CombatParams params = ComputeCombatParams(state);
  // Stretched on the way in, since MeasureFight counts in that clock (see its
  // header).
  Sequence played = MeasureFight(
      params,
      kTryoutSeconds * GameSpeedFactor(state.character.proto().level()));
  return played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
}

}  // namespace

bool EquipByName(CharacterInstance& character, const std::string& name) {
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipInstance* item = character.inventory().equip_instance(i);
    if (item != nullptr && item->name() == name &&
        character.CanEquip(item->prototype())) {
      return character.Equip(i);
    }
  }
  return false;
}

std::string HeldWeaponName(const CharacterInstance& character) {
  WornGear::const_iterator it =
      character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  return it == character.equipped().end() ? "-" : it->second->name();
}

namespace {

// The weapon type the character currently hits hardest with. The top tier of
// each type is tried against a mob of their own level, along with the weapon in
// hand. Unspecified if nothing can be worn or tried.
EquipType MeasureBestType(GameState& state, bool budget) {
  std::vector<const EquipPrototype*> ladders = Ladders(state, budget);
  if (ladders.empty() ||
      state.character.inventory().room() < static_cast<int>(ladders.size())) {
    return EQUIP_TYPE_UNSPECIFIED;
  }
  // Each try-on leaves the displaced copy in the bag, and restoring from the
  // proto is the only way to undo that. Without it, the bag fills with unbought
  // weapons and the room check stops the character shopping for good.
  Character before = state.character.ToProto();

  // The weapon in hand goes first so a tie keeps it. A Frozen weapon can't be
  // bought twice with one token, and after spending the character may only
  // afford cheap tiers.
  EquipPrototype worn;
  WornGear::const_iterator it =
      state.character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it != state.character.equipped().end()) {
    worn = it->second->prototype();
    ladders.insert(ladders.begin(), &worn);
  }

  std::string farming = OpenTryout(state);

  EquipType winner = EQUIP_TYPE_UNSPECIFIED;
  double best_rate = 0.0;
  for (const EquipPrototype* candidate : ladders) {
    if (!WearCopy(state.character, *candidate)) {
      continue;
    }
    // A claw with no projectiles deals nothing, and a dagger with throwing
    // stars would be credited for ammunition it never uses. So each candidate
    // is measured with exactly the ammunition it uses.
    state.character.Unequip(EQUIP_SLOT_PROJECTILE);
    EquipType ammo = AmmoFor(candidate->equip_type());
    if (ammo != EQUIP_TYPE_UNSPECIFIED) {
      const EquipPrototype* rung = BestRung(state, ammo, budget);
      if (rung != nullptr) {
        WearCopy(state.character, *rung);
      }
    }
    double rate = MeasureRate(state);
    if (rate > best_rate) {
      best_rate = rate;
      winner = candidate->equip_type();
    }
  }

  CloseTryout(state, farming);
  // Undo every try-on. What the character wears is what they paid for, which
  // the buying code below handles.
  state.character.RestoreFrom(before, state.equips, state.items);
  return winner;
}

// Buys and wears the best tier of `type` within reach. Does nothing if the
// character already has that type at the same tier or higher; a character on
// the wrong type swaps regardless of tier.
void ClimbLadder(GameState& state, EquipType type, bool budget) {
  const EquipPrototype* best = BestRung(state, type, budget);
  if (best == nullptr) {
    return;
  }
  WornGear::const_iterator held =
      state.character.equipped().find(best->equip_slot());
  bool right_ladder = held != state.character.equipped().end() &&
                      held->second->prototype().equip_type() == type;
  if (right_ladder &&
      best->required_level() <= held->second->prototype().required_level()) {
    return;
  }
  if (budget && !BuyOne(state, *best)) {
    return;
  }
  WearCopy(state.character, *best);
}

// Best off-hand the shop will sell the character. Nothing is measured: each
// off-hand belongs to one branch and has plain stats, so the only choice is the
// tier.
const EquipPrototype* BestSecondary(const GameState& state, bool budget) {
  const EquipPrototype* best = nullptr;
  for (const std::string& key : SecondaryShelf(state)) {
    const EquipPrototype& proto = state.equips.at(key);
    // Use the shop's own filter rather than CanEquip, which only checks the job
    // category. The three warrior off-hands aren't interchangeable.
    if (!state.character.MeetsLevel(proto) ||
        !state.character.MeetsJob(proto)) {
      continue;
    }
    if (budget && !CanPayFor(state, proto)) {
      continue;
    }
    if (best == nullptr || proto.required_level() > best->required_level()) {
      best = &proto;
    }
  }
  return best;
}

}  // namespace

namespace {

// Scrolls `proto` can take: matching its level tier, its equipment kind, and a
// shared job category. These are the scroll panel's three rules, excluding the
// clean slate scroll, which undoes rather than upgrades.
std::vector<const Scroll*> ScrollsFor(const GameState& state,
                                      const EquipPrototype& proto) {
  std::vector<const Scroll*> taken;
  ScrollTarget target = TargetForSlot(proto.equip_slot());
  if (target == SCROLL_TARGET_UNSPECIFIED || !Supports(proto, UPGRADE_SCROLL) ||
      proto.upgrade_slots() <= 0) {
    return taken;
  }
  std::set<int> categories(proto.equip_job_categories().begin(),
                           proto.equip_job_categories().end());
  for (const std::pair<const std::string, Scroll>& entry : state.scrolls) {
    const Scroll& scroll = entry.second;
    if (scroll.tier() != TierForLevel(proto.required_level()) ||
        scroll.target() != target ||
        scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE ||
        // Spends an Etc item the sim never holds, and TraceCost prices it at 0.
        !scroll.paid_with().empty()) {
      continue;
    }
    for (int category : scroll.applicable_job_categories()) {
      if (categories.count(category) > 0) {
        taken.push_back(&scroll);
        break;
      }
    }
  }
  return taken;
}

// `proto` as a dedicated player would leave it: every slot filled with
// `scroll`, and stars up to `star_cap`. A null scroll leaves the slots unused,
// as for an item that takes none.
Equip AtCeiling(const EquipPrototype& proto, const Scroll* scroll,
                int star_cap) {
  Equip state = FreshEquip(proto);
  int slots = TotalUpgradeSlots(proto);
  if (scroll != nullptr) {
    state.set_remaining_upgrade_slots(0);
    state.set_scroll_successes(slots);
    EquipStats gained;
    for (int i = 0; i < slots; ++i) {
      gained = SumEquipStats({gained, scroll->stats()});
    }
    *state.mutable_scroll_stats() = gained;
  }
  if (Supports(proto, UPGRADE_STAR_FORCE) &&
      state.remaining_upgrade_slots() == 0) {
    state.set_stars(std::min(
        star_cap, EquipTabItem::MaxStarsForLevel(proto.required_level())));
  }
  return state;
}

}  // namespace

bool SplitsFarmGear(EquipSlot slot) {
  return PotentialGroupOf(slot) == PotentialGroup::kAccessory;
}

namespace {

// Wears a fresh `proto` with state `made` in `slot`, leaving the displaced copy
// in the bag. `slot` is where the item is worn: every ring says
// EQUIP_SLOT_RING, and unequipping that would remove the wrong ring.
bool WearMade(CharacterInstance& character, EquipSlot slot,
              const EquipPrototype& proto, const Equip& made) {
  character.Unequip(slot);
  if (!character.PickUp(std::make_unique<EquipInstance>(proto, made))) {
    return false;
  }
  return character.Equip(character.inventory().size() - 1);
}

// The scroll the piece boss fights wear in `slot` should use: the one the
// character measures best with, including no scroll (tried first).
// `success_rate` narrows the choice, since a budget player picks a rate before
// a stat. Each try-on goes where the measurement reads, the farm gear. Leaves
// the character wearing the last try-on for the caller to restore.
const Scroll* BestScrollForSlot(GameState& state, EquipSlot slot,
                                int success_rate) {
  const EquipInstance* worn = state.character.WornAt(kBossGear, slot);
  if (worn == nullptr) {
    return nullptr;
  }
  EquipPrototype proto = worn->prototype();
  std::vector<const Scroll*> candidates;
  for (const Scroll* scroll : ScrollsFor(state, proto)) {
    if (success_rate == 0 || scroll->success_rate() == success_rate) {
      candidates.push_back(scroll);
    }
  }
  // No scroll is a candidate only when the slots are worth keeping. An item
  // that takes stars must fill every slot first, so declining a scroll would
  // also cost the stars.
  if (!Supports(proto, UPGRADE_STAR_FORCE)) {
    candidates.insert(candidates.begin(), nullptr);
  }
  const Scroll* best = nullptr;
  double best_rate = -1.0;
  for (const Scroll* candidate : candidates) {
    if (!WearMade(state.character, slot, proto,
                  AtCeiling(proto, candidate, kMaxStarForce))) {
      continue;
    }
    double rate = MeasureRate(state);
    if (rate > best_rate) {
      best_rate = rate;
      best = candidate;
    }
  }
  return best;
}

}  // namespace

namespace {

// True for items Outfit already shops for: the slots it buys weapons and
// off-hands for, and any item with a price. Checking only the price would give
// away the Fafnir, which nothing sells.
bool Shopped(const EquipPrototype& proto) {
  EquipSlot slot = proto.equip_slot();
  return slot == EQUIP_SLOT_PRIMARY_WEAPON || slot == EQUIP_SLOT_SECONDARY ||
         slot == EQUIP_SLOT_PROJECTILE || proto.has_shop_price() ||
         !proto.token_prices().empty();
}

// Wears the best of `candidates` in every slot of one family, highest tier
// first, with name breaking ties so runs repeat. A second copy of a ring would
// only swap with the first.
void WearBestOfFamily(CharacterInstance& character, EquipSlot family,
                      std::vector<const EquipPrototype*> candidates) {
  std::sort(candidates.begin(), candidates.end(),
            [](const EquipPrototype* a, const EquipPrototype* b) {
              if (a->required_level() != b->required_level()) {
                return a->required_level() > b->required_level();
              }
              return a->name() < b->name();
            });
  int room = static_cast<int>(SlotFamily(family).size());
  for (const EquipPrototype* proto : candidates) {
    if (room <= 0) {
      break;
    }
    if (character.IsWearing(proto->name()) || WearCopy(character, *proto)) {
      --room;
    }
  }
}

// The rest of the shop's equipment: rings, emblem, medal and the free pets.
// Nothing is measured, for the same reason as off-hands (plain stats, so a
// higher tier is simply better), and a family takes as many as it has slots.
void BuyAccessories(GameState& state, bool budget) {
  std::map<EquipSlot, std::vector<const EquipPrototype*>> by_family;
  std::vector<std::string> shelf = EquipShelf(state);
  for (const std::string& pet : ShopPetStock(state.equips)) {
    shelf.push_back(pet);
  }
  for (const std::string& key : shelf) {
    const EquipPrototype& proto = state.equips.at(key);
    if (proto.equip_slot() == EQUIP_SLOT_SECONDARY ||
        !state.character.MeetsLevel(proto) ||
        !state.character.MeetsJob(proto)) {
      continue;
    }
    // A piece bought on an earlier pass stays a candidate: left out, the best
    // of the rest would displace it, and the next pass would swap it back.
    if (budget && !state.character.IsWearing(proto.name()) &&
        (!CanPayFor(state, proto) || !BuyOne(state, proto))) {
      continue;
    }
    by_family[BaseSlot(proto.equip_slot())].push_back(&proto);
  }
  for (std::pair<const EquipSlot, std::vector<const EquipPrototype*>>& entry :
       by_family) {
    WearBestOfFamily(state.character, entry.first, entry.second);
  }
}

}  // namespace

void FullyUpgrade(GameState& state, int star_cap) {
  std::map<EquipSlot, EquipPrototype> worn;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped()) {
    worn[entry.first] = entry.second->prototype();
  }
  std::string farming = OpenTryout(state);
  // The character as they arrived, into which the maxed items are written,
  // since every try-on leaves the previous item in the bag.
  Character before = state.character.ToProto();
  for (const std::pair<const EquipSlot, EquipPrototype>& entry : worn) {
    const Scroll* scroll =
        BestScrollForSlot(state, entry.first, /*success_rate=*/0);
    (*before.mutable_equip_presets()
          ->mutable_presets(IndexOf(StatPreset::kFirst))
          ->mutable_equipped())[entry.first] =
        AtCeiling(entry.second, scroll, star_cap);
  }
  state.character.RestoreFrom(before, state.equips, state.items);
  CloseTryout(state, farming);
}

std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate) {
  std::vector<EquipSlot> worn;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped(kBossGear)) {
    worn.push_back(entry.first);
  }
  std::string farming = OpenTryout(state);
  Character before = state.character.ToProto();
  std::map<EquipSlot, const Scroll*> chosen;
  for (EquipSlot slot : worn) {
    const Scroll* scroll = BestScrollForSlot(state, slot, success_rate);
    // Each try-on left the previous item in the bag; restoring from the proto
    // undoes that (see FullyUpgrade).
    state.character.RestoreFrom(before, state.equips, state.items);
    if (scroll != nullptr) {
      chosen[slot] = scroll;
    }
  }
  CloseTryout(state, farming);
  return chosen;
}

void OutfitWeapon(GameState& state, EquipType type) {
  ClimbLadder(state, type, /*budget=*/false);
  EquipType ammo = AmmoFor(type);
  if (ammo != EQUIP_TYPE_UNSPECIFIED) {
    ClimbLadder(state, ammo, /*budget=*/false);
  }
  const EquipPrototype* off_hand = BestSecondary(state, /*budget=*/false);
  if (off_hand != nullptr) {
    WearCopy(state.character, *off_hand);
  }
  BuyAccessories(state, /*budget=*/false);
}

bool ReachedSymbolArea(const CharacterInstance& character,
                       const EquipPrototype& proto) {
  return !IsArcaneSymbol(proto) ||
         character.proto().level() >= proto.arcane_symbol().area_level();
}

int CollectSymbols(CharacterInstance& character) {
  // Collect the slots first, then absorb: absorbing a spare rebuilds the
  // symbol, so nothing should be iterating the worn map at that point.
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       character.equipped()) {
    if (IsSymbol(entry.second->prototype())) {
      slots.push_back(entry.first);
    }
  }
  int taken = 0;
  for (EquipSlot slot : slots) {
    int spares = character.SpareSymbols(slot);
    if (spares > 0) {
      taken += character.CombineSymbols(slot, spares);
    }
  }
  return taken;
}

namespace {

// Whether `gear` wears a copy of `proto` anywhere in its family. Equipping
// another would swap with it, sending the worn one to the bag, where it would
// be offered again forever.
bool WearsCopyOf(const CharacterInstance& character, StatPreset gear,
                 const EquipPrototype& proto) {
  for (EquipSlot slot : SlotFamily(proto.equip_slot())) {
    WornGear::const_iterator worn = character.equipped(gear).find(slot);
    if (worn != character.equipped(gear).end() &&
        worn->second->name() == proto.name()) {
      return true;
    }
  }
  return false;
}

}  // namespace

void WearBestFromBag(CharacterInstance& character) {
  const bool presets =
      character.proto().level() >= UnlockLevel(Feature::kEquipPresets);
  // Restart after every change, since equipping reorders the bag: the displaced
  // item goes back into it.
  bool moved = true;
  while (moved) {
    moved = false;
    const InventoryInstance& bag = character.inventory();
    for (int i = 0; i < bag.size(); ++i) {
      const EquipPrototype& proto = bag[i].prototype();
      const StatPreset gear =
          presets && SplitsFarmGear(proto.equip_slot()) ? kBossGear : kFarmGear;
      // A skill ring outranks nothing by level; WearSkillRings judges it.
      if (bag[i].is_trace() || Shopped(proto) || proto.has_equipment_skill() ||
          !ReachedSymbolArea(character, proto) || !character.CanEquip(proto) ||
          WearsCopyOf(character, gear, proto)) {
        continue;
      }
      WornGear::const_iterator worn =
          character.equipped(gear).find(proto.equip_slot());
      if (worn != character.equipped(gear).end() &&
          worn->second->prototype().required_level() >=
              proto.required_level()) {
        continue;
      }
      const EquipInstance* displaced =
          character.WornAt(gear, character.SlotToFill(proto, gear));
      if (displaced != nullptr &&
          displaced->prototype().has_equipment_skill()) {
        continue;
      }
      if (character.Equip(i, gear)) {
        moved = true;
        break;
      }
    }
  }
}

bool SharesFarmPiece(const CharacterInstance& character, EquipSlot slot) {
  if (character.proto().level() < UnlockLevel(Feature::kEquipPresets) ||
      !SplitsFarmGear(slot)) {
    return false;
  }
  const EquipInstance* worn = character.WornAt(kBossGear, slot);
  return worn != nullptr && worn == character.WornAt(kFarmGear, slot);
}

EquipSlot SplitFarmPiece(CharacterInstance& character, EquipSlot slot) {
  if (!SharesFarmPiece(character, slot)) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  const std::string name = character.WornAt(kFarmGear, slot)->name();
  const InventoryInstance& bag = character.inventory();
  int copy = -1;
  for (int i = 0; i < bag.size() && copy < 0; ++i) {
    if (bag.equip_instance(i) != nullptr && bag[i].prototype().name() == name) {
      copy = i;
    }
  }
  if (copy < 0 || !character.Unequip(slot, kFarmGear)) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  // Unequip appends, so `copy` still names the copy. Farming takes it first and
  // the worn piece follows it into the Boss preset: SlotToFill puts a second
  // copy where the first shows, so a ring lands where the copy did even when
  // an earlier ring slot stood empty.
  const EquipInstance* worn = bag.equip_instance(bag.size() - 1);
  character.Equip(copy, kFarmGear);
  for (int i = 0; i < bag.size(); ++i) {
    if (bag.equip_instance(i) == worn) {
      character.Equip(i, kBossGear);
      break;
    }
  }
  for (EquipSlot at : SlotFamily(slot)) {
    const EquipInstance* farm = character.WornAt(kFarmGear, at);
    if (farm != nullptr && farm->name() == name &&
        farm != character.WornAt(kBossGear, at)) {
      return at;
    }
  }
  return EQUIP_SLOT_UNSPECIFIED;
}

void OutfitDrops(GameState& state, const std::set<std::string>& skip) {
  std::map<EquipSlot, std::vector<const EquipPrototype*>> by_family;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state.equips) {
    const EquipPrototype& proto = entry.second;
    if (Shopped(proto) || skip.count(entry.first) > 0 ||
        !ReachedSymbolArea(state.character, proto) ||
        !state.character.CanEquip(proto)) {
      continue;
    }
    by_family[BaseSlot(proto.equip_slot())].push_back(&proto);
  }
  for (std::pair<const EquipSlot, std::vector<const EquipPrototype*>>& entry :
       by_family) {
    WearBestOfFamily(state.character, entry.first, entry.second);
  }
}

EquipType SettledWeaponType(GameState& state, bool budget) {
  Character before = state.character.ToProto();
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    while (state.character.LearnSkill(entry.second)) {
    }
  }
  EquipType type = MeasureBestType(state, budget);
  state.character.RestoreFrom(before, state.equips, state.items);
  return type;
}

void Outfit(GameState& state, bool budget, EquipType settled) {
  // Nothing is for sale before the shop unlocks, so there's nothing to choose.
  // The character keeps what the game gave them, as an early player does.
  if (!Unlocked(Feature::kShop, state.character, state.account)) {
    return;
  }
  EquipType type = settled == EQUIP_TYPE_UNSPECIFIED
                       ? MeasureBestType(state, budget)
                       : settled;
  if (type == EQUIP_TYPE_UNSPECIFIED) {
    return;
  }
  ClimbLadder(state, type, budget);
  EquipType ammo = AmmoFor(type);
  if (ammo != EQUIP_TYPE_UNSPECIFIED) {
    ClimbLadder(state, ammo, budget);
  }
  // After the weapon, because meso is spent in order of importance: a ring is
  // worth less than the weapon.
  const EquipPrototype* off_hand = BestSecondary(state, budget);
  if (off_hand != nullptr &&
      off_hand->required_level() >
          HeldTier(state.character, EQUIP_SLOT_SECONDARY) &&
      (!budget || BuyOne(state, *off_hand))) {
    WearCopy(state.character, *off_hand);
  }
  BuyAccessories(state, budget);
}

namespace {

// Whether the character owns `name`, worn in either preset or in the bag.
bool Owns(const CharacterInstance& character, const std::string& name) {
  return character.IsWearing(name, kFarmGear) ||
         character.IsWearing(name, kBossGear) || HeldInBag(character, name);
}

int MainStatOf(const EquipStats& stats, StatField field) {
  switch (field) {
    case STAT_FIELD_STR:
      return stats.str();
    case STAT_FIELD_DEX:
      return stats.dex();
    case STAT_FIELD_INT:
      return stats.int_();
    case STAT_FIELD_LUK:
      return stats.luk();
    default:
      return 0;
  }
}

// The piece to take from a token or pick box, or null when it holds nothing
// the character lacks.
const EquipPrototype* BoxPick(const GameState& state,
                              const ItemPrototype& box) {
  const CharacterInstance& character = state.character;
  std::vector<const EquipPrototype*> offered;
  for (const std::string& key : BoxStock(box, state.equips)) {
    const EquipPrototype& piece = state.equips.at(key);
    if (character.MeetsJob(piece)) {
      offered.push_back(&piece);
    }
  }
  const EquipPrototype* best = nullptr;
  if (box.has_pick_box()) {
    // One slot, so owning any of them leaves nothing to gain.
    const StatField main = PrimaryStatField(character.proto().job());
    for (const EquipPrototype* piece : offered) {
      if (Owns(character, piece->name())) {
        return nullptr;
      }
      if (best == nullptr || MainStatOf(piece->base_stats(), main) >
                                 MainStatOf(best->base_stats(), main)) {
        best = piece;
      }
    }
    return best;
  }
  int lowest = 0;
  for (const EquipPrototype* piece : offered) {
    if (Owns(character, piece->name())) {
      continue;
    }
    const EquipInstance* worn =
        character.WornAt(kFarmGear, piece->equip_slot());
    if (piece->equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON &&
        (worn == nullptr ||
         worn->prototype().equip_type() != piece->equip_type())) {
      continue;
    }
    const int tier = worn == nullptr ? 0 : worn->prototype().required_level();
    if (best == nullptr || tier < lowest) {
      best = piece;
      lowest = tier;
    }
  }
  return best;
}

// The best level of `skill`'s ring the character holds anywhere, or 0.
int HeldRingLevel(const CharacterInstance& character,
                  const std::string& skill) {
  int level = 0;
  auto consider = [&](const EquipPrototype& proto) {
    if (proto.has_equipment_skill() &&
        proto.equipment_skill().skill() == skill) {
      level = std::max(level, proto.equipment_skill().level());
    }
  };
  for (StatPreset gear : {kFarmGear, kBossGear}) {
    for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
         character.equipped(gear)) {
      consider(worn.second->prototype());
    }
  }
  const InventoryInstance& bag = character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    if (!bag[i].is_trace()) {
      consider(bag[i].prototype());
    }
  }
  return level;
}

// The ring to ask a ring box for, or "" when no roll could beat what is held.
std::string RingPick(const GameState& state, const RingBox& box) {
  int top = 0;
  for (const RingBox::LevelChance& chance : box.levels()) {
    top = std::max(top, chance.level());
  }
  std::string best;
  int lowest = top;
  bool own_slot = false;
  for (const std::string& skill : box.skills()) {
    const EquipPrototype* ring = RingAt(skill, top, state.equips);
    if (ring == nullptr) {
      continue;
    }
    const int held = HeldRingLevel(state.character, skill);
    const bool alone = ring->equip_slot() != EQUIP_SLOT_RING;
    if (held < lowest ||
        (held == lowest && !best.empty() && alone && !own_slot)) {
      best = skill;
      lowest = held;
      own_slot = alone;
    }
  }
  return best;
}

// Index in the bag of the best ring of `skill` there, or -1.
int BestRingInBag(const CharacterInstance& character,
                  const std::string& skill) {
  int index = -1;
  int level = 0;
  const InventoryInstance& bag = character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    const EquipPrototype& proto = bag[i].prototype();
    if (!bag[i].is_trace() && proto.has_equipment_skill() &&
        proto.equipment_skill().skill() == skill &&
        proto.equipment_skill().level() > level) {
      index = i;
      level = proto.equipment_skill().level();
    }
  }
  return index;
}

// Level of the ring of `skill` that `gear` wears, or 0.
int WornRingLevel(const CharacterInstance& character, StatPreset gear,
                  const std::string& skill) {
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       character.equipped(gear)) {
    const EquipPrototype& proto = worn.second->prototype();
    if (proto.has_equipment_skill() &&
        proto.equipment_skill().skill() == skill) {
      return proto.equipment_skill().level();
    }
  }
  return 0;
}

constexpr char kRestraint[] = "Ring of Restraint";
constexpr char kContinuous[] = "Continuous Ring";

}  // namespace

int OpenBoxes(GameState& state) {
  int opened = 0;
  bool again = true;
  while (again) {
    again = false;
    for (const StackableItem& stack : state.character.stackables()) {
      const ItemPrototype box = stack.prototype();
      if (box.has_ring_box()) {
        const std::string skill = RingPick(state, box.ring_box());
        if (!skill.empty() &&
            state.character.OpenRingBox(box, skill, state.equips) != nullptr) {
          again = true;
        }
      } else if (box.has_box() || box.has_pick_box()) {
        const EquipPrototype* pick = BoxPick(state, box);
        if (pick != nullptr && state.character.OpenBox(box, *pick)) {
          again = true;
        }
      }
      if (again) {
        ++opened;
        break;  // opening may have emptied the stack the loop stands on
      }
    }
  }
  return opened;
}

void WearSkillRings(GameState& state,
                    const std::function<double(GameState&)>& power,
                    std::string* memo) {
  CharacterInstance& character = state.character;
  int cont = BestRingInBag(character, kContinuous);
  if (cont >= 0 &&
      character.inventory()[cont].prototype().equipment_skill().level() >
          WornRingLevel(character, kFarmGear, kContinuous)) {
    character.Equip(cont, kFarmGear);
  }
  const bool presets =
      character.proto().level() >= UnlockLevel(Feature::kEquipPresets);
  const StatPreset gear = presets ? kBossGear : kFarmGear;
  int restraint = BestRingInBag(character, kRestraint);
  if (restraint < 0) {
    return;
  }
  const int held =
      character.inventory()[restraint].prototype().equipment_skill().level();
  const int worn = WornRingLevel(character, gear, kRestraint);
  if (held <= worn) {
    return;
  }
  // A better copy simply replaces the worn one, which SlotToFill finds.
  if (worn > 0) {
    character.Equip(restraint, gear);
    return;
  }
  std::string key = std::to_string(held);
  for (EquipSlot slot : SlotFamily(EQUIP_SLOT_RING)) {
    const EquipInstance* ring = character.WornAt(gear, slot);
    key += "," + (ring == nullptr ? std::string() : ring->name());
  }
  if (*memo == key) {
    return;
  }
  *memo = key;
  const std::string name = character.inventory()[restraint].prototype().name();
  const Character before = character.ToProto();
  const double base = power(state);
  double best = base;
  EquipSlot best_slot = EQUIP_SLOT_UNSPECIFIED;
  for (EquipSlot slot : SlotFamily(EQUIP_SLOT_RING)) {
    if (!character.Equip(BestRingInBag(character, kRestraint), gear, slot)) {
      continue;
    }
    const double tried = power(state);
    if (tried > best) {
      best = tried;
      best_slot = slot;
    }
    character.RestoreFrom(before, state.equips, state.items);
  }
  if (best_slot != EQUIP_SLOT_UNSPECIFIED) {
    character.Equip(BestRingInBag(character, kRestraint), gear, best_slot);
  }
}

int UseDroppedScrolls(GameState& state) {
  CharacterInstance& character = state.character;
  const bool magic =
      PrimaryStatField(character.proto().job()) == STAT_FIELD_INT;
  std::vector<const Scroll*> scrolls;
  for (const std::pair<const std::string, Scroll>& entry : state.scrolls) {
    const Scroll& scroll = entry.second;
    // One that raises only the other attack does this job no good.
    const int raise =
        magic ? scroll.stats().magic_attack() : scroll.stats().attack();
    if (!scroll.paid_with().empty() && raise > 0) {
      scrolls.push_back(&scroll);
    }
  }
  std::sort(scrolls.begin(), scrolls.end(),
            [magic](const Scroll* a, const Scroll* b) {
              return (magic ? a->stats().magic_attack() : a->stats().attack()) >
                     (magic ? b->stats().magic_attack() : b->stats().attack());
            });
  int used = 0;
  for (const Scroll* scroll : scrolls) {
    std::set<int> categories(scroll->applicable_job_categories().begin(),
                             scroll->applicable_job_categories().end());
    std::vector<EquipSlot> slots;
    for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
         character.equipped(kBossGear)) {
      const EquipPrototype& proto = worn.second->prototype();
      bool fits = false;
      for (int category : proto.equip_job_categories()) {
        fits = fits || categories.count(category) > 0;
      }
      if (fits && TargetForSlot(proto.equip_slot()) == scroll->target() &&
          Supports(proto, UPGRADE_SCROLL)) {
        slots.push_back(worn.first);
      }
    }
    for (EquipSlot slot : slots) {
      while (character.CountItem(scroll->paid_with()) > 0) {
        const EquipInstance* item = character.WornAt(kBossGear, slot);
        if (item == nullptr ||
            item->equip_state().remaining_upgrade_slots() <= 0 ||
            !character.SpendItem(scroll->paid_with(), 1)) {
          break;
        }
        character.ScrollEquipped(slot, *scroll, kBossGear);
        ++used;
      }
    }
  }
  return used;
}

}  // namespace ms
