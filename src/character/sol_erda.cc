#include "src/character/sol_erda.h"

#include <algorithm>
#include <cstdint>
#include <random>

namespace ms {
namespace {

double Lifted(double chance, double item_drop_pct) {
  return std::min(1.0, chance * (1.0 + std::max(0.0, item_drop_pct)));
}

int64_t Roll(int64_t kills, double chance, std::mt19937& rng) {
  if (kills <= 0) {
    return 0;
  }
  std::binomial_distribution<int64_t> paying(kills, chance);
  return paying(rng);
}

}  // namespace

double FragmentsPerKill(double item_drop_pct) {
  return Lifted(kFragmentDropChance, item_drop_pct);
}

double SolErdaEnergyPerKill(double item_drop_pct) {
  return kEnergyPerDrop * Lifted(kEnergyDropChance, item_drop_pct);
}

int64_t RollMobFragments(int64_t kills, double item_drop_pct,
                         std::mt19937& rng) {
  return Roll(kills, FragmentsPerKill(item_drop_pct), rng);
}

int64_t RollMobSolErdaEnergy(int64_t kills, double item_drop_pct,
                             std::mt19937& rng) {
  return kEnergyPerDrop *
         Roll(kills, Lifted(kEnergyDropChance, item_drop_pct), rng);
}

}  // namespace ms
