#include "analysis/parallel.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace ms {

void ParallelFor(int count, const std::function<void(int)>& body) {
  if (count <= 0) {
    return;
  }
  unsigned int cores = std::thread::hardware_concurrency();
  int threads = std::min(count, static_cast<int>(std::max(1u, cores)));
  if (threads == 1) {
    for (int i = 0; i < count; ++i) {
      body(i);
    }
    return;
  }
  // Indices are taken one at a time rather than sliced up front, because the
  // bodies are not the same size -- one branch climbs for two simulated days
  // and another for one, and a thread handed the slow half would be alone by
  // the end of it.
  std::atomic<int> next(0);
  std::vector<std::thread> pool;
  pool.reserve(threads);
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&body, &next, count] {
      for (int i = next++; i < count; i = next++) {
        body(i);
      }
    });
  }
  for (std::thread& thread : pool) {
    thread.join();
  }
}

}  // namespace ms
