#include "analysis/exact.h"

#include <algorithm>
#include <atomic>

namespace ms {
namespace {

std::atomic<bool> exact{false};

}  // namespace

bool Exact() {
  return exact.load(std::memory_order_relaxed);
}

void SetExact(bool value) {
  exact.store(value, std::memory_order_relaxed);
}

double CappedCycle(double cycle) {
  return Exact() ? cycle : std::min(cycle, kCycleCapSeconds);
}

}  // namespace ms
