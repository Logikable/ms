#include "analysis/sim_format.h"

#include <cstdio>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/character/exp_table.h"

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

std::vector<int> ParseLevels(const std::string& spec,
                             const std::string& flag_name) {
  std::vector<int> levels;
  std::string digits;
  for (int i = 0; i <= static_cast<int>(spec.size()); ++i) {
    if (i == static_cast<int>(spec.size()) || spec[i] == ',') {
      if (!digits.empty()) {
        levels.push_back(std::stoi(digits));
        digits.clear();
      }
    } else if (spec[i] != ' ') {
      digits.push_back(spec[i]);
    }
  }
  if (levels.empty()) {
    LOG(FATAL) << flag_name << " named no levels";
  }
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    if (levels[i] < 2 || levels[i] > kMaxLevel) {
      LOG(FATAL) << flag_name << " level " << levels[i] << " is outside 2.."
                 << kMaxLevel;
    }
  }
  return levels;
}

}  // namespace ms
