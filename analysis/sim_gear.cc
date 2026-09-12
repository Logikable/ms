#include "analysis/sim_gear.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/progression.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/projectile.h"
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

// The map and mob a weapon is tried out on, invented rather than taken from
// the catalog: a real map would let its crowd answer a question meant for one
// weapon against one mob.
constexpr char kTryoutMap[] = "__sim_gear_tryout";
constexpr char kTryoutMob[] = "__sim_gear_tryout_mob";

// How long each candidate is swung for, in the game's own seconds. Long enough
// that a four-second cooldown lands a dozen times, which is all the settling
// the comparison needs -- weapon_sim's own horizon is ten times this because
// it prints the number, where this only ranks with it.
constexpr double kTryoutSeconds = 60.0;

// The required level of what is worn in `slot`, which is how one rung is
// ranked against another: the shop's ladder is ordered by it.
int HeldTier(const CharacterInstance& character, EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      character.equipped().find(slot);
  return it == character.equipped().end()
             ? 0
             : it->second.prototype().required_level();
}

// Puts `proto` on without charging for it, which is what trying a weapon out
// costs a player. A copy already in the bag is worn again rather than
// duplicated: the bag is finite and this runs at every level.
bool WearCopy(CharacterInstance& character, const EquipPrototype& proto) {
  if (EquipByName(character, proto.name())) {
    return true;
  }
  if (!character.PickUp(std::make_unique<EquipInstance>(proto))) {
    return false;
  }
  return character.Equip(character.inventory().size() - 1);
}

// Both shelves of one stock list, one after the other. What a token buys is
// stock like any other here: the sims play the fights that drop the tokens, so
// the Frozen tier is as reachable to them as it is to a player.
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

// The off-hands alone. The shelf also carries the rings, the emblem and the
// medal, which fill slots of their own and are not what a branch re-arms its
// hand with.
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

// The token `proto` is priced in, or null for one the shop takes meso for.
const ItemPrototype* TokenFor(const GameState& state,
                              const EquipPrototype& proto) {
  if (proto.token_price() <= 0) {
    return nullptr;
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(proto.token_item());
  return it == state.items.end() ? nullptr : &it->second;
}

// Whether the character can pay for `proto` as they stand. A token price is
// met out of what the fights dropped, so this asks the Etc tab rather than the
// purse.
bool CanPayFor(const GameState& state, const EquipPrototype& proto) {
  const ItemPrototype* token = TokenFor(state, proto);
  if (token == nullptr) {
    return proto.shop_price() <= state.character.meso();
  }
  return proto.token_price() <= state.character.CountStackable(token->name());
}

// Pays for one of `proto`, in whichever currency it is priced in.
bool BuyOne(GameState& state, const EquipPrototype& proto) {
  const ItemPrototype* token = TokenFor(state, proto);
  return token == nullptr ? state.character.Buy(proto, 1)
                          : state.character.BuyWithToken(proto, *token, 1);
}

// The best rung of `type` the character can reach, or null when the shop
// stocks none they can hold. `budget` also asks whether they can pay.
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

// The top rung of every weapon ladder the character can hold. Only the top of
// each is worth trying: within a type the tiers only climb, so the question
// left to measure is which ladder, not which rung.
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

// Stands the tryout map and its mob up, and moves the character onto it.
// The mob is their own level, so the level multiplier lands where a player
// fighting their own tier would put it, and carries the measurement's own HP
// so a held swing is not let go early. Returns the map they came from.
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

// Tears it down again. It is scratch, not one of the game's.
void CloseTryout(GameState& state, const std::string& farming) {
  state.current_map = farming;
  state.maps.erase(kTryoutMap);
  state.mobs.erase(kTryoutMob);
}

// What the character now hits for a second against a lone mob of their own
// level: their swings, plus anything of theirs on a clock of its own.
double MeasureRate(GameState& state) {
  CombatParams params = ComputeCombatParams(state);
  // Stretched on the way in, since MeasureFight counts in that clock -- see
  // its header. A minute here was six game seconds at level 200.
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
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  return it == character.equipped().end() ? "-" : it->second.name();
}

namespace {

// Which weapon type the character hits hardest with as they now stand. Every
// ladder's top rung is tried on and swung at a mob of their own level, along
// with the weapon they already hold, and only those: within a type the tiers
// climb, so which rung is not in question.
//
// Unspecified when the shelf holds nothing they can wear or the bag is too
// full to try anything on -- in either case they keep what they hold.
EquipType MeasureBestType(GameState& state, bool budget) {
  std::vector<const EquipPrototype*> ladders = Ladders(state, budget);
  if (ladders.empty() ||
      state.character.inventory().room() < static_cast<int>(ladders.size())) {
    return EQUIP_TYPE_UNSPECIFIED;
  }
  // The character as they arrived. Every try-on wears a fresh copy and leaves
  // the one it displaced in the bag, and restoring from the proto is the only
  // eraser there is -- see FullyUpgrade. Without it a sweep that measures at
  // every level fills the bag with the weapons it did not buy, and the guard
  // above then quietly stops the character shopping for good.
  Character before = state.character.ToProto();

  // The weapon already in their hands is a candidate too, and the first one,
  // so a tie keeps it. Without this the shelf can talk them out of a weapon
  // better than anything on it -- a Frozen weapon has no second token behind
  // it, and a purse just spent leaves only the cheap rungs on offer.
  EquipPrototype worn;
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      state.character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it != state.character.equipped().end()) {
    worn = it->second.prototype();
    ladders.insert(ladders.begin(), &worn);
  }

  std::string farming = OpenTryout(state);

  EquipType winner = EQUIP_TYPE_UNSPECIFIED;
  double best_rate = 0.0;
  for (const EquipPrototype* candidate : ladders) {
    if (!WearCopy(state.character, *candidate)) {
      continue;
    }
    // A claw with an empty projectile slot swings for nothing, and a dagger
    // wearing stars would be credited with ammunition it never throws. Either
    // way the candidate is measured holding exactly what it draws from.
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
  // Everything tried on goes with it. What the character wears is what they
  // paid for, which is Buy's business below.
  state.character.RestoreFrom(before, state.equips, state.items);
  return winner;
}

// Buys and wears the best rung of `type` within reach. Nothing happens when
// the character already holds that ladder no lower down -- but a character on
// the wrong ladder swaps whatever the rungs say, because the measurement has
// just told them the type itself is worth more than the tier.
void ClimbLadder(GameState& state, EquipType type, bool budget) {
  const EquipPrototype* best = BestRung(state, type, budget);
  if (best == nullptr) {
    return;
  }
  std::map<EquipSlot, EquipInstance>::const_iterator held =
      state.character.equipped().find(best->equip_slot());
  bool right_ladder = held != state.character.equipped().end() &&
                      held->second.prototype().equip_type() == type;
  if (right_ladder &&
      best->required_level() <= held->second.prototype().required_level()) {
    return;
  }
  if (budget && !BuyOne(state, *best)) {
    return;
  }
  WearCopy(state.character, *best);
}

// The best off-hand the shop will sell the character. Nothing is measured
// here: one branch owns each off-hand and they carry plain stats, so there is
// no choice to make -- only a tier to reach.
const EquipPrototype* BestSecondary(const GameState& state, bool budget) {
  const EquipPrototype* best = nullptr;
  for (const std::string& key : SecondaryShelf(state)) {
    const EquipPrototype& proto = state.equips.at(key);
    // The shop's own filter rather than CanEquip, which asks only the job
    // category -- and the three warrior off-hands are not interchangeable.
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

// The scrolls `proto` takes: the tier its level falls in, the kind of
// equipment it is, and a job category both sides name. The scroll panel's own
// three rules, less the clean slate, which undoes rather than upgrades.
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
        scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
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

// The golden hammers a character at `level` could have driven in. None below
// the gate: a ceiling is what the player could reach, not what the item could
// hold.
int HammersAt(int level) {
  return level >= UnlockLevel(Feature::kHammer) ? kMaxHammers : 0;
}

// `proto` as a player who kept at it would leave it: `hammers` driven in,
// every slot spent on `scroll` and every star up to `star_cap` landed. A null
// scroll leaves the slots unspent, which is what an item taking none gets.
Equip AtCeiling(const EquipPrototype& proto, const Scroll* scroll, int star_cap,
                int hammers) {
  Equip state;
  state.set_equip_name(proto.name());
  if (TakesUpgradeSlots(proto)) {
    state.set_hammers(hammers);
  }
  int slots = TotalUpgradeSlots(proto, state);
  if (scroll == nullptr) {
    state.set_remaining_upgrade_slots(slots);
  } else {
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

// Wears a fresh `proto` carrying `made` in `slot`, in place of whatever that
// slot holds. The displaced copy stays in the bag, which is where a try-on
// goes.
//
// `slot` is where the item is WORN, which for a family is not the slot its
// prototype names: every ring's prototype says EQUIP_SLOT_RING, and stripping
// that one to try on the ring worn in RING_3 takes off the wrong ring and
// leaves the copy refused for a duplicate of one still on.
bool WearMade(CharacterInstance& character, EquipSlot slot,
              const EquipPrototype& proto, const Equip& made) {
  character.Unequip(slot);
  if (!character.PickUp(std::make_unique<EquipInstance>(proto, made))) {
    return false;
  }
  return character.Equip(character.inventory().size() - 1);
}

// Which scroll `slot` wants: the one the character measures best in, wearing
// none included and first, so an item no scroll helps keeps its slots.
//
// `success_rate` narrows the field to the scrolls that land that often; 0
// takes them all. A budgeted player picks a rate before they pick a stat --
// a 30% trace wastes seven slots out of ten on a piece one boss drops.
//
// Leaves the character wearing the last thing tried and the bag holding the
// try-ons. The caller puts both back.
const Scroll* BestScrollForSlot(GameState& state, EquipSlot slot,
                                int success_rate) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      state.character.equipped().find(slot);
  if (it == state.character.equipped().end()) {
    return nullptr;
  }
  EquipPrototype proto = it->second.prototype();
  std::vector<const Scroll*> candidates;
  for (const Scroll* scroll : ScrollsFor(state, proto)) {
    if (success_rate == 0 || scroll->success_rate() == success_rate) {
      candidates.push_back(scroll);
    }
  }
  // Wearing none is a candidate only where the slots are worth keeping. An
  // item that takes stars has to spend every slot before it can hold one, so
  // declining a scroll worth almost nothing there costs it the stars as well
  // -- and the stars are worth more than any scroll on the item.
  if (!Supports(proto, UPGRADE_STAR_FORCE)) {
    candidates.insert(candidates.begin(), nullptr);
  }
  const Scroll* best = nullptr;
  double best_rate = -1.0;
  for (const Scroll* candidate : candidates) {
    if (!WearMade(state.character, slot, proto,
                  AtCeiling(proto, candidate, kMaxStarForce,
                            HammersAt(state.character.proto().level())))) {
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

// True for something Outfit already shopped for. Two questions rather than
// one: the three slots it climbs a ladder in, and any item that names a price,
// since the shop's equipment shelf reaches slots the drops reach too -- a ring
// is bought and the Frozen gloves fall. Asking only the slot would hand a
// bought ring over free; asking only the price would hand over the Fafnir,
// which is priced at nothing because nothing sells it.
bool Shopped(const EquipPrototype& proto) {
  EquipSlot slot = proto.equip_slot();
  return slot == EQUIP_SLOT_PRIMARY_WEAPON || slot == EQUIP_SLOT_SECONDARY ||
         slot == EQUIP_SLOT_PROJECTILE || proto.has_shop_price() ||
         proto.token_price() > 0;
}

// Wears the best of `candidates` in every slot of one family, highest tier
// first and the name breaking a tie so a run repeats. A list rather than one
// item because a character wears four rings, and "the best ring" is then four
// answers -- SlotToFill refuses a second copy of one, so the four differ.
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

// The rest of the shop's equipment shelf: the rings, the emblem and the medal.
// Nothing is measured, for the reason the off-hand is not -- they carry plain
// stats, so a higher tier is simply more -- and a family takes as many as it
// holds rather than one.
void BuyAccessories(GameState& state, bool budget) {
  std::map<EquipSlot, std::vector<const EquipPrototype*>> by_family;
  for (const std::string& key : EquipShelf(state)) {
    const EquipPrototype& proto = state.equips.at(key);
    if (proto.equip_slot() == EQUIP_SLOT_SECONDARY ||
        !state.character.MeetsLevel(proto) ||
        !state.character.MeetsJob(proto)) {
      continue;
    }
    if (state.character.IsWearing(proto.name())) {
      continue;  // bought on an earlier pass; a climb outfits at every level
    }
    if (budget && (!CanPayFor(state, proto) || !BuyOne(state, proto))) {
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
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    worn[entry.first] = entry.second.prototype();
  }
  std::string farming = OpenTryout(state);
  // The character as they arrived, which is what the ceilings are written
  // into. Trying a scroll on means wearing it, and every try-on leaves its
  // predecessor in the bag -- a sim that upgrades at every level would fill
  // the bag with them. Restoring from the proto is the only eraser there is.
  Character before = state.character.ToProto();
  int hammers = HammersAt(state.character.proto().level());
  for (const std::pair<const EquipSlot, EquipPrototype>& entry : worn) {
    const Scroll* scroll =
        BestScrollForSlot(state, entry.first, /*success_rate=*/0);
    (*before.mutable_equipped())[entry.first] =
        AtCeiling(entry.second, scroll, star_cap, hammers);
  }
  state.character.RestoreFrom(before, state.equips, state.items);
  CloseTryout(state, farming);
}

std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate) {
  std::vector<EquipSlot> worn;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    worn.push_back(entry.first);
  }
  std::string farming = OpenTryout(state);
  Character before = state.character.ToProto();
  std::map<EquipSlot, const Scroll*> chosen;
  for (EquipSlot slot : worn) {
    const Scroll* scroll = BestScrollForSlot(state, slot, success_rate);
    // Every try-on left its predecessor in the bag, and restoring from the
    // proto is the only eraser there is -- see FullyUpgrade.
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

// Whether the character has reached the map that hands `proto` over. A symbol
// is not a drop off a ladder: one waits at each Arcane River checkpoint, so a
// character standing at 200 has the first of the six and none of the rest.
bool ReachedSymbolArea(const CharacterInstance& character,
                       const EquipPrototype& proto) {
  return !IsArcaneSymbol(proto) ||
         character.proto().level() >= proto.arcane_symbol().area_level();
}

void WearBestFromBag(CharacterInstance& character) {
  // Restarted after every change: equipping shuffles the bag, since whatever
  // is displaced goes back into it.
  bool moved = true;
  while (moved) {
    moved = false;
    const InventoryInstance& bag = character.inventory();
    for (int i = 0; i < bag.size(); ++i) {
      const EquipPrototype& proto = bag[i].prototype();
      if (bag[i].is_trace() || Shopped(proto) ||
          !ReachedSymbolArea(character, proto) || !character.CanEquip(proto)) {
        continue;
      }
      std::map<EquipSlot, EquipInstance>::const_iterator worn =
          character.equipped().find(proto.equip_slot());
      if (worn != character.equipped().end() &&
          worn->second.prototype().required_level() >= proto.required_level()) {
        continue;
      }
      if (character.Equip(i)) {
        moved = true;
        break;
      }
    }
  }
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
  // Nothing is for sale before the shop opens, so there is nothing to choose
  // between and nothing to climb -- the character keeps what the game gave
  // them, as a player that early does.
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
  // After the weapon, because the purse is spent in the order that matters:
  // a ring is worth less than the thing it is swung beside.
  const EquipPrototype* off_hand = BestSecondary(state, budget);
  if (off_hand != nullptr &&
      off_hand->required_level() >
          HeldTier(state.character, EQUIP_SLOT_SECONDARY) &&
      (!budget || BuyOne(state, *off_hand))) {
    WearCopy(state.character, *off_hand);
  }
  BuyAccessories(state, budget);
}

}  // namespace ms
