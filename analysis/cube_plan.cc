#include "analysis/cube_plan.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "absl/types/span.h"
#include "src/character/character_stats.h"
#include "src/combat/damage.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

const EquipInstance* Worn(const GameState& state, EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      state.character.equipped().find(slot);
  return it == state.character.equipped().end() ? nullptr : &it->second;
}

// TotalEquipStats' own fold, redone here because the percentage is what moves
// between two candidates: a potential grants %ATT, and the fold scales the
// weapon in the character's hand along with everything else.
int FoldAttack(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + 1e-9));
}

// `combined` with `part`'s share taken back out, undoing the reverse combine
// two sources of ignored defence meet by. 1 where the part cancels everything,
// which leaves nothing for the rest to say.
double WithoutIgnoredDefense(double combined, double part) {
  if (part >= 1.0) {
    return 1.0;
  }
  return 1.0 - (1.0 - combined) / (1.0 - part);
}

// The potentials on everything worn but `slot`. What a cube rolls goes on top
// of this, so the sum over the other pieces is taken once per slot rather than
// once per draw.
PotentialTotals PotentialsBut(const CharacterInstance& character,
                              EquipSlot slot) {
  PotentialTotals totals;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       character.equipped()) {
    if (entry.first == slot) {
      continue;
    }
    AddPotential(entry.second.equip_state().main_potential(),
                 entry.second.prototype().required_level(), totals);
  }
  return totals;
}

// What a boss's defence leaves of a character who ignores `ied` of it.
// CombatPower has no target and so cannot say, but ignored defence is one of
// the three lines a weapon is cubed for -- a shopper blind to it would never
// buy one.
double DefenceFactor(double ied) {
  return 1.0 - kBossPdr * (1.0 - ied);
}

// The character's damage chain with `totals` in place of the potentials they
// wear. Everything a potential moves, and nothing else.
OffenseStats OffenseWith(const GameState& state, const CubeBasis& basis,
                         const PotentialTotals& totals) {
  const CharacterInstance& character = state.character;
  const Character& proto = character.proto();
  const PotentialTotals& worn = character.potential_totals();
  const EquipStats paid = PotentialStatGrant(character, basis.derived, totals);
  const EquipStats& held = basis.derived.potential_stats;

  EquipStats stats = basis.raw;
  stats.set_str(stats.str() + paid.str() - held.str());
  stats.set_dex(stats.dex() + paid.dex() - held.dex());
  stats.set_int_(stats.int_() + paid.int_() - held.int_());
  stats.set_luk(stats.luk() + paid.luk() - held.luk());
  stats.set_attack(FoldAttack(
      stats.attack(),
      basis.derived.attack_pct - worn.attack_pct + totals.attack_pct));
  stats.set_magic_attack(
      FoldAttack(stats.magic_attack(), basis.derived.magic_attack_pct -
                                           worn.magic_attack_pct +
                                           totals.magic_attack_pct));

  PassiveOffense passives = PassiveOffenseFor(basis.derived);
  passives.damage_pct += totals.damage_pct - worn.damage_pct;
  passives.boss_pct += totals.boss_pct - worn.boss_pct;
  passives.crit_dmg += totals.crit_dmg - worn.crit_dmg;
  passives.ied = CombineIgnoredDefense(
      WithoutIgnoredDefense(basis.derived.ied, worn.ied), totals.ied);

  return OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                         stats, character.weapon_type(),
                         /*attack_skill=*/nullptr, /*attack_level=*/0,
                         passives);
}

// What a character carrying `totals` hits a boss for, in the units the scroll
// and star offers are ranked in: their own combat power, times what an
// ignored-defence line moves against what they already ignore. So a cube and
// a star are compared in one currency.
double PowerOf(const GameState& state, const CubeBasis& basis,
               const PotentialTotals& totals) {
  OffenseStats offense = OffenseWith(state, basis, totals);
  return CombatPower(offense, /*vs_boss=*/true) * DefenceFactor(offense.ied) /
         basis.defence;
}

// What swapping the worn potentials for `totals` is worth in income, priced in
// the same combat power the rest of the shelf is: a %meso or %drop line pays a
// rate, and what a rate is worth is how long there is left to earn it.
double IncomeGain(const CubeBasis& basis, const PotentialTotals& worn,
                  const PotentialTotals& totals, const CubeIncome& income) {
  if (!income.rate || income.seconds_left <= 0.0 ||
      income.power_per_meso <= 0.0) {
    return 0.0;
  }
  if (totals.meso_pct == worn.meso_pct &&
      totals.item_drop_pct == worn.item_drop_pct) {
    return 0.0;
  }
  DerivedStats after = basis.derived;
  after.equip_meso_pct += totals.meso_pct - worn.meso_pct;
  after.item_drop_pct += totals.item_drop_pct - worn.item_drop_pct;
  double extra =
      income.rate(MesoBonus(after), after.item_drop_pct) -
      income.rate(MesoBonus(basis.derived), basis.derived.item_drop_pct);
  return extra * income.seconds_left * income.power_per_meso;
}

}  // namespace

CubeBasis CubeBasisFor(const GameState& state) {
  CubeBasis basis;
  basis.derived = DerivedStatsFor(state.character, state.skills);
  const EquipStats sources[] = {state.character.equip_stats(),
                                basis.derived.skill_stats};
  basis.raw = SumEquipStats(absl::MakeConstSpan(sources));
  basis.defence = DefenceFactor(
      OffenseWith(state, basis, state.character.potential_totals()).ied);
  return basis;
}

namespace {

// What taking `rolled` in place of what `slot` holds would be worth: power and
// income together, in the one currency the shelf is ranked in. `others` and
// `standing` are the two halves of the comparison that do not move between
// draws, so they are worked out once by the caller.
double GainOf(const GameState& state, const CubeBasis& basis, int level,
              const PotentialTotals& others, const PotentialTotals& now,
              double standing, const Potential& rolled,
              const CubeIncome& income) {
  PotentialTotals totals = others;
  AddPotential(rolled, level, totals);
  return PowerOf(state, basis, totals) - standing +
         IncomeGain(basis, now, totals, income);
}

}  // namespace

int CubeGain(const GameState& state, const CubeBasis& basis, EquipSlot slot,
             const CubeIncome& income, std::mt19937& rng) {
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr || !item->CanCube()) {
    return 0;
  }
  int level = item->prototype().required_level();
  PotentialGroup group = PotentialGroupOf(slot);
  const Potential& current = item->equip_state().main_potential();
  PotentialTotals others = PotentialsBut(state.character, slot);
  PotentialTotals now = others;
  AddPotential(current, level, now);
  double standing = PowerOf(state, basis, now);

  double total = 0.0;
  for (int draw = 0; draw < kCubeSamples; ++draw) {
    // Keep-better, which is the offer GMS makes: a roll worse than what the
    // item holds is declined, and the cube bought the chance rather than the
    // result.
    total +=
        std::max(0.0, GainOf(state, basis, level, others, now, standing,
                             CubePotential(current, CubeType::kRed, group, rng),
                             income));
  }
  double mean = total / kCubeSamples;
  if (Replaceable(state, slot)) {
    mean = mean * kReplaceableNumerator / kReplaceableDenominator;
  }
  return static_cast<int>(mean);
}

bool WorthTaking(const GameState& state, const CubeBasis& basis, EquipSlot slot,
                 const Potential& rolled, const CubeIncome& income) {
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr) {
    return false;
  }
  int level = item->prototype().required_level();
  PotentialTotals others = PotentialsBut(state.character, slot);
  PotentialTotals now = others;
  AddPotential(item->equip_state().main_potential(), level, now);
  double standing = PowerOf(state, basis, now);
  return GainOf(state, basis, level, others, now, standing, rolled, income) >
         0.0;
}

bool Replaceable(const GameState& state, EquipSlot slot) {
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr) {
    return false;
  }
  EquipSlot wants = item->prototype().equip_slot();
  EquipType type = item->prototype().equip_type();
  int level = item->prototype().required_level();
  int reached = state.character.proto().level();
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state.equips) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != wants || proto.required_level() <= level ||
        proto.required_level() > reached) {
      continue;
    }
    // Which weapon a branch swings is a measurement rather than a level, so
    // only a longer ladder of the same type replaces one -- see
    // SettledWeaponType.
    if (type != EQUIP_TYPE_UNSPECIFIED && proto.equip_type() != type) {
      continue;
    }
    if (state.character.MeetsJob(proto)) {
      return true;
    }
  }
  return false;
}

}  // namespace ms
