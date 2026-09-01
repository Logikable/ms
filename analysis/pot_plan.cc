#include "analysis/pot_plan.h"

#include <algorithm>
#include <cstdint>

#include "absl/types/span.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/consumables.h"
#include "src/combat/damage.h"
#include "src/combat/encounter.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// How far a permanent unlock has to clear its own price before it is worth
// the purse it takes. Twice over rather than once: the meso would otherwise
// have gone on gear, and a pot that only breaks even by the last day of the
// run is not worth taking a star off the weapon for.
constexpr double kBuyMargin = 2.0;

// The meso a second the encounter pays with the Wealth Acquisition Potion
// switched the way it is asked about. Switched back before returning: this is
// a question, not a move.
double RateWithWealthPotion(GameState& state, bool on,
                            absl::Span<const Mob* const> mobs,
                            absl::Span<const double> kills_per_second) {
  CharacterInstance& character = state.character;
  bool was =
      character.ConsumableActive(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  if (was != on) {
    character.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  }
  DerivedStats derived = DerivedStatsFor(character, state.skills);
  double rate =
      PotMesoPerSecond(mobs, kills_per_second, MesoBonus(derived),
                       derived.meso_final_mult, derived.item_drop_pct);
  if (was != on) {
    character.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  }
  return rate;
}

// Whether the Extreme Green Potion would actually buy the character a stage.
// False for one already at the fastest the formula models, and for one
// holding nothing to swing.
bool RaisesTheStage(GameState& state) {
  const EquipPrototype* weapon = EquippedWeapon(state);
  if (weapon == nullptr) {
    return false;
  }
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  int base = BaseAttackSpeedStage(state.character.proto().job(),
                                  weapon->attack_speed());
  return AttackSpeedStage(base, derived.attack_speed_bonus,
                          kGreenPotionAttackSpeed) >
         AttackSpeedStage(base, derived.attack_speed_bonus, 0);
}

// Switches `type` to `on`, which is a no-op when it is already there.
void SetPot(CharacterInstance& character, ConsumableType type, bool on) {
  if (character.ConsumableActive(type) != on) {
    character.ToggleConsumable(type);
  }
}

// Whether the rent left to pay clears the permanent price with room to spare.
bool WorthBuying(double rent_per_second, double seconds_left,
                 int64_t permanent_price) {
  return rent_per_second * seconds_left > kBuyMargin * permanent_price;
}

// Buys `info` outright if the mode allows it, the purse covers it, and -- in
// kAuto -- the rent left to pay is worth more than the price.
void BuyIfWorthIt(GameState& state, const PotPolicy& policy,
                  const ConsumableInfo& info, double rent_per_second,
                  PotSpend* spend) {
  if (policy.mode == PotMode::kRent || policy.mode == PotMode::kOff) {
    return;
  }
  if (policy.mode == PotMode::kAuto &&
      !WorthBuying(rent_per_second, policy.seconds_left,
                   info.permanent_price)) {
    return;
  }
  if (state.character.BuyConsumable(info.type)) {
    spend->bought += info.permanent_price;
  }
}

}  // namespace

double PotMesoPerSecond(absl::Span<const Mob* const> mobs,
                        absl::Span<const double> kills_per_second,
                        double meso_pct, double meso_mult, double drop_pct) {
  double total = 0.0;
  for (std::size_t i = 0; i < mobs.size() && i < kills_per_second.size(); ++i) {
    if (mobs[i] == nullptr || mobs[i]->boss()) {
      continue;
    }
    total += kills_per_second[i] * ExpectedMesoPerKill(*mobs[i], drop_pct);
  }
  return total * (1.0 + meso_pct) * meso_mult;
}

void PlanPots(GameState& state, const PotPolicy& policy,
              absl::Span<const Mob* const> mobs,
              absl::Span<const double> kills_per_second, PotSpend* spend) {
  CharacterInstance& character = state.character;
  int level = character.proto().level();
  for (const ConsumableInfo& info : AllConsumables()) {
    if (level < info.unlock_level) {
      continue;
    }
    if (policy.mode == PotMode::kOff) {
      SetPot(character, info.type, false);
      continue;
    }
    if (info.per_second) {
      // The map decides: a pot that drinks more than the crowd pays is one
      // the player puts away until they are somewhere worth drinking it.
      double gain = RateWithWealthPotion(state, true, mobs, kills_per_second) -
                    RateWithWealthPotion(state, false, mobs, kills_per_second);
      bool worth = gain > info.price;
      SetPot(character, info.type, worth);
      if (worth) {
        BuyIfWorthIt(state, policy, info, info.price, spend);
      }
      continue;
    }
    // A stage of attack speed against a million meso, in a fight whose clear
    // is worth many times that: it goes on whenever it is worth a stage at
    // all, and it is worth nothing to a character already at the ceiling.
    SetPot(character, info.type, RaisesTheStage(state));
    if (character.ConsumableActive(info.type)) {
      BuyIfWorthIt(state, policy, info,
                   info.price * policy.boss_entries_per_second, spend);
    }
  }
}

void DrinkPots(GameState& state, double seconds, PotSpend* spend) {
  if (seconds <= 0.0) {
    return;
  }
  bool drinking = false;
  for (const ConsumableInfo& info : AllConsumables()) {
    if (!info.per_second) {
      continue;
    }
    drinking = drinking || state.character.ConsumableInEffect(info.type);
    spend->drained += state.character.ChargeConsumable(info.type, seconds);
  }
  if (drinking) {
    spend->drinking_seconds += seconds;
  }
}

void EnterFightWithPots(GameState& state, PotSpend* spend) {
  ++spend->entries;
  for (const ConsumableInfo& info : AllConsumables()) {
    if (!info.per_second) {
      spend->drained += state.character.ChargeConsumable(info.type, 1);
    }
  }
}

}  // namespace ms
