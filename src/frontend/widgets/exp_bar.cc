#include "src/frontend/widgets/exp_bar.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/exp_table.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Decimal places for the EXP percentage, by job tier: a level that needs
// hundreds of times more EXP moves its bar hundreds of times more slowly.
int ExpPctDecimals(int level) {
  if (level < 60) {
    return 0;
  }
  if (level < 100) {
    return 1;
  }
  if (level < 200) {
    return 2;
  }
  if (level < 260) {
    return 3;
  }
  return 4;
}

}  // namespace

ftxui::Element ExpBar(const Character& character) {
  if (character.level() >= kTrialLevelCap) {
    return ProgressBar(1.0f, kTheme, "MAX");
  }
  int64_t exp = character.exp();
  int64_t tnl = ExpToNextLevel(character.level());
  float frac =
      tnl > 0 ? static_cast<float>(exp) / static_cast<float>(tnl) : 0.0f;
  double pct = tnl > 0
                   ? static_cast<double>(exp) * 100.0 / static_cast<double>(tnl)
                   : 0.0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%.*f%%", ExpPctDecimals(character.level()), pct);
  return ProgressBar(frac, kTheme,
                     FormatWithCommas(exp) + " (" + std::string(buf) + ")");
}

}  // namespace ms
