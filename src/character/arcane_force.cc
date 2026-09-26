#include "src/character/arcane_force.h"

#include <algorithm>

namespace ms {
namespace {

// One row of GMS's table: the percent of the requirement where it starts, and
// the factors from there up to the next row.
struct ForceRow {
  int met_pct;
  double dealt;
  double taken;
};

// Read from the bottom up, so a character with nothing lands on the first row
// and one with 1.5x the requirement on the last. The top row's 0 taken is GMS's
// "monster hits for 1", which the damage floor provides.
constexpr ForceRow kForceTable[] = {
    {0, 0.10, 2.8},   {10, 0.30, 2.4},  {30, 0.60, 1.8},
    {50, 0.70, 1.6},  {70, 0.80, 1.4},  {100, 1.00, 1.0},
    {110, 1.10, 0.8}, {130, 1.30, 0.4}, {150, 1.50, 0.0},
};

}  // namespace

ForceFactors ArcaneFactorsFor(int owned, int required) {
  if (required <= 0) {
    return ForceFactors();
  }
  int met_pct = std::max(0, owned) * 100 / required;
  ForceFactors factors;
  for (const ForceRow& row : kForceTable) {
    if (met_pct >= row.met_pct) {
      factors.damage_dealt = row.dealt;
      factors.damage_taken = row.taken;
    }
  }
  return factors;
}

}  // namespace ms
