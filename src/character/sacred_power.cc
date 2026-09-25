#include "src/character/sacred_power.h"

#include <algorithm>

#include "src/character/arcane_force.h"

namespace ms {
namespace {

// Where each side of the table stops.
constexpr int kMinDealtPct = 5;
constexpr int kMaxBonusPct = 25;
// How far short a character can be and still only take half again.
constexpr int kHalfAgainGap = 50;

}  // namespace

ForceFactors SacredFactorsFor(int owned, int required) {
  if (required <= 0) {
    return ForceFactors();
  }
  int gap = std::max(0, owned) - required;
  ForceFactors factors;
  if (gap < 0) {
    factors.damage_dealt = std::max(kMinDealtPct, 100 + gap) / 100.0;
    factors.damage_taken = -gap <= kHalfAgainGap ? 1.5 : 2.0;
  } else {
    factors.damage_dealt = (100 + std::min(kMaxBonusPct, gap / 2)) / 100.0;
  }
  return factors;
}

}  // namespace ms
