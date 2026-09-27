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

const EquipInstance* Worn(const GameState& state, StatPreset gear,
                          EquipSlot slot) {
  return state.character.WornAt(gear, slot);
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

// Potential totals from every potential `gear` wears except the one `track`
// names on `item`. A cube's roll is added on top, so the rest is summed once
// per slot rather than once per draw.
PotentialTotals PotentialsBut(const CharacterInstance& character,
                              StatPreset gear, const EquipInstance* item,
                              PotentialTrack track) {
  PotentialTotals totals;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       character.equipped(gear)) {
    int level = entry.second->prototype().required_level();
    for (PotentialTrack held :
         {PotentialTrack::kMain, PotentialTrack::kBonus}) {
      if (entry.second == item && held == track) {
        continue;
      }
      AddPotential(PotentialOf(entry.second->equip_state(), held), level,
                   totals);
    }
  }
  return totals;
}

// Stats the character wears and is granted with `totals` in place of the worn
// potentials. Returns the two halves WorthOf takes, not a folded OffenseStats,
// so cubes are measured on the same terms as stars.
void StatsWith(const GameState& state, const CubeBasis& basis,
               const PotentialTotals& totals, EquipStats* out,
               PassiveOffense* out_passives) {
  const CharacterInstance& character = state.character;
  const PotentialTotals& worn = character.potential_totals(basis.derived.gear);
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
  DerivedStats after = basis.farm;
  after.equip_meso_pct += totals.meso_pct - worn.meso_pct;
  after.item_drop_pct += totals.item_drop_pct - worn.item_drop_pct;
  double extra = income.rate(MesoBonus(after), after.item_drop_pct) -
                 income.rate(MesoBonus(basis.farm), basis.farm.item_drop_pct);
  return extra * income.seconds_left * income.power_per_meso;
}

}  // namespace

CubeBasis CubeBasisFor(const GameState& state, const Yardstick& yard) {
  CubeBasis basis;
  // Use the bossing preset: DerivedStatsFor defaults to farming, whose Hyper
  // Stats and Inner Ability differ on ignored defence.
  basis.derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                  Activity::kBossing);
  basis.farm = DerivedStatsFor(state.character, state.skills, {}, {},
                               Activity::kFarming);
  const EquipStats sources[] = {state.character.equip_stats(basis.derived.gear),
                                basis.derived.skill_stats};
  basis.raw = SumEquipStats(absl::MakeConstSpan(sources));
  basis.yard = yard;
  return basis;
}

namespace {

// Everything a cube on one piece is valued against, computed once per piece
// rather than per draw. A piece counts toward boss power if boss fights wear
// it and toward income if farming does; a piece worn by both counts twice.
struct CubePricing {
  CubeType cube = CubeType::kRed;
  int level = 0;
  PotentialGroup group{};
  bool bossed = false;
  bool farmed = false;
  PotentialTotals boss_others;
  PotentialTotals farm_others;
  PotentialTotals farm_now;
  double standing = 0.0;
};

CubePricing PricingFor(const GameState& state, const CubeBasis& basis,
                       const EquipInstance& item, EquipSlot slot,
                       PotentialTrack track) {
  const CharacterInstance& character = state.character;
  CubePricing pricing;
  pricing.level = item.prototype().required_level();
  pricing.group = PotentialGroupOf(slot);
  pricing.bossed = character.WornAt(kBossGear, slot) == &item;
  pricing.farmed = character.WornAt(kFarmGear, slot) == &item;
  const Potential& held = PotentialOf(item.equip_state(), track);
  pricing.boss_others = PotentialsBut(character, kBossGear, &item, track);
  PotentialTotals boss_now = pricing.boss_others;
  AddPotential(held, pricing.level, boss_now);
  pricing.standing = PowerOf(state, basis, boss_now);
  pricing.farm_others = PotentialsBut(character, kFarmGear, &item, track);
  pricing.farm_now = pricing.farm_others;
  AddPotential(held, pricing.level, pricing.farm_now);
  return pricing;
}

// Value of `rolled` replacing what the piece holds: power plus income, in the
// shelf's single currency.
double GainOf(const GameState& state, const CubeBasis& basis,
              const CubePricing& pricing, const Potential& rolled,
              const CubeIncome& income) {
  double gain = 0.0;
  if (pricing.bossed) {
    PotentialTotals totals = pricing.boss_others;
    AddPotential(rolled, pricing.level, totals);
    gain += PowerOf(state, basis, totals) - pricing.standing;
  }
  if (pricing.farmed) {
    PotentialTotals totals = pricing.farm_others;
    AddPotential(rolled, pricing.level, totals);
    gain += IncomeGain(basis, pricing.farm_now, totals, income);
  }
  return gain;
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

// Expected gain of one cube on the slot, averaged over draws and never below
// zero.
double MarginalGain(const GameState& state, const CubeBasis& basis,
                    const CubePricing& pricing, const Potential& current,
                    const CubeIncome& income, std::mt19937& rng) {
  double total = 0.0;
  for (int draw = 0; draw < kCubeSamples; ++draw) {
    total += std::max(
        0.0, GainOf(state, basis, pricing,
                    CubePotential(current, pricing.cube, pricing.group, rng),
                    income));
  }
  return total / kCubeSamples;
}

// Total gain left by runs of each length, summed over kCubeRuns. Runs are
// played out because each cube rolls against what the last one left: a kept
// roll can raise the rank, and the line that clears a defence wall may only
// exist there.
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
      Potential rolled = CubePotential(held, pricing.cube, pricing.group, rng);
      double gain = GainOf(state, basis, pricing, rolled, income);
      // Keep-better, as GMS offers. A higher rank is kept even when damage
      // doesn't change: under a defence wall every roll deals the 1-damage
      // floor, so a run judged on damage alone would never climb.
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
                            StatPreset gear, EquipSlot slot, CubeType cube,
                            const CubeIncome& income, std::mt19937& rng) {
  CubeProgram best;
  const EquipInstance* item = Worn(state, gear, slot);
  if (item == nullptr || !item->CanCube()) {
    return best;
  }
  const Cube& shelf = CubeOf(cube);
  const Potential& current = PotentialOf(item->equip_state(), shelf.track);
  CubePricing pricing = PricingFor(state, basis, *item, slot, shelf.track);
  pricing.cube = cube;

  // A farm-only piece is kept however the boss gear changes.
  double share =
      pricing.bossed && Replaceable(state, slot)
          ? static_cast<double>(kReplaceableNumerator) / kReplaceableDenominator
          : 1.0;

  // Try one cube first. A longer run only beats it per meso when the single
  // cube is worth nothing, which happens only under a defence wall.
  double marginal =
      MarginalGain(state, basis, pricing, current, income, rng) * share;
  if (marginal > 0.0) {
    best.cubes = 1;
    best.gain = marginal;
    best.cost = shelf.cost;
    return best;
  }

  // Play out runs; see PlayCubeRuns for why they can't be counted
  // independently.
  std::vector<double> reached =
      PlayCubeRuns(state, basis, pricing, current, income, rng);

  for (int rung = 0; rung < kCubeProgramLengthCount; ++rung) {
    double expected = reached[rung] / kCubeRuns * share;
    int64_t cost = static_cast<int64_t>(kCubeProgramLengths[rung]) * shelf.cost;
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

bool WorthTaking(const GameState& state, const CubeBasis& basis,
                 StatPreset gear, EquipSlot slot, PotentialTrack track,
                 const Potential& rolled, const CubeIncome& income) {
  const EquipInstance* item = Worn(state, gear, slot);
  if (item == nullptr) {
    return false;
  }
  const Potential& held = PotentialOf(item->equip_state(), track);
  double gain =
      GainOf(state, basis, PricingFor(state, basis, *item, slot, track), rolled,
             income);
  if (gain > 0.0) {
    return true;
  }
  // Accept a higher rank even when damage didn't change, on the same terms
  // BestCubeProgram priced the run. The two must agree, or the shopper pays for
  // a program and then declines every result.
  return gain >= 0.0 && rolled.rank() > held.rank();
}

// Whether the character could ever buy `proto`. A tier priced in tokens only
// counts once one of the tokens has dropped, so a character who can't clear
// Damien or Lotus keeps the weapon in hand.
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
  const EquipInstance* item = Worn(state, kBossGear, slot);
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
