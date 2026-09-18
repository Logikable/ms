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

// What the character wears and grants with `totals` in place of the potentials
// worn. The two halves the yardstick's door asks for rather than a folded
// OffenseStats: folding one here is how a cube came to be measured WITHOUT the
// swing while a star was measured with it, though BuyBest sorts them
// together.
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

// What a character carrying `totals` hits a boss for, in the units the scroll
// and star offers are ranked in: their own combat power, times what an
// ignored-defence line moves against what they already ignore. So a cube and
// a star are compared in one currency.
double PowerOf(const GameState& state, const CubeBasis& basis,
               const PotentialTotals& totals) {
  EquipStats stats;
  PassiveOffense passives;
  StatsWith(state, basis, totals, &stats, &passives);
  return WorthOf(state, basis.yard, stats, passives);
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

CubeBasis CubeBasisFor(const GameState& state, const Yardstick& yard) {
  CubeBasis basis;
  // The BOSSING preset, because that is the fight this whole valuation is
  // aimed at. Read in the farming preset -- DerivedStatsFor's silent default --
  // the hyper stats and Inner Ability behind the character's ignored defence
  // were somebody else's, and a character over the defence wall priced their
  // cubes as one standing under it, which is to say at nothing.
  basis.derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                  Activity::kBossing);
  const EquipStats sources[] = {state.character.equip_stats(),
                                basis.derived.skill_stats};
  basis.raw = SumEquipStats(absl::MakeConstSpan(sources));
  basis.yard = yard;
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

// How long a run of cubes the shopper will consider. One is what it always
// offered; the rest are there so a line that needs a rank the item has not
// reached is priced at what reaching it costs rather than written off on the
// first roll.
constexpr int kCubeProgramLengths[] = {1, 4, 16, 64};

// Runs played out per slot. Few, because a run is the dear part of this file:
// every cube in one has to be valued to decide whether it is kept.
constexpr int kCubeRuns = 4;

constexpr int kCubeProgramLengthCount =
    sizeof(kCubeProgramLengths) / sizeof(kCubeProgramLengths[0]);

}  // namespace

namespace {

// Everything a cube into one slot is priced against, settled once for the
// slot rather than re-read per draw.
struct CubePricing {
  int level = 0;
  PotentialGroup group{};
  PotentialTotals others;
  PotentialTotals now;
  double standing = 0.0;
};

// What one cube into the slot is expected to add, averaged over draws and
// never less than nothing.
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

// What runs of each length leave behind, summed over kCubeRuns. Runs are
// PLAYED OUT rather than rolls counted, because a cube rolls against what the
// last one left: keeping a better roll can carry the item up a rank, and the
// line that clears a defence wall is one only a higher rank offers.
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
      // Keep-better, GMS's own offer: a roll worse than what the item holds is
      // declined, and the cube bought the chance.
      //
      // A RANK is taken even where the damage does not move. Under a defence
      // wall every roll is worth nothing, both sides being on the 1-damage
      // floor, so a run judged on damage alone never climbs a rank and never
      // reaches the line that clears the wall.
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

  // One cube first, and usually last. A longer run only ever beats a single
  // cube per meso when the single cube is worth NOTHING -- which is what a
  // defence wall does and nothing else does -- so the expensive part below is
  // skipped wherever the marginal roll already pays.
  double marginal =
      MarginalGain(state, basis, pricing, current, income, rng) * share;
  if (marginal > 0.0) {
    best.cubes = 1;
    best.gain = marginal;
    best.cost = kCubeCost;
    return best;
  }

  // Runs played out rather than rolls counted, because a cube rolls against
  // what the LAST one left: keeping a better roll can carry the item up a rank,
  // and the line that clears a defence wall is one only a higher rank offers.
  // A run of sixty priced as sixty independent rolls never sees that, which is
  // why it is worth the cost of playing them.
  std::vector<double> reached =
      PlayCubeRuns(state, basis, pricing, current, income, rng);

  for (int rung = 0; rung < kCubeProgramLengthCount; ++rung) {
    double expected = reached[rung] / kCubeRuns * share;
    int64_t cost = static_cast<int64_t>(kCubeProgramLengths[rung]) * kCubeCost;
    if (expected <= 0.0) {
      continue;
    }
    // Cross-multiplied rather than divided, and the empty run loses to
    // anything: with a cost of zero it would otherwise tie every length and
    // keep them all out.
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
  // A rank where the damage did not move, on the same terms BestCubeProgram
  // priced the run: under a defence wall every roll is worth nothing, and an
  // accept rule reading damage alone would throw away the rank-up the run was
  // bought for. The two have to agree or the shopper pays for a program it
  // then declines -- which it did, 2,484 cubes and none kept.
  return gain >= 0.0 &&
         rolled.rank() > item->equip_state().main_potential().rank();
}

// Whether the character could ever pay for `proto`, as against whether the
// catalog lists it. A tier priced in a token is only a prospect once one of
// that token has dropped: the AbsoLab weapon costs coins Damien and Lotus
// alone hand out, so to a character who cannot clear them it is not the next
// weapon -- it is scenery, and the piece in their hand is the one they keep.
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
    // Which weapon a branch swings is a measurement rather than a level, so
    // only a longer ladder of the same type replaces one -- see
    // SettledWeaponType.
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
