#include "analysis/sim_gear.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/progression.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/shop.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// The map and mob a weapon is tried out on, invented rather than taken from
// the catalog: a real map would let its crowd answer a question about one
// weapon against one mob, and the real maps stop below the trial cap anyway.
constexpr char kTryoutMap[] = "__sim_gear_tryout";
constexpr char kTryoutMob[] = "__sim_gear_tryout_mob";

// How long each candidate is swung for. Long enough that a four-second
// cooldown lands a dozen times, which is all the settling the comparison
// needs -- weapon_sim's own horizon is ten times this because it prints the
// number, where this only ranks with it.
constexpr double kTryoutSeconds = 60.0;

// What a weapon draws from, or unspecified for the ones that draw from
// nothing. The claw is the only weapon in the game whose damage lives in what
// it throws rather than in itself, so it is the only one that shops twice.
EquipType AmmoFor(EquipType weapon) {
  return weapon == EQUIP_TYPE_CLAW ? EQUIP_TYPE_THROWING_STAR
                                   : EQUIP_TYPE_UNSPECIFIED;
}

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

// The best rung of `type` the character can reach, or null when the shop
// stocks none they can hold. `budget` also asks whether they can pay.
const EquipPrototype* BestRung(const GameState& state, EquipType type,
                               bool budget) {
  const EquipPrototype* best = nullptr;
  for (const std::string& key : ShopWeaponStock(state.equips)) {
    const EquipPrototype& proto = state.equips.at(key);
    if (proto.equip_type() != type || !state.character.CanEquip(proto)) {
      continue;
    }
    if (budget && proto.shop_price() > state.character.meso()) {
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
  for (const std::string& key : ShopWeaponStock(state.equips)) {
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

// What the character now hits for a second against a lone mob of their own
// level: their swings, plus anything of theirs on a clock of its own.
double MeasureRate(GameState& state) {
  CombatParams params = ComputeCombatParams(state);
  Sequence played = PlaySwings(params, kTryoutSeconds);
  double rate = played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
  for (const AttackOption& extra : params.auto_attacks) {
    if (!extra.damage_per_hit.empty() && extra.interval_seconds > 0.0) {
      rate += extra.damage_per_hit[0] / extra.interval_seconds;
    }
  }
  return rate;
}

}  // namespace

double SoloDamage(const AttackOption& attack) {
  if (attack.damage_per_hit.empty()) {
    return 0.0;
  }
  double damage = attack.damage_per_hit[0];
  if (!attack.final_attack_damage.empty()) {
    damage += attack.final_attack_damage[0];
  }
  if (attack.empowered != nullptr && attack.empowered_every > 0) {
    damage += (SoloDamage(*attack.empowered) - damage) / attack.empowered_every;
  }
  return damage;
}

Sequence PlaySwings(const CombatParams& params, double horizon) {
  constexpr double kStep = 0.01;
  std::vector<double> cooldown(params.attacks.size(), 0.0);
  std::vector<int> swings(params.attacks.size(), 0);
  Sequence played;
  double phase = 0.0;
  int pick = -1;  // the swing being wound up, held until it lands
  for (double elapsed = 0.0; elapsed < horizon; elapsed += kStep) {
    for (double& left : cooldown) {
      left = std::max(0.0, left - kStep);
    }
    // Index 0 is the bare poke, which is never committed to.
    if (pick <= 0) {
      pick = -1;
      double best_rate = -1.0;
      for (int i = 0; i < static_cast<int>(params.attacks.size()); ++i) {
        const AttackOption& attack = params.attacks[i];
        if (attack.swing_seconds <= 0.0 || cooldown[i] > 0.0 ||
            attack.heal_fraction > 0.0) {
          continue;  // a cast is not one of the swings being compared
        }
        double rate = SoloDamage(attack) / attack.swing_seconds;
        if (rate > best_rate) {
          best_rate = rate;
          pick = i;
        }
      }
    }
    if (pick < 0) {
      break;
    }
    phase += kStep;
    if (phase < params.attacks[pick].swing_seconds) {
      continue;
    }
    phase -= params.attacks[pick].swing_seconds;
    played.damage += SoloDamage(params.attacks[pick]);
    played.seconds += params.attacks[pick].swing_seconds;
    ++swings[pick];
    cooldown[pick] = params.attacks[pick].cooldown_seconds;
    pick = -1;
  }
  for (int i = 0; i < static_cast<int>(swings.size()); ++i) {
    if (played.main_attack < 0 || swings[i] > swings[played.main_attack]) {
      played.main_attack = i;
    }
  }
  return played;
}

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
// ladder's top rung is tried on and swung at a mob of their own level, and
// only that: within a type the tiers climb, so which rung is not in question.
//
// Unspecified when the shelf holds nothing they can wear or the bag is too
// full to try anything on -- in either case they keep what they hold.
EquipType MeasureBestType(GameState& state, bool budget) {
  std::vector<const EquipPrototype*> ladders = Ladders(state, budget);
  if (ladders.empty() ||
      state.character.inventory().room() < static_cast<int>(ladders.size())) {
    return EQUIP_TYPE_UNSPECIFIED;
  }
  std::string held = HeldWeaponName(state.character);

  // A mob of the character's own level, so the level multiplier lands where a
  // player fighting their own tier would put it. Torn down afterwards: it is
  // scratch, not one of the game's.
  Mob mob;
  mob.set_name("Tryout");
  mob.set_level(state.character.proto().level());
  mob.set_max_hp(1);
  state.mobs[kTryoutMob] = mob;
  MapData map;
  map.set_name("Tryout");
  MapData::Spawn* spawn = map.add_spawns();
  spawn->set_mob(kTryoutMob);
  spawn->set_count(1);
  state.maps[kTryoutMap] = map;
  std::string farming = state.current_map;
  state.current_map = kTryoutMap;

  EquipType winner = EQUIP_TYPE_UNSPECIFIED;
  double best_rate = 0.0;
  for (const EquipPrototype* candidate : ladders) {
    if (!WearCopy(state.character, *candidate)) {
      continue;
    }
    // A claw with an empty star slot swings for nothing, and a dagger wearing
    // stars would be credited with ammunition it never throws. Either way the
    // candidate is measured holding exactly what it draws from.
    state.character.Unequip(EQUIP_SLOT_STARS);
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

  state.current_map = farming;
  state.maps.erase(kTryoutMap);
  state.mobs.erase(kTryoutMob);
  // Everything tried on comes back off. What the character wears is what they
  // paid for, which is Buy's business below.
  state.character.Unequip(EQUIP_SLOT_STARS);
  EquipByName(state.character, held);
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
  if (budget && !state.character.Buy(*best, 1)) {
    return;
  }
  WearCopy(state.character, *best);
}

// The best off-hand the shop will sell the character. Nothing is measured
// here: one branch owns each off-hand and they carry plain stats, so there is
// no choice to make -- only a tier to reach.
const EquipPrototype* BestSecondary(const GameState& state, bool budget) {
  const EquipPrototype* best = nullptr;
  for (const std::string& key : ShopSecondaryStock(state.equips)) {
    const EquipPrototype& proto = state.equips.at(key);
    // The shop's own filter rather than CanEquip, which asks only the job
    // category -- and the three warrior off-hands are not interchangeable.
    if (!state.character.MeetsLevel(proto) ||
        !state.character.MeetsJob(proto)) {
      continue;
    }
    if (budget && proto.shop_price() > state.character.meso()) {
      continue;
    }
    if (best == nullptr || proto.required_level() > best->required_level()) {
      best = &proto;
    }
  }
  return best;
}

}  // namespace

void Outfit(GameState& state, bool budget) {
  // Nothing is for sale before the shop opens, so there is nothing to choose
  // between and nothing to climb -- the character keeps what the game gave
  // them, as a player that early does.
  if (!Unlocked(Feature::kShop, state.character)) {
    return;
  }
  EquipType type = MeasureBestType(state, budget);
  if (type == EQUIP_TYPE_UNSPECIFIED) {
    return;
  }
  ClimbLadder(state, type, budget);
  EquipType ammo = AmmoFor(type);
  if (ammo != EQUIP_TYPE_UNSPECIFIED) {
    ClimbLadder(state, ammo, budget);
  }
  // Last, because it is the smallest of the three and the purse is spent in
  // the order that matters.
  const EquipPrototype* off_hand = BestSecondary(state, budget);
  if (off_hand == nullptr ||
      off_hand->required_level() <=
          HeldTier(state.character, EQUIP_SLOT_SECONDARY)) {
    return;
  }
  if (budget && !state.character.Buy(*off_hand, 1)) {
    return;
  }
  WearCopy(state.character, *off_hand);
}

}  // namespace ms
