#include "src/item/star_force_cost.h"

#include <cmath>
#include <cstdint>

#include "src/item/equip_instance.h"

namespace ms {
namespace {

// Every price starts here, so the first star on a low level item is never
// free.
constexpr int64_t kStarForceBaseCost = 1000;

// GMS quotes every price to the hundred.
constexpr int64_t kRounding = 100;

// Below this the cost climbs by a plain multiple of the star count. At and
// above it, GMS switches to (stars + 1) ^ 2.7 over a divisor that changes
// nearly every star -- the exponent is what makes the last few unaffordable.
constexpr int kFirstExponentStar = 10;

// The divisor for each star from 10 upward. A smaller one is a dearer star,
// so the three rises (18, 19 and the run to 22) are the walls players talk
// about, and the drops at 15, 20 and 22 are the shelves between them.
constexpr int kExponentDivisors[] = {400, 220, 150, 110, 75,  200,
                                     200, 150, 70,  45,  200, 125};

// What every star past the table costs. GMS runs one formula from 22 to 30.
constexpr int kDivisorPastTheTable = 200;

int Divisor(int stars) {
  int index = stars - kFirstExponentStar;
  if (index >= static_cast<int>(sizeof(kExponentDivisors) / sizeof(int))) {
    return kDivisorPastTheTable;
  }
  return kExponentDivisors[index];
}

int64_t RoundToHundred(double amount) {
  return static_cast<int64_t>(std::llround(amount / kRounding)) * kRounding;
}

}  // namespace

int64_t StarForceCost(int required_level, int stars) {
  if (required_level <= 0 || stars < 0 || stars >= kMaxStarForce) {
    return 0;
  }
  double level_cubed = std::pow(static_cast<double>(required_level), 3.0);
  double raw;
  if (stars < kFirstExponentStar) {
    raw = level_cubed * (stars + 1) / 25.0;
  } else {
    raw = level_cubed * std::pow(stars + 1.0, 2.7) / Divisor(stars);
  }
  return RoundToHundred(kStarForceBaseCost + std::llround(raw));
}

}  // namespace ms
