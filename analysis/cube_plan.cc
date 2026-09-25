#include "analysis/cube_plan.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "analysis/sim_boss.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/combat/damage.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

const EquipInstance* Worn(const GameState& state, EquipSlot slot) {
  WornGear::const_iterator it = state.character.equipped().find(slot);
  return it == state.character.equipped().end() ? nullptr : it->second;
}

// TotalEquipStats' own attack fold, redone here because the percentage differs
// between candidates: a potential grants %ATT, and the fold scales the weapon's
// attack along with everything else.
int FoldAttack(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + 1e-9));
}

// Removes `part`'s share from `combined`, undoing the reverse-multiplicative
// combine that ignored-defence sources use. Returns 1 when the part already
// ignores everything.
double WithoutIgnoredDefense(double combined, double part) {
  if (part >= 1.0) {
    return 1.0;
  }
  return 1.0 - (1.0 - combined) / (1.0 - part);
}

// Potential totals from everything worn except `slot`. A cube's roll is added
// on top, so the other pieces are summed once per slot rather than once per
// draw.
PotentialTotals PotentialsBut(const CharacterInstance& character,
                              EquipSlot slot) {
  PotentialTotals totals;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       character.equipped()) {
    if (entry.first == slot) {
      continue;
    }
    AddPotential(entry.second->equip_state().main_potential(),
                 entry.second->prototype().required_level(), totals);
  }
  return totals;
}

// Stats the character wears and is granted with `totals` in place of the worn
// potentials. Returns the two halves WorthOf takes, not a folded OffenseStats.
// Folding one here once measured cubes without the attack while stars were
// measured with it, even though BuyBest sorts them together.
void StatsWith(const GameState& state, const CubeBasis& basis,
               const PotentialTotals& totals, EquipStats* out,
               PassiveOffense* out_passives) {
  const CharacterInstance& character = state.character;
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

  *out = stats;
  *out_passives = passives;
}

// Damage a character with `totals` deals against the yardstick, in the same
// units scroll and star offers are ranked in. So a cube and a star compare
// directly.
double PowerOf(const GameState& state, const CubeBasis& basis,
               const PotentialTotals& totals) {
  EquipStats stats;
  PassiveOffense passives;
  StatsWith(state, basis, totals, &stats, &passives);
  return WorthOf(state, basis.yard, stats, passives);
}

// Income value of swapping the worn potentials for `totals`, in the same combat
// power units as the rest of the shelf. A %meso or %drop line earns a rate, and
// a rate is worth as much as the time left to earn it.
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

CubeBasis CubeBasisFor(const GameState& state, const Yardstick& yard) {
  CubeBasis basis;
  // Use the bossing preset, because the target fight is what this valuation
  // aims at. DerivedStatsFor defaults to the farming preset, which read the
  // wrong hyper stats and Inner Ability for ignored defence. A character over
  // the defence wall then valued cubes as if under it, at nothing.
  basis.derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                  Activity::kBossing);
  const EquipStats sources[] = {state.character.equip_stats(),
                                basis.derived.skill_stats};
  basis.raw = SumEquipStats(absl::MakeConstSpan(sources));
  basis.yard = yard;
  return basis;
}

namespace {

// Value of `rolled` replacing what `slot` holds: power plus income, in the
// shelf's single currency. `others` and `standing` don't change between draws,
// so the caller computes them once.
double GainOf(const GameState& state, const CubeBasis& basis, int level,
              const PotentialTotals& others, const PotentialTotals& now,
              double standing, const Potential& rolled,
              const CubeIncome& income) {
  PotentialTotals totals = others;
  AddPotential(rolled, level, totals);
  return PowerOf(state, basis, totals) - standing +
         IncomeGain(basis, now, totals, income);
}

// Run lengths the shopper considers. A single cube is the usual offer; longer
// runs make a line that needs a higher rank get valued at the cost of reaching
// it, rather than written off on the first roll.
constexpr int kCubeProgramLengths[] = {1, 4, 16, 64};

// Runs played per slot. Few, because runs are the expensive part: every cube in
// one has to be valued to decide whether to keep it.
constexpr int kCubeRuns = 4;

constexpr int kCubeProgramLengthCount =
    sizeof(kCubeProgramLengths) / sizeof(kCubeProgramLengths[0]);

}  // namespace

namespace {

// Everything a cube on one slot is valued against, computed once per slot
// rather than per draw.
struct CubePricing {
  int level = 0;
  PotentialGroup group{};
  PotentialTotals others;
  PotentialTotals now;
  double standing = 0.0;
};

// Expected gain of one cube on the slot, averaged over draws and never below
// zero.
double MarginalGain(const GameState& state, const CubeBasis& basis,
                    const CubePricing& pricing, const Potential& current,
                    const CubeIncome& income, std::mt19937& rng) {
  double total = 0.0;
  for (int draw = 0; draw < kCubeSamples; ++draw) {
    total += std::max(
        0.0, GainOf(state, basis, pricing.level, pricing.others, pricing.now,
                    pricing.standing,
                    CubePotential(current, CubeType::kRed, pricing.group, rng),
                    income));
  }
  return total / kCubeSamples;
}

// Total gain left by runs of each length, summed over kCubeRuns. Runs are
// played out rather than rolls counted independently, because each cube rolls
// against what the last one left. Keeping a better roll can raise the item's
// rank, and the line that clears a defence wall may only exist at a higher
// rank. Counting independent rolls would never see that.
std::vector<double> PlayCubeRuns(const GameState& state, const CubeBasis& basis,
                                 const CubePricing& pricing,
                                 const Potential& current,
                                 const CubeIncome& income, std::mt19937& rng) {
  int longest = kCubeProgramLengths[kCubeProgramLengthCount - 1];
  std::vector<double> reached(kCubeProgramLengthCount, 0.0);
  for (int run = 0; run < kCubeRuns; ++run) {
    Potential held = current;
    double best_gain = 0.0;
    int rung = 0;
    for (int cube = 1; cube <= longest; ++cube) {
      Potential rolled =
          CubePotential(held, CubeType::kRed, pricing.group, rng);
      double gain = GainOf(state, basis, pricing.level, pricing.others,
                           pricing.now, pricing.standing, rolled, income);
      // Keep-better, as GMS offers: a roll worse than the item's current lines
      // is declined, and the cube only bought the chance.
      //
      // A higher rank is kept even when damage doesn't change. Under a defence
      // wall every roll is worth nothing, since both sides deal the 1-damage
      // floor, so a run judged on damage alone would never climb a rank or
      // reach the line that clears the wall.
      if (gain > best_gain ||
          (gain >= best_gain && rolled.rank() > held.rank())) {
        best_gain = gain;
        held = rolled;
      }
      if (cube == kCubeProgramLengths[rung]) {
        reached[rung++] += best_gain;
      }
    }
  }
  return reached;
}

}  // namespace

CubeProgram BestCubeProgram(const GameState& state, const CubeBasis& basis,
                            EquipSlot slot, const CubeIncome& income,
                            std::mt19937& rng) {
  CubeProgram best;
  const EquipInstance* item = Worn(state, slot);
  if (item == nullptr || !item->CanCube()) {
    return best;
  }
  const Potential& current = item->equip_state().main_potential();
  CubePricing pricing;
  pricing.level = item->prototype().required_level();
  pricing.group = PotentialGroupOf(slot);
  pricing.others = PotentialsBut(state.character, slot);
  pricing.now = pricing.others;
  AddPotential(current, pricing.level, pricing.now);
  pricing.standing = PowerOf(state, basis, pricing.now);

  double share =
      Replaceable(state, slot)
          ? static_cast<double>(kReplaceableNumerator) / kReplaceableDenominator
          : 1.0;

  // Try one cube first; usually that decides it. A longer run only beats a
  // single cube per meso when the single cube is worth nothing, which only
  // happens under a defence wall. So the expensive part below is skipped
  // whenever one roll already pays.
  double marginal =
      MarginalGain(state, basis, pricing, current, income, rng) * share;
  if (marginal > 0.0) {
    best.cubes = 1;
    best.gain = marginal;
    best.cost = kCubeCost;
    return best;
  }

  // Play out runs; see PlayCubeRuns for why they can't be counted
  // independently.
  std::vector<double> reached =
      PlayCubeRuns(state, basis, pricing, current, income, rng);

  for (int rung = 0; rung < kCubeProgramLengthCount; ++rung) {
    double expected = reached[rung] / kCubeRuns * share;
    int64_t cost = static_cast<int64_t>(kCubeProgramLengths[rung]) * kCubeCost;
    if (expected <= 0.0) {
      continue;
    }
    // Compare by cross-multiplying rather than dividing. The empty starting run
    // must lose to anything: with zero cost it would otherwise tie every length
    // and block them all.
    if (best.cubes > 0 && expected * best.cost <= best.gain * cost) {
      continue;  // no better per meso than the run already chosen
    }
    best.cubes = kCubeProgramLengths[rung];
    best.gain = expected;
    best.cost = cost;
  }
  return best;
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
  double gain =
      GainOf(state, basis, level, others, now, standing, rolled, income);
  if (gain > 0.0) {
    return true;
  }
  // Accept a higher rank even when damage didn't change, on the same terms
  // BestCubeProgram used to price the run. Under a defence wall every roll is
  // worth nothing, and a rule reading damage alone would throw away the rank-up
  // the run was bought for. The two must agree, or the shopper pays for a
  // program and then declines every result; that once happened with 2,484
  // cubes.
  return gain >= 0.0 &&
         rolled.rank() > item->equip_state().main_potential().rank();
}

// Whether the character could ever buy `proto`, as opposed to whether the
// catalog lists it. A tier priced in tokens only counts once one of the tokens
// has dropped. The AbsoLab weapon costs coins only Damien and Lotus give, so to
// a character who can't clear them it isn't the next weapon, and they keep the
// one in hand.
bool WithinReach(const GameState& state, const EquipPrototype& proto) {
  if (proto.token_price() <= 0) {
    return true;
  }
  std::map<std::string, ItemPrototype>::const_iterator token =
      state.items.find(proto.token_item());
  if (token == state.items.end()) {
    return false;
  }
  return state.character.CountItem(token->second.name()) > 0;
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
    // A branch's weapon type is chosen by measurement, not level, so only a
    // higher-level weapon of the same type replaces one. See SettledWeaponType.
    if (type != EQUIP_TYPE_UNSPECIFIED && proto.equip_type() != type) {
      continue;
    }
    if (state.character.MeetsJob(proto) && WithinReach(state, proto)) {
      return true;
    }
  }
  return false;
}

}  // namespace ms
