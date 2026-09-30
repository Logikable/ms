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

}  // namespace

namespace {

// The gains of one rank's sampled rolls, sorted, and the chance a roll at that
// rank ranks up instead.
struct RankDraws {
  PotentialRank rank = POTENTIAL_RANK_UNSPECIFIED;
  double rank_up = 0.0;
  std::vector<double> gains;
};

double MeanMax(const std::vector<double>& gains, double floor) {
  double total = 0.0;
  for (double gain : gains) {
    total += std::max(gain, floor);
  }
  return total / gains.size();
}

// The r with (1 - q) E[max(X, r)] + q carried - toll = r, where `carried` is
// what a rank-up is worth. Bisected: the left side minus r falls as r rises.
double ReserveFor(const RankDraws& draws, double carried, double toll) {
  const double q = draws.rank_up;
  auto excess = [&](double r) {
    const double stay = draws.gains.empty() ? 0.0 : MeanMax(draws.gains, r);
    return (1.0 - q) * stay + q * carried - toll - r;
  };
  const double low_gain = draws.gains.empty() ? carried : draws.gains.front();
  const double high_gain = draws.gains.empty() ? carried : draws.gains.back();
  double lo = std::min(low_gain, carried) - toll - 1.0;
  double hi = std::max(high_gain, carried);
  for (int step = 0; step < 100 && lo < hi; ++step) {
    const double mid = lo + (hi - lo) / 2;
    if (mid <= lo || mid >= hi) {
      break;
    }
    (excess(mid) > 0.0 ? lo : hi) = mid;
  }
  // With no rank above, the rule must stop on some roll it can draw.
  return q == 0.0 ? std::min(hi, high_gain) : hi;
}

// Where a rank's draws fall against its reservation value: the share that
// rolls on, and the mean gain of the ones the rule stops on, times their
// share.
struct Split {
  double below = 0.0;
  double stopped = 0.0;
};

Split SplitAt(const RankDraws& draws, double reserve) {
  Split split;
  for (double gain : draws.gains) {
    if (gain < reserve) {
      split.below += 1.0;
    } else {
      split.stopped += gain;
    }
  }
  if (!draws.gains.empty()) {
    split.below /= draws.gains.size();
    split.stopped /= draws.gains.size();
  }
  return split;
}

// Samples every rank from the piece's own up. A piece with no potential
// starts on a pseudo-rank that always "ranks up" to Rare, as its first cube
// always gives a Rare potential.
std::vector<RankDraws> SampleRanks(const GameState& state,
                                   const CubeBasis& basis,
                                   const CubePricing& pricing,
                                   PotentialRank held, double share,
                                   const CubeIncome& income,
                                   std::mt19937& rng) {
  std::vector<RankDraws> ranks;
  if (held == POTENTIAL_RANK_UNSPECIFIED) {
    ranks.push_back({held, 1.0, {}});
    held = POTENTIAL_RANK_RARE;
  }
  for (int rank = held; rank <= POTENTIAL_RANK_LEGENDARY; ++rank) {
    RankDraws draws;
    draws.rank = static_cast<PotentialRank>(rank);
    draws.rank_up = PotentialRankUpChance(pricing.cube, draws.rank);
    draws.gains.reserve(kCubeSamples);
    for (int i = 0; i < kCubeSamples; ++i) {
      draws.gains.push_back(
          share *
          GainOf(state, basis, pricing,
                 RollPotential(pricing.cube, pricing.group, draws.rank, rng),
                 income));
    }
    std::sort(draws.gains.begin(), draws.gains.end());
    ranks.push_back(std::move(draws));
  }
  return ranks;
}

}  // namespace

CubeProgram BestCubeProgram(const GameState& state, const CubeBasis& basis,
                            StatPreset gear, EquipSlot slot, CubeType cube,
                            const CubeIncome& income, std::mt19937& rng) {
  CubeProgram program;
  const EquipInstance* item = Worn(state, gear, slot);
  if (item == nullptr || !item->CanCube()) {
    return program;
  }
  const Cube& shelf = CubeOf(cube);
  const PotentialRank held =
      PotentialOf(item->equip_state(), shelf.track).rank();
  CubePricing pricing = PricingFor(state, basis, *item, slot, shelf.track);
  pricing.cube = cube;
  if (!pricing.bossed && !pricing.farmed) {
    return program;
  }
  // A farm-only piece is kept however the boss gear changes.
  program.share =
      pricing.bossed && Replaceable(state, slot)
          ? static_cast<double>(kReplaceableNumerator) / kReplaceableDenominator
          : 1.0;
  const std::vector<RankDraws> ranks =
      SampleRanks(state, basis, pricing, held, program.share, income, rng);
  const double toll = income.power_per_meso * shelf.cost;

  // Legendary down: each rank's rule needs the one above's, and the run's
  // cubes and gain need the same, so both are carried down together.
  double carried = 0.0;  // E[max(X, r)] one rank up
  double above_cubes = 0.0;
  double above_gain = 0.0;
  Split above;
  for (int i = ranks.size() - 1; i >= 0; --i) {
    const RankDraws& draws = ranks[i];
    const double q = draws.rank_up;
    const double reserve = ReserveFor(draws, carried, toll);
    const Split here = SplitAt(draws, reserve);
    const double stay = (1.0 - q) * here.below;
    const double cubes = (1.0 + q * above.below * above_cubes) / (1.0 - stay);
    const double gain = ((1.0 - q) * here.stopped +
                         q * (above.stopped + above.below * above_gain)) /
                        (1.0 - stay);
    program.reserve[draws.rank] = reserve;
    carried = draws.gains.empty() ? 0.0 : MeanMax(draws.gains, reserve);
    above_cubes = cubes;
    above_gain = gain;
    above = here;
  }
  // The piece's own lines are the zero every gain is measured from, so a
  // reservation value at or below zero means no roll pays.
  if (program.reserve[ranks.front().rank] <= 0.0) {
    return program;
  }
  program.cubes = above_cubes;
  program.gain = above_gain;
  program.cost = static_cast<int64_t>(std::llround(above_cubes * shelf.cost));
  return program;
}

bool WorthTaking(const GameState& state, const CubeBasis& basis,
                 StatPreset gear, EquipSlot slot, PotentialTrack track,
                 const Potential& rolled, const CubeIncome& income,
                 const CubeProgram& program) {
  const EquipInstance* item = Worn(state, gear, slot);
  if (item == nullptr) {
    return false;
  }
  const PotentialRank held = PotentialOf(item->equip_state(), track).rank();
  const double gain =
      program.share * GainOf(state, basis,
                             PricingFor(state, basis, *item, slot, track),
                             rolled, income);
  const double now = std::max(0.0, program.reserve[held]);
  const double then = std::max(gain, program.reserve[rolled.rank()]);
  // Within the same state, the better lines: they are what the piece keeps if
  // the rule is never run again.
  return then > now || (then == now && gain > 0.0);
}

// Whether the character could ever buy `proto`. A tier priced in tokens only
// counts once one of its tokens has dropped, so a character who can't clear
// Damien or Lotus keeps the weapon in hand.
bool WithinReach(const GameState& state, const EquipPrototype& proto) {
  if (proto.token_prices().empty()) {
    return true;
  }
  for (const TokenPrice& price : proto.token_prices()) {
    std::map<std::string, ItemPrototype>::const_iterator token =
        state.items.find(price.token_item());
    if (token != state.items.end() &&
        state.character.CountItem(token->second.name()) > 0) {
      return true;
    }
  }
  return false;
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
