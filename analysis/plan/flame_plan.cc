#include "analysis/plan/flame_plan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "analysis/plan/cube_plan.h"
#include "analysis/plan/gain_distribution.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/item/equip_instance.h"
#include "src/item/upgrade/flame.h"
#include "src/item/upgrade/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

int FoldAttack(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + 1e-9));
}

// `into` plus `sign` times the fields a flame line can raise.
void AddFlameFields(EquipStats& into, const EquipStats& part, int sign) {
  into.set_str(into.str() + sign * part.str());
  into.set_dex(into.dex() + sign * part.dex());
  into.set_int_(into.int_() + sign * part.int_());
  into.set_luk(into.luk() + sign * part.luk());
  into.set_max_hp(into.max_hp() + sign * part.max_hp());
  into.set_attack(into.attack() + sign * part.attack());
  into.set_magic_attack(into.magic_attack() + sign * part.magic_attack());
  into.set_boss_damage(into.boss_damage() + sign * part.boss_damage());
}

// Everything a flame on one piece is valued against, computed once per piece.
struct FlamePricing {
  const GameState* state = nullptr;
  CubeBasis basis;
  const EquipPrototype* proto = nullptr;
  // The worn lines' flat stats and percents, which a roll replaces.
  EquipStats held_flat;
  FlamePercents held_pct;
  double standing = 0.0;
  // WorthOf's scratch copy of basis.passives.
  mutable PassiveOffense passives;

  // Damage with `lines` in place of the worn ones.
  double PowerWith(const FlameLines& lines) const {
    EquipStats flat = FlameStats(lines, *proto);
    AddFlameFields(flat, held_flat, -1);
    const FlamePercents pct = FlamePercentsOf(lines, *proto);
    const double all_stat = (pct.all_stat - held_pct.all_stat) / 100.0;
    const double damage = (pct.damage - held_pct.damage) / 100.0;

    PotentialTotals totals = basis.worn;
    for (double* share :
         {&totals.str_pct, &totals.dex_pct, &totals.int_pct, &totals.luk_pct}) {
      *share += all_stat;
    }
    const CharacterInstance& character = state->character;
    const EquipStats paid =
        PotentialStatGrant(character, basis.derived, totals, flat);
    const EquipStats& was = basis.derived.potential_stats;

    EquipStats stats = basis.raw;
    AddFlameFields(stats, flat, 1);
    stats.set_str(stats.str() + paid.str() - was.str());
    stats.set_dex(stats.dex() + paid.dex() - was.dex());
    stats.set_int_(stats.int_() + paid.int_() - was.int_());
    stats.set_luk(stats.luk() + paid.luk() - was.luk());
    stats.set_attack(FoldAttack(stats.attack(), basis.derived.attack_pct));
    stats.set_magic_attack(
        FoldAttack(stats.magic_attack(), basis.derived.magic_attack_pct));

    passives = basis.passives;
    passives.damage_pct += damage;
    return WorthOf(*state, basis.yard, stats, passives);
  }
};

FlamePricing PricingFor(const GameState& state, const CubeBasis& basis,
                        const EquipInstance& item) {
  FlamePricing pricing;
  pricing.state = &state;
  pricing.basis = basis;
  pricing.proto = &item.prototype();
  pricing.held_flat = FlameStats(item.equip_state().flame(), item.prototype());
  pricing.held_pct =
      FlamePercentsOf(item.equip_state().flame(), item.prototype());
  pricing.standing = pricing.PowerWith(item.equip_state().flame());
  return pricing;
}

// The r with E[max(X, r)] - toll = r, bisected, and never above the best roll:
// the rule must stop on some roll it can draw.
double ReserveFor(const GainDistribution& gains, double toll) {
  double lo = gains.low() - toll - 1.0;
  double hi = gains.high();
  for (int step = 0; step < 100 && lo < hi; ++step) {
    const double mid = lo + (hi - lo) / 2;
    if (mid <= lo || mid >= hi) {
      break;
    }
    (gains.MeanMax(mid) - toll - mid > 0.0 ? lo : hi) = mid;
  }
  return std::min(hi, gains.high());
}

// Bins a flame's rolls are counted into; see CountFlames.
constexpr int kFlameBins = 4096;

// n choose k, as a double: the counts here pass no int's range.
double Choose(int n, int k) {
  if (k < 0 || k > n) {
    return 0.0;
  }
  double ways = 1.0;
  for (int i = 0; i < k; ++i) {
    ways = ways * (n - i) / (i + 1);
  }
  return ways;
}

// Every roll of `flame` counted with its exact chance: kFlameLines distinct
// lines from the pool, each at an even tier. A roll is valued as the sum of its
// lines each valued alone, since four lines on one piece are small beside the
// character and barely move each other's worth; a line worth nothing at every
// tier is one of a crowd of dead lines. A reroll matching the worn lines is
// drawn again in GMS, a chance too small to count.
void CountFlames(const FlamePricing& pricing, FlameType flame,
                 GainDistribution& gains) {
  const Flame& shelf = FlameOf(flame);
  const std::vector<FlameStat> pool = FlamePool(*pricing.proto);
  const double bare = pricing.PowerWith(FlameLines());
  // Each live line's worth at each tier, over the bare piece.
  std::vector<std::array<double, kFlameTiers>> live;
  for (FlameStat stat : pool) {
    std::array<double, kFlameTiers> worth{};
    bool any = false;
    for (int t = 0; t < kFlameTiers; ++t) {
      FlameLines one;
      FlameLine* line = one.Add();
      line->set_stat(stat);
      line->set_tier(shelf.min_tier + t);
      worth[t] = pricing.PowerWith(one) - bare;
      any = any || worth[t] != 0.0;
    }
    if (any) {
      live.push_back(worth);
    }
  }
  // Rolls are binned by gain over the widest span they could cover, each bin
  // keeping its chance and mean gain: a weapon's pool counts tens of thousands
  // of rolls, and sorting them all cost more than the rest of a sweep's flames.
  double reach = 0.0;
  double depth = 0.0;
  for (const std::array<double, kFlameTiers>& worth : live) {
    reach += std::max(0.0, *std::max_element(worth.begin(), worth.end()));
    depth += std::min(0.0, *std::min_element(worth.begin(), worth.end()));
  }
  const double width = (reach - depth) / kFlameBins;
  std::vector<double> bin_chance(kFlameBins + 1, 0.0);
  std::vector<double> bin_mass(kFlameBins + 1, 0.0);
  const int n = static_cast<int>(pool.size());
  const int dead = n - static_cast<int>(live.size());
  const int lines = std::min(kFlameLines, n);
  const double rolls = Choose(n, lines);
  const double start = bare - pricing.standing;
  // Every set of live lines a roll could hold, with the rest dead: each such
  // set is as likely as the ways to fill the rest from the dead lines.
  std::vector<int> chosen;
  std::function<void(int)> pick = [&](int next) {
    const int fill = lines - static_cast<int>(chosen.size());
    const double chance = Choose(dead, fill) / rolls;
    if (chance > 0.0) {
      // The chosen lines' tiers, every combination equally likely.
      const int combos = static_cast<int>(
          std::pow(kFlameTiers, static_cast<double>(chosen.size())));
      for (int combo = 0; combo < combos; ++combo) {
        double gain = start;
        for (int c = 0, rest = combo; c < static_cast<int>(chosen.size());
             ++c, rest /= kFlameTiers) {
          gain += live[chosen[c]][rest % kFlameTiers];
        }
        const int bin =
            width > 0.0 ? static_cast<int>((gain - start - depth) / width) : 0;
        const int at = std::clamp(bin, 0, kFlameBins);
        bin_chance[at] += chance / combos;
        bin_mass[at] += gain * chance / combos;
      }
    }
    if (static_cast<int>(chosen.size()) == lines) {
      return;
    }
    for (int i = next; i < static_cast<int>(live.size()); ++i) {
      chosen.push_back(i);
      pick(i + 1);
      chosen.pop_back();
    }
  };
  pick(0);
  for (int at = 0; at <= kFlameBins; ++at) {
    if (bin_chance[at] > 0.0) {
      gains.Add(bin_mass[at] / bin_chance[at], bin_chance[at]);
    }
  }
}

const EquipInstance* Flammable(const GameState& state, EquipSlot slot) {
  const EquipInstance* item = state.character.WornAt(kBossGear, slot);
  return item != nullptr && item->CanFlame() ? item : nullptr;
}

}  // namespace

std::vector<double> FlameGains(const GameState& state, const CubeBasis& basis,
                               EquipSlot slot, FlameType flame, int samples,
                               std::mt19937& rng, double* standing) {
  std::vector<double> gains;
  const EquipInstance* item = Flammable(state, slot);
  if (item == nullptr) {
    return gains;
  }
  const FlamePricing pricing = PricingFor(state, basis, *item);
  if (standing != nullptr) {
    *standing = pricing.standing;
  }
  gains.reserve(samples);
  for (int i = 0; i < samples; ++i) {
    const FlameLines rolled =
        RollFlame(flame, item->prototype(), item->equip_state().flame(), rng);
    gains.push_back(pricing.PowerWith(rolled) - pricing.standing);
  }
  std::sort(gains.begin(), gains.end());
  return gains;
}

FlameProgram BestFlameProgram(const GameState& state, const CubeBasis& basis,
                              EquipSlot slot, FlameType flame,
                              double power_per_meso, std::mt19937& rng,
                              int samples) {
  FlameProgram program;
  const EquipInstance* item = Flammable(state, slot);
  if (item == nullptr) {
    return program;
  }
  GainDistribution gains;
  if (samples > 0) {
    for (double gain : FlameGains(state, basis, slot, flame, samples, rng)) {
      gains.Add(gain);
    }
  } else {
    CountFlames(PricingFor(state, basis, *item), flame, gains);
  }
  if (gains.empty()) {
    return program;
  }
  gains.Finish();
  program.share =
      Replaceable(state, slot)
          ? static_cast<double>(kReplaceableNumerator) / kReplaceableDenominator
          : 1.0;
  gains.Scale(program.share);
  const int64_t price = FlameOf(flame).cost;
  program.reserve = ReserveFor(gains, power_per_meso * price);
  // The piece's own lines are the zero every gain is measured from.
  if (program.reserve <= 0.0) {
    return program;
  }
  const double below = gains.Below(program.reserve);
  program.flames = 1.0 / (1.0 - below);
  program.gain = gains.Stopped(program.reserve) / (1.0 - below);
  program.cost = static_cast<int64_t>(std::llround(program.flames * price));
  return program;
}

struct FlameRun::Priced {
  FlamePricing pricing;
};

FlameRun::FlameRun(const GameState& state, const Yardstick& yard,
                   EquipSlot slot, const FlameProgram& program)
    : slot_(slot), program_(program) {
  const EquipInstance* item = Flammable(state, slot);
  if (item != nullptr) {
    priced_ = std::make_unique<Priced>();
    priced_->pricing = PricingFor(state, CubeBasisFor(state, yard), *item);
  }
}

FlameRun::~FlameRun() = default;

double FlameRun::GainOf(const FlameLines& lines) const {
  return program_.share *
         (priced_->pricing.PowerWith(lines) - priced_->pricing.standing);
}

bool FlameRun::Continues(const GameState& state) const {
  const EquipInstance* item = Flammable(state, slot_);
  return item != nullptr && priced_ != nullptr &&
         GainOf(item->equip_state().flame()) < program_.reserve;
}

bool FlameRun::Ahead(const GameState& state) const {
  const EquipInstance* item = Flammable(state, slot_);
  return item != nullptr && priced_ != nullptr &&
         GainOf(item->equip_state().flame()) > 0.0;
}

bool FlameRun::Takes(const GameState& state, const FlameLines& rolled) const {
  const EquipInstance* item = Flammable(state, slot_);
  return item != nullptr && priced_ != nullptr &&
         GainOf(rolled) > GainOf(item->equip_state().flame());
}

}  // namespace ms
