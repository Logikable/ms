#include "src/character/v_matrix.h"

#include <algorithm>
#include <cstdint>
#include <random>

#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The three level bands a common or job node goes through, and the cost per
// level in each. The first level is priced separately (free for a job node, 7
// for a common node), and an archetype node is priced like a common one.
constexpr int kBandTop[] = {10, 20, 30};
constexpr int kBandCost[] = {4, 6, 9};

// The level where a boost node's price doubles, and the cost on either side.
constexpr int kBoostStep = 40;

}  // namespace

int MaxVNodeLevel(VNodeKind kind) {
  switch (kind) {
    case V_NODE_KIND_COMMON:
    case V_NODE_KIND_ARCHETYPE:
    case V_NODE_KIND_JOB:
      return 30;
    case V_NODE_KIND_BOOST:
      return 60;
    default:
      return 0;
  }
}

int VNodeStepCost(VNodeKind kind, int level) {
  if (level < 1 || level > MaxVNodeLevel(kind)) {
    return 0;
  }
  if (kind == V_NODE_KIND_BOOST) {
    return level <= kBoostStep ? 1 : 2;
  }
  if (level == 1) {
    return kind == V_NODE_KIND_JOB ? 0 : 7;
  }
  for (int band = 0; band < 3; ++band) {
    if (level <= kBandTop[band]) {
      return kBandCost[band];
    }
  }
  return 0;
}

int VNodeCost(VNodeKind kind, int from, int to) {
  int total = 0;
  for (int level = std::max(from + 1, 1); level <= to; ++level) {
    total += VNodeStepCost(kind, level);
  }
  return total;
}

double VPointsPerKill(double item_drop_pct) {
  return std::min(1.0,
                  kVPointDropChance * (1.0 + std::max(0.0, item_drop_pct)));
}

int64_t RollMobVPoints(int64_t kills, double item_drop_pct, std::mt19937& rng) {
  if (kills <= 0) {
    return 0;
  }
  std::binomial_distribution<int64_t> paying(kills,
                                             VPointsPerKill(item_drop_pct));
  return paying(rng);
}

}  // namespace ms
