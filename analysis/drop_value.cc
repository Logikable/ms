#include "analysis/drop_value.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "analysis/sim_gear.h"
#include "analysis/yardstick.h"
#include "src/character/arcane_force.h"
#include "src/character/character_stats.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
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

// Stats of the item worn in `slot`, which a replacement has to beat. Empty for
// an empty slot.
EquipStats WornIn(const GameState& state, EquipSlot slot) {
  WornGear::const_iterator it = state.character.equipped().find(slot);
  return it == state.character.equipped().end() ? EquipStats()
                                                : it->second->stats();
}

// Value of one duplicate of a worn symbol. A duplicate is one EXP toward the
// next symbol level, which also costs meso. So it's worth the level's gain
// minus its price, divided by the duplicates it takes.
double SymbolDuplicateValue(const GameState& state, const DropBasis& basis,
                            const EquipInstance& worn) {
  const ms::Equip& state_of = worn.equip_state();
  int level = SymbolLevel(state_of);
  int needed = SymbolExpToNextLevel(level);
  if (needed <= 0) {
    return 0.0;  // maxed out; another copy adds nothing
  }
  StatField primary = PrimaryStatField(state.character.proto().job());
  EquipStats added =
      Minus(SymbolStatsFor(primary, level + 1), SymbolStatsFor(primary, level));
  double gain = PowerWith(state, basis, Plus(basis.worn, added)) - basis.power;
  double paid = AsMeso(basis, gain) -
                static_cast<double>(SymbolLevelUpCost(worn.prototype(), level));
  return std::max(0.0, paid / needed);
}

}  // namespace

DropBasis DropBasisFor(const GameState& state, double power_per_meso,
                       HeldYardstick& held) {
  DropBasis basis;
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
    if (proto.token_item().empty() || proto.token_price() <= 0) {
      continue;
    }
    double each = EquipDropValue(state, basis, proto) / proto.token_price();
    double& best = basis.tokens[proto.token_item()];
    best = std::max(best, each);
  }
  return basis;
}

double EquipDropValue(const GameState& state, const DropBasis& basis,
                      const EquipPrototype& proto) {
  if (!state.character.MeetsJob(proto) || !state.character.MeetsLevel(proto) ||
      !ReachedSymbolArea(state.character, proto)) {
    return 0.0;
  }
  if (IsArcaneSymbol(proto)) {
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
  EquipStats added =
      Minus(EquipInstance(proto).stats(), WornIn(state, proto.equip_slot()));
  double gain = PowerWith(state, basis, Plus(basis.worn, added)) - basis.power;
  return AsMeso(basis, gain);
}

double ItemDropValue(const DropBasis& basis, const std::string& key,
                     const ItemPrototype& proto) {
  if (proto.sell_price() > 0) {
    return proto.sell_price();
  }
  std::map<std::string, double>::const_iterator token = basis.tokens.find(key);
  return token == basis.tokens.end() ? 0.0 : token->second;
}

double DropsPerKill(const GameState& state, const DropBasis& basis,
                    const Mob& mob) {
  double total = 0.0;
  for (const MobDrop& drop : mob.drops()) {
    if (drop.has_item()) {
      std::map<std::string, ItemPrototype>::const_iterator it =
          state.items.find(drop.item());
      if (it != state.items.end()) {
        total +=
            drop.per_kill() * ItemDropValue(basis, drop.item(), it->second);
      }
      continue;
    }
    if (!drop.has_equip()) {
      continue;
    }
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    if (it != state.equips.end()) {
      total += drop.per_kill() * EquipDropValue(state, basis, it->second);
    }
  }
  return total;
}

}  // namespace ms
