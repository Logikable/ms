#include "analysis/sim_format.h"

#include <cstdio>

namespace ms {

void FormatShort(double value, char* out, int size) {
  const char* suffix[] = {"Q", "T", "B", "M", "k"};
  const double scale[] = {1e15, 1e12, 1e9, 1e6, 1e3};
  for (int i = 0; i < 5; ++i) {
    if (value >= scale[i]) {
      snprintf(out, size, "%.3g%s", value / scale[i], suffix[i]);
      return;
    }
  }
  snprintf(out, size, "%.0f", value);
}

}  // namespace ms
