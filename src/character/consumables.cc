#include "src/character/consumables.h"

#include "absl/types/span.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// 1,000 a second against 100m is a day and a bit of farming, so the permanent
// Wealth Acquisition Potion pays for itself early in a character's life. The
// Extreme Green Potion's 250m against 1m a fight is the other end of the
// deal: bosses are locked to the day, so buying it outright is a decision a
// player takes with a long climb still ahead of them.

// The last line of each is where the pot pays out, which is the fact a player
// weighing the rent needs before the numbers above it.
constexpr const char* kWealthEffects[] = {
    "+20% Meso Obtained",
    "+20% Item Drop Rate",
    "1.2x Meso Multiplier",
    "Farming only",
};
constexpr const char* kGreenEffects[] = {
    "+1 Attack Speed",
    "May exceed the attack speed cap",
    "Boss fights only",
};

constexpr ConsumableInfo kConsumables[] = {
    {CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, "Wealth Acquisition Potion",
     kConsumableUnlockLevel, 1'000, /*per_second=*/true, 100'000'000,
     absl::MakeConstSpan(kWealthEffects)},
    {CONSUMABLE_TYPE_EXTREME_GREEN_POTION, "Extreme Green Potion", 190,
     1'000'000, /*per_second=*/false, 250'000'000,
     absl::MakeConstSpan(kGreenEffects)},
};

}  // namespace

absl::Span<const ConsumableInfo> AllConsumables() {
  return absl::MakeConstSpan(kConsumables);
}

const ConsumableInfo* ConsumableInfoFor(ConsumableType type) {
  for (const ConsumableInfo& info : kConsumables) {
    if (info.type == type) {
      return &info;
    }
  }
  return nullptr;
}

}  // namespace ms
