#include "analysis/drop_value.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "analysis/sim_gear.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/character/symbol.h"
#include "src/combat/damage.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/shop.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

EquipStats Plus(const EquipStats& a, const EquipStats& b) {
  const EquipStats sources[] = {a, b};
  return SumEquipStats(sources);
}

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

// Damage against the yardstick with `stats` in place of what the character
// wears, by the closed form, as GearShopper does. A played fight per drop would
// cost too much.
double PowerWith(const GameState& state, const DropBasis& basis,
                 const EquipStats& stats) {
  return WorthOf(state, basis.yard, stats, PassiveOffenseFor(basis.derived));
}

// Converts a damage gain to the meso it would otherwise cost. Zero when no rate
// was given, so a caller without a shopper gets zero for unsellable drops.
double AsMeso(const DropBasis& basis, double gain) {
  if (gain <= 0.0 || basis.power_per_meso <= 0.0) {
    return 0.0;
  }
  return gain / basis.power_per_meso;
}

// Stats of the item worn in `slot`, which a replacement has to beat: as worn,
// or by tier the better of the farming and bossing presets' pieces, so a piece
// one preset already wears is no upgrade for the other. Empty for an empty
// slot.
EquipStats WornIn(const GameState& state, const DropBasis& basis,
                  EquipSlot slot) {
  if (!basis.by_tier) {
    WornGear::const_iterator it = state.character.equipped().find(slot);
    return it == state.character.equipped().end() ? EquipStats()
                                                  : it->second->stats();
  }
  EquipStats best;
  double best_power = -1.0;
  for (StatPreset preset : {kFarmGear, kBossGear}) {
    const EquipInstance* worn = state.character.WornAt(preset, slot);
    if (worn == nullptr) {
      continue;
    }
    EquipStats stats = EquipInstance(worn->prototype()).stats();
    double power = PowerWith(state, basis, Plus(basis.worn, stats));
    if (power > best_power) {
      best_power = power;
      best = stats;
    }
  }
  return best;
}

// Whether `proto` is a weapon of another type than the one worn. The closed
// form prices stats alone and would rate a dagger over a Night Lord's claw,
// though no skill of theirs fires from it.
bool OtherWeaponType(const GameState& state, const EquipPrototype& proto) {
  if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON) {
    return false;
  }
  WornGear::const_iterator it =
      state.character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  return it != state.character.equipped().end() &&
         it->second->prototype().equip_type() != proto.equip_type();
}

// Value of one duplicate of a worn symbol. A duplicate is one EXP toward the
// next symbol level, which also costs meso. So it's worth the level's gain
// minus its price, divided by the duplicates it takes.
double SymbolDuplicateValue(const GameState& state, const DropBasis& basis,
                            const EquipInstance& worn) {
  const ms::Equip& state_of = worn.equip_state();
  int level = SymbolLevel(state_of);
  int needed = SymbolExpToNextLevel(worn.prototype(), level);
  if (needed <= 0) {
    return 0.0;  // maxed out; another copy adds nothing
  }
  StatField primary = PrimaryStatField(state.character.proto().job());
  EquipStats added = Minus(SymbolStatsFor(worn.prototype(), primary, level + 1),
                           SymbolStatsFor(worn.prototype(), primary, level));
  double gain = PowerWith(state, basis, Plus(basis.worn, added)) - basis.power;
  double paid = AsMeso(basis, gain) -
                static_cast<double>(SymbolLevelUpCost(worn.prototype(), level));
  return std::max(0.0, paid / needed);
}

// What more drop rate is worth on loot wanted once, as a share of what each
// expected copy would be worth: the piece is held from when it arrives, so the
// gain is how much sooner that is. With arrival exponential at `x` expected
// copies over the run, the share of the run held is 1 - (1 - e^-x) / x, whose
// slope this is per copy. One half at x = 0, since a copy lands on average
// halfway; about 1 / x^2 once the piece is all but certain.
double SoonerShare(double x) {
  if (x < 1e-3) {
    return 0.5 - x / 3.0;
  }
  return (-std::expm1(-x) - x * std::exp(-x)) / (x * x);
}

}  // namespace

DropBasis DropBasisFor(const GameState& state, double power_per_meso,
                       HeldYardstick& held, bool by_tier) {
  DropBasis basis;
  basis.by_tier = by_tier;
  basis.derived = DerivedStatsFor(state.character, state.skills);
  basis.worn = TotalEquipStats(state.character, basis.derived);
  basis.yard = held.For(state);
  basis.power = PowerWith(state, basis, basis.worn);
  basis.power_per_meso = power_per_meso;
  // Scan the shelf once rather than once per token. Nothing on the shelf is
  // bought with a token that is itself bought with tokens, so the basis without
  // tokens is enough.
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state.equips) {
    const EquipPrototype& proto = entry.second;
    if (proto.token_prices().empty()) {
      continue;
    }
    double value = EquipDropValue(state, basis, proto);
    for (const TokenPrice& price : proto.token_prices()) {
      double& best = basis.tokens[price.token_item()];
      if (value / price.count() > best) {
        best = value / price.count();
        basis.token_counts[price.token_item()] = price.count();
      }
    }
  }
  // Only boss loot holds boxes, and each one's stock sorts the whole catalog.
  for (const std::pair<const std::string, ItemPrototype>& entry : state.items) {
    if (!by_tier || !(entry.second.has_box() || entry.second.has_pick_box())) {
      continue;
    }
    for (const std::string& key : BoxStock(entry.second, state.equips)) {
      const EquipPrototype& piece = state.equips.at(key);
      if (state.character.MeetsJob(piece)) {
        double& best = basis.boxes[entry.first];
        best = std::max(best, EquipDropValue(state, basis, piece));
      }
    }
  }
  return basis;
}

double EquipDropValue(const GameState& state, const DropBasis& basis,
                      const EquipPrototype& proto) {
  if (!state.character.MeetsJob(proto) || !state.character.MeetsLevel(proto) ||
      !ReachedSymbolArea(state.character, proto) ||
      OtherWeaponType(state, proto)) {
    return 0.0;
  }
  if (IsSymbol(proto)) {
    // A second copy of a worn symbol is a duplicate that feeds the symbol's
    // level, not a piece of gear. A first copy falls through and is valued as
    // gear.
    WornGear::const_iterator worn =
        state.character.equipped().find(proto.equip_slot());
    if (worn != state.character.equipped().end()) {
      return SymbolDuplicateValue(state, basis, *worn->second);
    }
  }
  // What wearing it would add over the item in the slot. A piece no better than
  // the worn one scores zero, which is right: wearing it is the only use of a
  // gear drop.
  EquipStats added = Minus(EquipInstance(proto).stats(),
                           WornIn(state, basis, proto.equip_slot()));
  double gain = PowerWith(state, basis, Plus(basis.worn, added)) - basis.power;
  return AsMeso(basis, gain);
}

double ItemDropValue(const DropBasis& basis, const std::string& key,
                     const ItemPrototype& proto) {
  if (proto.sell_price() > 0) {
    return proto.sell_price();
  }
  std::map<std::string, double>::const_iterator token = basis.tokens.find(key);
  if (token != basis.tokens.end()) {
    return token->second;
  }
  std::map<std::string, double>::const_iterator box = basis.boxes.find(key);
  return box == basis.boxes.end() ? 0.0 : box->second;
}

double ClearLootPerDropRate(const GameState& state, const DropBasis& basis,
                            const BossDifficulty& difficulty, double drop_pct,
                            double clears) {
  double total = 0.0;
  for (const MobDrop& drop : difficulty.drops()) {
    double value = 0.0;
    double scaled = drop.per_kill();
    // Copies a clear yields at `drop_pct`, and how many are wanted, where zero
    // is no limit.
    double copies = drop.per_kill() * (1.0 + drop_pct);
    int wanted = 1;
    if (drop.has_item()) {
      std::map<std::string, ItemPrototype>::const_iterator it =
          state.items.find(drop.item());
      if (it == state.items.end()) {
        continue;
      }
      value = ItemDropValue(basis, drop.item(), it->second);
      std::map<std::string, int>::const_iterator count =
          basis.token_counts.find(drop.item());
      if (it->second.sell_price() > 0) {
        wanted = 0;
      } else if (count != basis.token_counts.end()) {
        wanted = count->second;
      }
    } else if (drop.has_equip()) {
      std::map<std::string, EquipPrototype>::const_iterator it =
          state.equips.find(drop.equip());
      if (it != state.equips.end()) {
        value = EquipDropValue(state, basis, it->second);
      }
      scaled = drop.per_kill() - std::floor(drop.per_kill());
      if (scaled * (1.0 + drop_pct) >= 1.0) {
        scaled = 0.0;
      }
      copies = std::floor(drop.per_kill()) +
               std::min(1.0, (drop.per_kill() - std::floor(drop.per_kill())) *
                                 (1.0 + drop_pct));
    }
    double gain = scaled * DropRolls(drop) * value * clears;
    if (wanted > 0) {
      gain *= SoonerShare(copies * DropRolls(drop) * clears / wanted);
    }
    total += gain;
  }
  return total;
}

double DropsPerKill(const GameState& state, const DropBasis& basis,
                    const Mob& mob) {
  double total = 0.0;
  for (const MobDrop& drop : mob.drops()) {
    if (drop.has_item()) {
      std::map<std::string, ItemPrototype>::const_iterator it =
          state.items.find(drop.item());
      if (it != state.items.end()) {
        total += drop.per_kill() * DropRolls(drop) *
                 ItemDropValue(basis, drop.item(), it->second);
      }
      continue;
    }
    if (!drop.has_equip()) {
      continue;
    }
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    if (it != state.equips.end()) {
      total += drop.per_kill() * DropRolls(drop) *
               EquipDropValue(state, basis, it->second);
    }
  }
  return total;
}

}  // namespace ms
