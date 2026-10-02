#include "analysis/flame_plan.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "analysis/cube_plan.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Draws used to value one flame, as for a cube.
constexpr int kFlameSamples = kCubeSamples;

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

double MeanMax(const std::vector<double>& gains, double floor) {
  double total = 0.0;
  for (double gain : gains) {
    total += std::max(gain, floor);
  }
  return total / gains.size();
}

// The r with E[max(X, r)] - toll = r, bisected, and never above the best draw:
// the rule must stop on some roll it can draw.
double ReserveFor(const std::vector<double>& gains, double toll) {
  double lo = gains.front() - toll - 1.0;
  double hi = gains.back();
  for (int step = 0; step < 100 && lo < hi; ++step) {
    const double mid = lo + (hi - lo) / 2;
    if (mid <= lo || mid >= hi) {
      break;
    }
    (MeanMax(gains, mid) - toll - mid > 0.0 ? lo : hi) = mid;
  }
  return std::min(hi, gains.back());
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
                              double power_per_meso, std::mt19937& rng) {
  FlameProgram program;
  std::vector<double> gains =
      FlameGains(state, basis, slot, flame, kFlameSamples, rng);
  if (gains.empty()) {
    return program;
  }
  program.share =
      Replaceable(state, slot)
          ? static_cast<double>(kReplaceableNumerator) / kReplaceableDenominator
          : 1.0;
  for (double& gain : gains) {
    gain *= program.share;
  }
  const int64_t price = FlameOf(flame).cost;
  program.reserve = ReserveFor(gains, power_per_meso * price);
  // The piece's own lines are the zero every gain is measured from.
  if (program.reserve <= 0.0) {
    return program;
  }
  double below = 0.0;
  double stopped = 0.0;
  for (double gain : gains) {
    if (gain < program.reserve) {
      below += 1.0;
    } else {
      stopped += gain;
    }
  }
  below /= gains.size();
  stopped /= gains.size();
  program.flames = 1.0 / (1.0 - below);
  program.gain = stopped / (1.0 - below);
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

bool FlameRun::Takes(const GameState& state, const FlameLines& rolled) const {
  const EquipInstance* item = Flammable(state, slot_);
  return item != nullptr && priced_ != nullptr &&
         GainOf(rolled) > GainOf(item->equip_state().flame());
}

}  // namespace ms
