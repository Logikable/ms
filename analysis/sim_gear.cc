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
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// The map and mob a weapon is tried out on, invented rather than taken from
// the catalog: a real map would let its crowd answer a question meant for one
// weapon against one mob.
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

std::vector<std::string> SecondaryShelf(const GameState& state) {
  return BothShelves(ShopSecondaryStock(state.equips, kPaidInMeso),
                     ShopSecondaryStock(state.equips, kPaidInTokens));
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
  return proto.token_price() <=
         state.character.CountStackable(ITEM_CATEGORY_ETC, token->name());
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

// What the character now hits for a second against a lone mob of their own
// level: their swings, plus anything of theirs on a clock of its own.
double MeasureRate(GameState& state) {
  CombatParams params = ComputeCombatParams(state);
  Sequence played = PlaySwings(params, kTryoutSeconds);
  double rate = played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
  return rate + OffClockRate(params, played, 1.0);
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
    // A form that marks enemies rides on top of the strike that sets it off,
    // where one counted on the swing stands in for the swing. The same two
    // readings CombatSim::SwingDamage takes, against the one mob measured here.
    damage +=
        attack.brands_enemies
            ? SoloDamage(*attack.empowered) / attack.empowered_every
            : (SoloDamage(*attack.empowered) - damage) / attack.empowered_every;
  }
  return damage;
}

namespace {

// The buff clocks a run of swings carries, mirroring CombatSim's: seconds each
// buff has left standing, seconds until it can go up again, and which of them
// are up right now as the mask CombatParams indexes its tables by.
struct BuffClocks {
  std::vector<double> left;
  std::vector<double> cooldown;
  std::vector<double> standing;  // seconds each has spent up over the run
  int mask = 0;
};

// Winds every buff's clocks on by one step and puts up any that has come round
// on its own. A buff its own swing lays is left alone here -- see LayBuff.
void RunBuffClocks(const CombatParams& params, double step, BuffClocks& c) {
  c.left.resize(params.buffs.size(), 0.0);
  c.cooldown.resize(params.buffs.size(), 0.0);
  c.standing.resize(params.buffs.size(), 0.0);
  c.mask = 0;
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    c.left[i] = std::max(0.0, c.left[i] - step);
    c.cooldown[i] = std::max(0.0, c.cooldown[i] - step);
    if (buff.laid_by_attack < 0 && c.left[i] <= 0.0 && c.cooldown[i] <= 0.0 &&
        buff.duration_seconds > 0.0) {
      c.left[i] = buff.duration_seconds;
      c.cooldown[i] = buff.cooldown_seconds;
    }
    if (c.left[i] > 0.0) {
      c.mask |= 1 << i;
      c.standing[i] += step;
    }
  }
}

// Puts up every buff the swing at `swung` lays.
void LayBuff(const CombatParams& params, int swung, BuffClocks& c) {
  for (int i = 0; i < static_cast<int>(c.left.size()); ++i) {
    if (params.buffs[i].laid_by_attack == swung) {
      c.left[i] = params.buffs[i].duration_seconds;
      c.cooldown[i] = params.buffs[i].cooldown_seconds;
    }
  }
}

// The swing that lays a lapsed buff, ahead of the hardest one on offer -- the
// same rule CombatSim::BuffToLay follows, and for the same reason.
int SwingToLay(const CombatParams& params, const BuffClocks& c,
               const std::vector<double>& cooldown) {
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    if (buff.laid_by_attack < 0 || buff.duration_seconds <= 0.0 ||
        c.left[i] > 0.0 || cooldown[buff.laid_by_attack] > 0.0) {
      continue;
    }
    return buff.laid_by_attack;
  }
  return -1;
}

// The swing landing the most per second of the ones off cooldown, or -1 when
// none is. A cast is not among them: it deals no damage.
int BestSwing(const std::vector<AttackOption>& attacks,
              const std::vector<double>& cooldown) {
  int pick = -1;
  double best_rate = -1.0;
  for (int i = 0; i < static_cast<int>(attacks.size()); ++i) {
    const AttackOption& attack = attacks[i];
    if (attack.swing_seconds <= 0.0 || cooldown[i] > 0.0 ||
        attack.heal_fraction > 0.0) {
      continue;
    }
    double rate = SoloDamage(attack) / attack.swing_seconds;
    if (rate > best_rate) {
      best_rate = rate;
      pick = i;
    }
  }
  return pick;
}

}  // namespace

Sequence PlaySwings(const CombatParams& params, double horizon) {
  constexpr double kStep = 0.01;
  std::vector<double> cooldown(params.attacks.size(), 0.0);
  std::vector<int> swings(params.attacks.size(), 0);
  BuffClocks clocks;
  Sequence played;
  double phase = 0.0;
  int pick = -1;  // the swing being wound up, held until it lands
  for (double elapsed = 0.0; elapsed < horizon; elapsed += kStep) {
    RunBuffClocks(params, kStep, clocks);
    for (double& left : cooldown) {
      left = std::max(0.0, left - kStep);
    }
    // Index 0 is the bare poke, which is never committed to.
    if (pick <= 0) {
      pick = SwingToLay(params, clocks, cooldown);
      if (pick < 0) {
        pick = BestSwing(params.Attacks(clocks.mask), cooldown);
      }
    }
    if (pick < 0) {
      break;
    }
    // Read off the table for the buffs standing now: the same swing is worth
    // more under a buff, and which one it is was settled when it was aimed.
    const AttackOption& swung = params.Attacks(clocks.mask)[pick];
    phase += kStep;
    if (phase < swung.swing_seconds) {
      continue;
    }
    phase -= swung.swing_seconds;
    played.damage += SoloDamage(swung);
    played.seconds += swung.swing_seconds;
    ++swings[pick];
    cooldown[pick] = swung.cooldown_seconds;
    LayBuff(params, pick, clocks);
    pick = -1;
  }
  for (int i = 0; i < static_cast<int>(swings.size()); ++i) {
    if (played.main_attack < 0 || swings[i] > swings[played.main_attack]) {
      played.main_attack = i;
    }
  }
  for (double standing : clocks.standing) {
    played.buff_uptime.push_back(horizon > 0.0 ? standing / horizon : 0.0);
  }
  return played;
}

double OffClockRate(const CombatParams& params, const Sequence& played,
                    double speed) {
  double rate = 0.0;
  for (int i = 0; i < static_cast<int>(params.auto_attacks.size()); ++i) {
    int gate = params.auto_attacks[i].needs_buff;
    // Priced where it fires: a gated pulse off the table with its own buff up,
    // an ungated one off the character as they stand.
    const AttackOption& extra =
        gate >= 0 ? params.AutoAttacks(1 << gate)[i] : params.auto_attacks[i];
    if (extra.damage_per_hit.empty() || extra.interval_seconds <= 0.0) {
      continue;
    }
    double per_pulse = extra.damage_per_hit[0];
    if (extra.empowered != nullptr && extra.empowered_every > 0 &&
        !extra.empowered->damage_per_hit.empty()) {
      // Marking, the form rides the pulse that set it off rather than standing
      // in for one. No summon marks today, but the two readings differ and the
      // averaging has to pick the right one.
      per_pulse +=
          extra.brands_enemies
              ? extra.empowered->damage_per_hit[0] / extra.empowered_every
              : (extra.empowered->damage_per_hit[0] - per_pulse) /
                    extra.empowered_every;
    }
    double share =
        gate >= 0 && gate < static_cast<int>(played.buff_uptime.size())
            ? played.buff_uptime[gate]
            : 1.0;
    rate += share * per_pulse / (extra.interval_seconds / speed);
  }
  return rate;
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
  std::string held = HeldWeaponName(state.character);

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
  if (budget && !BuyOne(state, *off_hand)) {
    return;
  }
  WearCopy(state.character, *off_hand);
}

}  // namespace ms
