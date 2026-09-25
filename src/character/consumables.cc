#include "src/character/consumables.h"

#include "absl/types/span.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// 1,000 a second against 100m is a little over a day of farming, so the Wealth
// Acquisition Potion pays for itself early. The Extreme Green Potion's 250m
// against 1m a fight is the other end, since bosses are limited per day.
//
// The Wild Totem's 2,000 a second is most of what a bare Lv220 earns on a map,
// and the extra kills it buys are worth as much again, so it pays for itself.
// Its billion price is four days of rent, which keeps renting a real decision.

// The last line of each is how the buff pays out, which is what a player
// weighing the rent needs to know before the numbers above it.
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
constexpr const char* kTotemEffects[] = {
    "Halves respawn time to 3.78s",
    "Farming only",
};

constexpr ConsumableInfo kConsumables[] = {
    {CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, "Wealth Acquisition Potion",
     kConsumableUnlockLevel, 1'000, /*per_second=*/true, 100'000'000,
     absl::MakeConstSpan(kWealthEffects)},
    {CONSUMABLE_TYPE_EXTREME_GREEN_POTION, "Extreme Green Potion", 190,
     1'000'000, /*per_second=*/false, 250'000'000,
     absl::MakeConstSpan(kGreenEffects)},
    {CONSUMABLE_TYPE_WILD_TOTEM, "Wild Totem", 220, 2'000,
     /*per_second=*/true, 1'000'000'000, absl::MakeConstSpan(kTotemEffects)},
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
