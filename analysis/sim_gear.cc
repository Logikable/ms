#include "analysis/sim_gear.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/progression.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
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

// How long each candidate is swung for. Long enough that a four-second
// cooldown lands a dozen times, which is all the settling the comparison
// needs -- weapon_sim's own horizon is ten times this because it prints the
// number, where this only ranks with it.
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

// Stands the tryout map and its mob up, and moves the character onto it.
// The mob is their own level, so the level multiplier lands where a player
// fighting their own tier would put it. Returns the map they came from.
std::string OpenTryout(GameState& state) {
  Mob mob;
  mob.set_name("Tryout");
  mob.set_level(state.character.proto().level());
  mob.set_max_hp(1);
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
  Sequence played = PlaySwings(params, kTryoutSeconds);
  double rate = played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
  return rate + OffClockRate(params, played, 1.0);
}

}  // namespace

double CrowdDamage(const AttackOption& attack, int enemies, bool charge_burns) {
  if (attack.damage_per_hit.empty()) {
    return 0.0;
  }
  int hit = std::min(std::max(1, attack.max_enemies), std::max(1, enemies));
  if (attack.scatter_hits > 0) {
    hit = std::min(hit, attack.scatter_hits);
  }
  double per = attack.damage_per_hit[0];
  // A swing that gains as it travels: the k'th enemy takes (1 + gain)^k, so
  // what the whole swing lands is the geometric sum rather than hit times one.
  double damage =
      attack.pierce_gain_pct > 0.0
          ? per * (std::pow(1.0 + attack.pierce_gain_pct, hit) - 1.0) /
                attack.pierce_gain_pct
          : per * hit;
  // A scattered swing spreads before it doubles up, so what it lands is a whole
  // strike on each enemy it reached and what the repeat cut leaves of the rest.
  // No queue here to pick the healthiest from, and none is needed: the total is
  // the same whoever the leftovers fall on.
  if (attack.scatter_hits > hit) {
    damage += per * (attack.scatter_hits - hit) * attack.scatter_repeat_kept;
  }
  if (!attack.lead_damage.empty()) {
    damage +=
        attack.lead_damage[0] * std::min(std::max(1, attack.lead_enemies), hit);
  }
  // Rolled once per enemy reached, so it climbs with the crowd.
  if (!attack.final_attack_damage.empty()) {
    damage += attack.final_attack_damage[0] * hit;
  }
  // Rolled once for the whole swing, so it does not.
  if (!attack.single_final_attack_damage.empty()) {
    damage += attack.single_final_attack_damage[0];
  }
  // A chance that lands on one enemy, and worth a share of what that one was
  // taking anyway -- so it is charged once however wide the swing is.
  for (const ProcRoll& proc : attack.procs) {
    damage += per * proc.chance * proc.damage_pct;
  }
  // The burns this swing lights, charged at the rate they can actually
  // sustain. Relighting one buys no more ticks, so what a swing is worth is
  // the seconds before the same swing comes round again, capped by how long
  // the burn would have lasted anyway. A swing on no cooldown is back as soon
  // as it lands; one on a long wait keeps burning through it. One helping
  // apiece, for the reason CombatSim::SwingDamage charges one.
  for (const DotApplication& burn : attack.dots) {
    if (!charge_burns || burn.interval_seconds <= 0.0 || burn.damage.empty()) {
      continue;
    }
    // An attack on its own clock is paced by that clock and by nothing else:
    // a summon has no swing time and no cooldown, and reading only those two
    // would charge its burn nothing at all.
    double cadence = std::max({attack.swing_seconds, attack.cooldown_seconds,
                               attack.interval_seconds});
    double burning = std::min(burn.duration_seconds, cadence);
    damage +=
        burn.damage[0] * hit * burning * burn.chance / burn.interval_seconds;
  }
  if (attack.empowered != nullptr && attack.empowered_every > 0) {
    // A form that marks enemies rides on top of the strike that sets it off,
    // where one counted on the swing stands in for the swing. The same two
    // readings CombatSim::SwingDamage takes.
    damage +=
        attack.brands_enemies
            ? CrowdDamage(*attack.empowered, enemies, charge_burns) /
                  attack.empowered_every
            : (CrowdDamage(*attack.empowered, enemies, charge_burns) - damage) /
                  attack.empowered_every;
  }
  // The strike the swing sets off, spread over the swings that go out while it
  // is waiting -- and outside the averaging above, because it rides the swing
  // whichever form that swing took.
  if (attack.side != nullptr) {
    double every =
        std::max(attack.side->cooldown_seconds, attack.swing_seconds);
    if (every > 0.0) {
      damage += CrowdDamage(*attack.side, enemies, charge_burns) *
                attack.swing_seconds / every;
    }
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
  // Lines still to land before each buff bought with hits goes up.
  std::vector<double> charge;
  int mask = 0;
};

// Takes a landed swing's lines off every buff bought with them, and only while
// that buff is down -- the same rule CombatSim::CreditBuffs follows.
void ChargeBuffs(const CombatParams& params, int lines, BuffClocks& c) {
  for (int i = 0; i < static_cast<int>(c.charge.size()); ++i) {
    if (params.buffs[i].charge_lines > 0 && c.left[i] <= 0.0) {
      c.charge[i] = std::max(0.0, c.charge[i] - lines);
    }
  }
}

// Winds every buff's clocks on by one step and puts up any that has come round
// on its own. A buff its own swing lays is left alone here -- see LayBuff.
void RunBuffClocks(const CombatParams& params, double step, BuffClocks& c) {
  c.left.resize(params.buffs.size(), 0.0);
  c.cooldown.resize(params.buffs.size(), 0.0);
  c.standing.resize(params.buffs.size(), 0.0);
  if (c.charge.size() != params.buffs.size()) {
    c.charge.resize(params.buffs.size());
    for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
      c.charge[i] = params.buffs[i].charge_lines;
    }
  }
  c.mask = 0;
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    c.left[i] = std::max(0.0, c.left[i] - step);
    c.cooldown[i] = std::max(0.0, c.cooldown[i] - step);
    bool ready =
        buff.charge_lines > 0 ? c.charge[i] <= 0.0 : c.cooldown[i] <= 0.0;
    if (buff.laid_by_attack < 0 && c.left[i] <= 0.0 && ready &&
        buff.duration_seconds > 0.0) {
      c.left[i] = buff.duration_seconds;
      c.cooldown[i] = buff.cooldown_seconds;
      c.charge[i] = buff.charge_lines;
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

// What the Freeze Stacks held multiply a swing by, and what a deeper pile
// would be worth to the swing that comes next. Both mirror CombatSim's -- see
// FreezeBoost and FreezeCredit for why the credit is needed at all.
double FreezeBoost(const AttackOption& attack, int stacks, bool frozen) {
  if (stacks <= 0) {
    return 1.0;
  }
  double spent =
      attack.freeze_spends ? 1.0 + attack.freeze_fd_per_stack * stacks : 1.0;
  // The one mob sim_gear fights is the first type, which is what stands in for
  // the queue everywhere else here.
  double shattered = frozen && !attack.freeze_ied_gain.empty()
                         ? 1.0 + attack.freeze_ied_gain.front() * stacks
                         : 1.0;
  double crit = frozen ? 1.0 + attack.freeze_crit_gain * stacks : 1.0;
  return crit * spent * (1.0 + attack.freeze_matt_gain * stacks) * shattered;
}

// What the scar on the group multiplies a swing by, mirroring
// CombatSim::ScarBoost: a swing scars partway through itself, so a group not
// scarred already collects only the share of the lines landing after the cut.
double ScarBoost(const AttackOption& attack, double odds) {
  if (attack.scar_fd <= 0.0) {
    return 1.0;
  }
  double share = odds;
  if (attack.scar_chance > 0.0) {
    int lines = std::max(1, attack.lines);
    double unscarred =
        std::pow(1.0 - attack.scar_chance, static_cast<double>(lines));
    share =
        1.0 - (1.0 - share) * (1.0 - unscarred) / (attack.scar_chance * lines);
  }
  return 1.0 + attack.scar_fd * share;
}

// The odds a scar stands after this swing, as CombatSim::ApplyScar folds them.
// No credit goes with it: every swing the character makes scars alike, so
// there is no choice between them to price.
double CreditScar(const AttackOption& attack, double odds) {
  if (attack.scar_chance <= 0.0 || attack.scar_seconds <= 0.0) {
    return odds;
  }
  return 1.0 - (1.0 - odds) *
                   std::pow(1.0 - attack.scar_chance,
                            static_cast<double>(std::max(1, attack.lines)));
}

// What the state the enemies are already in multiplies a swing by, mirroring
// CombatSim::ConditionBoost. Whether the group is afflicted at all -- frozen
// or burning -- and how many burns stand across it, which here is the enemies
// each alight slot was laid on. The Burn declaration below is why this takes
// the count already worked out rather than the clocks.
double ConditionBoost(const AttackOption& attack, bool afflicted, int alight) {
  double boost = afflicted ? 1.0 + attack.fd_when_afflicted : 1.0;
  if (attack.fd_per_dot <= 0.0 || attack.dot_count_cap <= 0) {
    return boost;
  }
  return boost *
         (1.0 + attack.fd_per_dot * std::min(alight, attack.dot_count_cap));
}

// What the freeze `attack` would leave is worth over the seconds before it
// could come round again -- CombatSim::FrozenCredit, over the one clock that
// stands here for the whole group, exactly as a burn's does.
double FrozenCredit(const std::vector<AttackOption>& attacks,
                    const AttackOption& attack, int stacks, int enemies,
                    double frozen_left, int alight) {
  bool afflicted = frozen_left > 0.0 || alight > 0;
  if (attack.freeze_seconds <= 0.0) {
    return 0.0;
  }
  double cadence = std::max(attack.swing_seconds, attack.cooldown_seconds);
  double gained =
      std::min(attack.freeze_seconds, cadence) - std::min(frozen_left, cadence);
  if (gained <= 0.0) {
    return 0.0;
  }
  double best = 0.0;
  for (const AttackOption& other : attacks) {
    if (other.swing_seconds <= 0.0) {
      continue;
    }
    // Freezing moves the affliction gate as well as the pile, so the pair is
    // priced together -- see CombatSim::FrozenRate.
    double gain =
        FreezeBoost(other, stacks, true) * ConditionBoost(other, true, alight) -
        FreezeBoost(other, stacks, false) *
            ConditionBoost(other, afflicted, alight);
    best = std::max(best,
                    CrowdDamage(other, enemies) * gain / other.swing_seconds);
  }
  return best * gained;
}

double FreezeCredit(const std::vector<AttackOption>& attacks,
                    const AttackOption& attack, int stacks, int cap,
                    int enemies, bool frozen, int alight) {
  bool afflicted = frozen || alight > 0;
  int room = std::min(attack.freeze_build, cap - stacks);
  if (room <= 0) {
    return 0.0;
  }
  double best = 0.0;
  for (const AttackOption& other : attacks) {
    if (other.swing_seconds <= 0.0) {
      continue;
    }
    double gain = (FreezeBoost(other, stacks + room, frozen) -
                   FreezeBoost(other, stacks, frozen)) *
                  ConditionBoost(other, afflicted, alight);
    best = std::max(best, CrowdDamage(other, enemies) * gain);
  }
  return best;
}

// Moves the pile on for a landed attack, as CombatSim::CreditFreeze does.
int CreditFreeze(const AttackOption& attack, int stacks, int cap) {
  if (cap <= 0) {
    return stacks;
  }
  if (attack.freeze_build > 0) {
    return std::min(cap, stacks + attack.freeze_build);
  }
  if (attack.freeze_spends) {
    return std::max(0, stacks - std::max(1, attack.lines));
  }
  return stacks;
}

// One burn the run is keeping on the enemies in front of it. sim_gear has no
// queue, so a single clock stands for the whole group a swing reaches -- the
// reading it already takes of everything else a swing lands.
struct Burn {
  double left = 0.0;
  // Helpings on them. Fractional, because a burn that takes hold half the time
  // is modelled by half a helping rather than by rolling for it.
  double stacks = 0.0;
  double damage = 0.0;  // one helping, one tick, over every enemy reached
  double interval = 0.0;
  int lit_by = -1;  // swing that last relit it, so its ticks are credited home
  // Enemies it was laid on, which is what the drains count: sim_gear keeps one
  // clock for the group, so the slot stands for this many burning monsters.
  int reached = 0;
};

// A clock per burn slot the character can leave, whichever swing leaves it.
std::vector<Burn> BurnSlots(const CombatParams& params) {
  int slots = params.dot_count;
  for (const AttackOption& attack : params.attacks) {
    for (const DotApplication& burn : attack.dots) {
      slots = std::max(slots, burn.slot + 1);
    }
  }
  return std::vector<Burn>(std::max(0, slots));
}

// Burns alight across the group, and whether anything is afflicted at all.
int BurnsAlight(const std::vector<Burn>& held) {
  int alight = 0;
  for (const Burn& on : held) {
    if (on.left > 0.0 && on.stacks > 0.0) {
      alight += on.reached;
    }
  }
  return alight;
}

// What relighting the burns `attack` carries is worth over the seconds before
// it could come round again -- the mirror of CombatSim::BurnCredit, and needed
// for the same reason: burning that was coming anyway is not this swing's to
// claim, and a chooser told otherwise never swings anything else.
double BurnCredit(const AttackOption& attack, int enemies,
                  const std::vector<Burn>& held) {
  int hit = std::min(std::max(1, attack.max_enemies), std::max(1, enemies));
  double cadence = std::max(
      {attack.swing_seconds, attack.cooldown_seconds, attack.interval_seconds});
  double total = 0.0;
  for (const DotApplication& burn : attack.dots) {
    if (burn.slot < 0 || burn.slot >= static_cast<int>(held.size()) ||
        burn.interval_seconds <= 0.0 || burn.damage.empty()) {
      continue;
    }
    const Burn& on = held[burn.slot];
    double lit = std::min(burn.duration_seconds, cadence);
    double gained = on.stacks * (lit - std::min(on.left, cadence));
    gained += std::min(1.0, burn.max_stacks - on.stacks) * lit;
    total +=
        burn.damage[0] * hit * burn.chance * gained / burn.interval_seconds;
  }
  return total;
}

// What lighting this swing's burns is worth to everything swung AFTER it --
// the gate it opens and the count it deepens, neither of which BurnCredit pays
// for. The mirror of CombatSim::BurnStateCredit.
double BurnStateCredit(const std::vector<AttackOption>& attacks,
                       const AttackOption& attack, int enemies,
                       const std::vector<Burn>& held, bool afflicted) {
  if (attack.dots.empty()) {
    return 0.0;
  }
  int hit = std::min(std::max(1, attack.max_enemies), std::max(1, enemies));
  double cadence = std::max(
      {attack.swing_seconds, attack.cooldown_seconds, attack.interval_seconds});
  int alight = BurnsAlight(held);
  double gained = 0.0;
  for (const DotApplication& burn : attack.dots) {
    if (burn.slot < 0 || burn.slot >= static_cast<int>(held.size()) ||
        burn.interval_seconds <= 0.0) {
      continue;
    }
    double lit = std::min(burn.duration_seconds, cadence);
    gained +=
        burn.chance * hit * (lit - std::min(held[burn.slot].left, cadence));
  }
  if (gained <= 0.0) {
    return 0.0;
  }
  double best = 0.0;
  for (const AttackOption& other : attacks) {
    if (other.swing_seconds <= 0.0) {
      continue;
    }
    double gain = ConditionBoost(other, true, alight + hit) -
                  ConditionBoost(other, afflicted, alight);
    best = std::max(best,
                    CrowdDamage(other, enemies) * gain / other.swing_seconds);
  }
  return best * gained / std::max(1, hit);
}

// Puts every burn the landed swing carries on the group, in expectation: one
// that takes hold half the time adds half a helping and carries the clock half
// the way to a full duration.
void LightBurns(const AttackOption& attack, int enemies, int swung,
                std::vector<Burn>& held) {
  int hit = std::min(std::max(1, attack.max_enemies), std::max(1, enemies));
  for (const DotApplication& burn : attack.dots) {
    if (burn.slot < 0 || burn.slot >= static_cast<int>(held.size()) ||
        burn.interval_seconds <= 0.0 || burn.damage.empty()) {
      continue;
    }
    Burn& on = held[burn.slot];
    on.stacks = std::min<double>(burn.max_stacks, on.stacks + burn.chance);
    on.left =
        burn.chance * burn.duration_seconds + (1.0 - burn.chance) * on.left;
    on.damage = burn.damage[0] * hit;
    on.interval = burn.interval_seconds;
    on.lit_by = swung;
    on.reached = hit;
  }
}

// Lands the ticks the step is owed and drops a pile whose seconds ran out. The
// ticks are spread over the step rather than landed whole: at a hundredth of a
// second against an interval of one the difference is arithmetic, and the run
// is long.
void RunBurns(double step, std::vector<Burn>& held, Sequence& played) {
  for (Burn& on : held) {
    if (on.left <= 0.0 || on.interval <= 0.0) {
      continue;
    }
    double spent = std::min(step, on.left);
    on.left -= spent;
    double dealt = on.damage * on.stacks * spent / on.interval;
    played.damage += dealt;
    if (on.lit_by >= 0 &&
        on.lit_by < static_cast<int>(played.damage_by_attack.size())) {
      played.damage_by_attack[on.lit_by] += dealt;
    }
    if (on.left <= 0.0) {
      on.stacks = 0.0;
    }
  }
}

// The ice a summon lays while the swings go on beside it. Its damage is priced
// apart -- see OffClockRate -- but its freeze is not the summon's own: it
// stands over everything the character swings, and Elquines pulsing every
// three seconds against eight seconds of ice is what keeps a boss frozen for
// the whole fight. Skipped by OffClockRate's arithmetic, so it is walked here.
struct OwnClockIce {
  double interval = 0.0;
  double seconds = 0.0;
  double phase = 0.0;
};

std::vector<OwnClockIce> OwnClockIceSources(const CombatParams& params) {
  std::vector<OwnClockIce> sources;
  for (const AttackOption& extra : params.auto_attacks) {
    if (extra.freeze_seconds > 0.0 && extra.interval_seconds > 0.0) {
      sources.push_back({extra.interval_seconds, extra.freeze_seconds, 0.0});
    }
  }
  return sources;
}

// Runs those clocks on for `step`, returning the ice standing after it.
double RunOwnClockIce(double step, std::vector<OwnClockIce>& sources,
                      double frozen_left) {
  for (OwnClockIce& source : sources) {
    source.phase += step;
    while (source.phase >= source.interval) {
      source.phase -= source.interval;
      frozen_left = std::max(frozen_left, source.seconds);
    }
  }
  return frozen_left;
}

// The swing landing the most per second of the ones off cooldown, or -1 when
// none is. A cast is not among them: it deals no damage.
int BestSwing(const std::vector<AttackOption>& attacks,
              const std::vector<double>& cooldown, int enemies, int stacks,
              int cap, const std::vector<Burn>& held, double frozen_left,
              double scar_odds) {
  int pick = -1;
  double best_rate = -1.0;
  for (int i = 0; i < static_cast<int>(attacks.size()); ++i) {
    const AttackOption& attack = attacks[i];
    if (attack.swing_seconds <= 0.0 || cooldown[i] > 0.0 ||
        attack.heal_fraction > 0.0) {
      continue;
    }
    bool frozen = frozen_left > 0.0;
    int alight = BurnsAlight(held);
    double rate =
        (CrowdDamage(attack, enemies, false) *
             FreezeBoost(attack, stacks, frozen) *
             ScarBoost(attack, scar_odds) *
             ConditionBoost(attack, frozen || alight > 0, alight) +
         BurnCredit(attack, enemies, held) +
         BurnStateCredit(attacks, attack, enemies, held, frozen || alight > 0) +
         FreezeCredit(attacks, attack, stacks, cap, enemies, frozen, alight) +
         FrozenCredit(attacks, attack, stacks, enemies, frozen_left, alight)) /
        attack.swing_seconds;
    if (rate > best_rate) {
      best_rate = rate;
      pick = i;
    }
  }
  return pick;
}

}  // namespace

Sequence PlaySwings(const CombatParams& params, double horizon, int enemies) {
  constexpr double kStep = 0.01;
  std::vector<double> cooldown(params.attacks.size(), 0.0);
  std::vector<int> swings(params.attacks.size(), 0);
  BuffClocks clocks;
  std::vector<Burn> burning = BurnSlots(params);
  std::vector<OwnClockIce> summoned_ice = OwnClockIceSources(params);
  Sequence played;
  played.damage_by_attack.assign(params.attacks.size(), 0.0);
  int freeze = 0;
  // One clock for the whole group, as the burns have: the ice a swing lays
  // falls on everything it reached.
  double frozen_left = 0.0;
  // The odds the group carries a scar, as one number for all of them -- the
  // same simplification the freeze clock above makes.
  double scar_odds = 0.0;
  double phase = 0.0;
  int pick = -1;  // the swing being wound up, held until it lands
  for (double elapsed = 0.0; elapsed < horizon; elapsed += kStep) {
    RunBuffClocks(params, kStep, clocks);
    RunBurns(kStep, burning, played);
    frozen_left =
        RunOwnClockIce(kStep, summoned_ice, std::max(0.0, frozen_left - kStep));
    for (double& left : cooldown) {
      left = std::max(0.0, left - kStep);
    }
    // Index 0 is the bare poke, which is never committed to.
    if (pick <= 0) {
      pick = SwingToLay(params, clocks, cooldown);
      if (pick < 0) {
        pick = BestSwing(params.Attacks(clocks.mask), cooldown, enemies, freeze,
                         params.FreezeCap(clocks.mask), burning, frozen_left,
                         scar_odds);
      }
    }
    if (pick < 0) {
      break;
    }
    // Read off the table for the buffs standing now: the same swing is worth
    // more under a buff, and which one it is was settled when it was aimed.
    const AttackOption& swung = params.Attacks(clocks.mask)[pick];
    // A HELD swing is played to the end here. Nothing in this sim tracks what
    // one enemy has left, so there is nothing to let go early for -- and a
    // boss, which is what these numbers are for, is held to the end anyway.
    phase += kStep;
    if (phase < swung.swing_seconds) {
      continue;
    }
    phase -= swung.swing_seconds;
    // Read before the pile moves, so the swing is paid for the stacks it went
    // in holding.
    // Burn out: the clocks land its ticks as they fall due, which is the whole
    // point of keeping them.
    int alight = BurnsAlight(burning);
    double landed =
        CrowdDamage(swung, enemies, false) *
        FreezeBoost(swung, freeze, frozen_left > 0.0) *
        ScarBoost(swung, scar_odds) *
        ConditionBoost(swung, frozen_left > 0.0 || alight > 0, alight);
    LightBurns(swung, enemies, pick, burning);
    scar_odds = CreditScar(swung, scar_odds);
    frozen_left = std::max(frozen_left, swung.freeze_seconds);
    freeze = CreditFreeze(swung, freeze, params.FreezeCap(clocks.mask));
    played.damage += landed;
    played.damage_by_attack[pick] += landed;
    played.seconds += swung.swing_seconds;
    ++swings[pick];
    cooldown[pick] = swung.cooldown_seconds;
    LayBuff(params, pick, clocks);
    ChargeBuffs(params, swung.lines, clocks);
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
                    double speed, int enemies) {
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
    double per_pulse = CrowdDamage(extra, enemies);
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

// `proto` as a player who kept at it would leave it: every slot spent on
// `scroll` and every star up to `star_cap` landed. A null scroll leaves the
// slots unspent, which is what an item taking none gets.
Equip AtCeiling(const EquipPrototype& proto, const Scroll* scroll,
                int star_cap) {
  Equip state;
  state.set_equip_name(proto.name());
  int slots = proto.upgrade_slots();
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

// Wears a fresh `proto` carrying `made`, in place of whatever the slot holds.
// The displaced copy stays in the bag, which is where a try-on goes.
bool WearMade(CharacterInstance& character, const EquipPrototype& proto,
              const Equip& made) {
  character.Unequip(proto.equip_slot());
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
    if (!WearMade(state.character, proto,
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
  for (const std::pair<const EquipSlot, EquipPrototype>& entry : worn) {
    const Scroll* scroll =
        BestScrollForSlot(state, entry.first, /*success_rate=*/0);
    (*before.mutable_equipped())[entry.first] =
        AtCeiling(entry.second, scroll, star_cap);
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
