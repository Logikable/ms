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
//
// The Wild Totem's 2,000 a second is most of what a bare Lv220 earns off the
// map, and the kills it buys are worth about that again -- so it pays for
// itself on its own and turns a profit on whatever %meso the character wears.
// Its billion is the four days of farming that makes renting it a decision
// for a while rather than a formality.

// The last line of each is where the buff pays out, which is the fact a player
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
