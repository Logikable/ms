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
constexpr ConsumableInfo kConsumables[] = {
    {CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, "Wealth Acquisition Potion",
     kConsumableUnlockLevel, 1'000, /*per_second=*/true, 100'000'000},
    {CONSUMABLE_TYPE_EXTREME_GREEN_POTION, "Extreme Green Potion", 190,
     1'000'000, /*per_second=*/false, 250'000'000},
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
