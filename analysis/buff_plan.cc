#include "analysis/buff_plan.h"

#include <algorithm>
#include <cstdint>

#include "absl/types/span.h"
#include "analysis/meso_rate.h"
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
// have gone on gear, and a buff that only breaks even by the last day of the
// run is not worth taking a star off the weapon for.
constexpr double kBuyMargin = 2.0;

// What switching the Wealth Acquisition Potion on adds to that. Switched back
// before returning: this is a question, not a move.
double WealthPotionGain(GameState& state, const BuffYield& yield) {
  CharacterInstance& character = state.character;
  bool was =
      character.ConsumableActive(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  character.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  double flipped = MesoPerSecondFor(state, yield.crowd);
  character.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  double standing = MesoPerSecondFor(state, yield.crowd);
  return was ? standing - flipped : flipped - standing;
}

// And what planting the totem adds: the kills it buys, valued at whatever the
// character's %meso and drop rate are worth. The EXP and the drops those kills
// also pay are gravy the decision does not need -- the rent clears on the meso
// alone or it does not clear at all.
double WildTotemGain(GameState& state, const BuffYield& yield) {
  if (yield.kills_with_totem.empty()) {
    return 0.0;
  }
  return MesoPerSecondFor(state, yield.crowd.At(yield.kills_with_totem)) -
         MesoPerSecondFor(state, yield.crowd.At(yield.kills_without_totem));
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
void SetBuff(CharacterInstance& character, ConsumableType type, bool on) {
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
void BuyIfWorthIt(GameState& state, const BuffPolicy& policy,
                  const ConsumableInfo& info, double rent_per_second,
                  BuffSpend* spend) {
  if (policy.mode == BuffMode::kRent || policy.mode == BuffMode::kOff) {
    return;
  }
  if (policy.mode == BuffMode::kAuto &&
      !WorthBuying(rent_per_second, policy.seconds_left,
                   info.permanent_price)) {
    return;
  }
  if (state.character.BuyConsumable(info.type)) {
    spend->bought += info.permanent_price;
  }
}

}  // namespace

// Whether `info` goes on, and what its rent is worth against. Each buff is a
// different question, so each answers its own.
bool WorthSwitchingOn(GameState& state, const BuffYield& yield,
                      const ConsumableInfo& info) {
  switch (info.type) {
    case CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION:
      // The map decides: a potion that drinks more than the crowd pays is one
      // the player puts away until they are somewhere worth drinking it.
      return WealthPotionGain(state, yield) > info.price;
    case CONSUMABLE_TYPE_WILD_TOTEM:
      return WildTotemGain(state, yield) > info.price;
    default:
      // A stage of attack speed against a million meso, in a fight whose clear
      // is worth many times that: it goes on whenever it is worth a stage at
      // all, and it is worth nothing to a character already at the ceiling.
      return RaisesTheStage(state);
  }
}

void PlanBuffs(GameState& state, const BuffPolicy& policy,
               const BuffYield& yield, BuffSpend* spend) {
  CharacterInstance& character = state.character;
  int level = character.proto().level();
  for (const ConsumableInfo& info : AllConsumables()) {
    if (level < info.unlock_level) {
      continue;
    }
    if (policy.mode == BuffMode::kOff) {
      SetBuff(character, info.type, false);
      continue;
    }
    SetBuff(character, info.type, WorthSwitchingOn(state, yield, info));
    if (!character.ConsumableActive(info.type)) {
      continue;
    }
    // What the rent comes to a second, which for a buff charged at a boss door
    // is its price over how often the player walks through one.
    double rent = info.per_second ? info.price
                                  : info.price * policy.boss_entries_per_second;
    BuyIfWorthIt(state, policy, info, rent, spend);
  }
}

void DrinkBuffs(GameState& state, double seconds, BuffSpend* spend) {
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

void EnterFightWithBuffs(GameState& state, BuffSpend* spend) {
  ++spend->entries;
  for (const ConsumableInfo& info : AllConsumables()) {
    if (!info.per_second) {
      spend->drained += state.character.ChargeConsumable(info.type, 1);
    }
  }
}

}  // namespace ms
