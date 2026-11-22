#include "analysis/parallel.h"

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

namespace ms {
namespace {

TEST(ParallelForTest, RunsEveryIndexOnce) {
  std::vector<int> runs(1000, 0);
  ParallelFor(static_cast<int>(runs.size()), [&](int i) { ++runs[i]; });
  for (int count : runs) {
    EXPECT_EQ(count, 1);
  }
}

// A sweep's branches each price their shelf through it, so a call from inside
// a body must finish every inner index before the outer one goes on.
TEST(ParallelForTest, ACallInsideABodyFinishesBeforeItReturns) {
  constexpr int kOuter = 24;
  constexpr int kInner = 50;
  std::vector<int> sums(kOuter, 0);
  ParallelFor(kOuter, [&](int i) {
    std::vector<int> inner(kInner, 0);
    ParallelFor(kInner, [&](int j) { inner[j] = j; });
    for (int value : inner) {
      sums[i] += value;
    }
  });
  for (int sum : sums) {
    EXPECT_EQ(sum, kInner * (kInner - 1) / 2);
  }
}

}  // namespace
}  // namespace ms
