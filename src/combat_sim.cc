#include "src/combat_sim.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Pause between clearing one mob and engaging the next: 3 game ticks (900ms).
constexpr double kInterKillDelaySeconds = 0.9;

}  // namespace

CombatParams ComputeCombatParams(const GameState& state) {
  CombatParams params;
  std::map<std::string, MapData>::const_iterator map_it =
      state.maps.find(state.current_map);
  if (map_it == state.maps.end()) {
    return params;
  }
  const MapData& map = map_it->second;

  const std::map<EquipSlot, EquipInstance>& equipped =
      state.character.equipped();
  std::map<EquipSlot, EquipInstance>::const_iterator weapon_it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (weapon_it == equipped.end()) {
    return params;
  }
  const EquipPrototype& weapon = weapon_it->second.prototype();

  std::vector<const Mob*> mobs;
  for (const std::string& mob_name : map.mobs()) {
    std::map<std::string, Mob>::const_iterator mob_it =
        state.mobs.find(mob_name);
    if (mob_it != state.mobs.end()) {
      mobs.push_back(&mob_it->second);
    }
  }
  if (mobs.empty()) {
    return params;
  }

  const Character& proto = state.character.proto();
  OffenseStats offense = OffenseStatsFor(proto.job(), proto.allocated_stats(),
                                         state.character.equip_stats());
  params.swing_seconds =
      SwingIntervalSeconds(BaseAttackDelayMs(weapon.equip_type()),
                           weapon.attack_speed()) *
      kGameSpeedFactor;
  params.respawn_seconds = kRespawnIntervalSeconds * kGameSpeedFactor;
  int per_type = map.spawn_count() / static_cast<int>(mobs.size());
  for (const Mob* mob : mobs) {
    CombatType type;
    type.mob = mob;
    type.damage_per_hit = ExpectedAttackDamage(offense, *mob);
    type.simultaneous = per_type;
    params.types.push_back(std::move(type));
  }
  params.active = true;
  return params;
}

void CombatSim::Refill(const CombatParams& params) {
  roster_.clear();
  for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
    for (int k = 0; k < params.types[i].simultaneous; ++k) {
      roster_.push_back(i);
    }
  }
  attack_phase_ = 0.0;
  delay_remaining_ = 0.0;
  if (!roster_.empty()) {
    target_hp_ = params.types[roster_.front()].mob->max_hp();
  }
}

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  active_ = params.active;
  kills_this_step_.assign(params.types.size(), 0);
  if (!params.active || params.types.empty() || params.swing_seconds <= 0.0) {
    initialized_ = false;
    respawning_ = false;
    target_name_.clear();
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
    return;
  }

  double swing = params.swing_seconds;
  // Clamp large real-time gaps (e.g. after a pause) to one swing so the
  // animation resumes gracefully rather than jumping.
  double dt = std::min(elapsed_seconds, swing);

  // (Re)initialize on first activation or when the encounter shape changes.
  if (!initialized_ || type_count_ != static_cast<int>(params.types.size())) {
    type_count_ = static_cast<int>(params.types.size());
    respawn_phase_ = 0.0;
    Refill(params);
    initialized_ = true;
  }

  // The respawn beat refills the whole roster.
  respawn_phase_ += dt;
  if (respawn_phase_ >= params.respawn_seconds) {
    respawn_phase_ -= params.respawn_seconds;
    Refill(params);
  }

  if (delay_remaining_ > 0.0) {
    delay_remaining_ = std::max(0.0, delay_remaining_ - dt);
  } else if (!roster_.empty()) {
    attack_phase_ += dt;
    if (attack_phase_ >= swing) {
      attack_phase_ -= swing;
      target_hp_ -= params.types[roster_.front()].damage_per_hit;
      if (target_hp_ <= 0.0) {  // overkill wasted; the next mob is full HP
        ++kills_this_step_[roster_.front()];
        roster_.erase(roster_.begin());
        attack_phase_ = 0.0;
        if (!roster_.empty()) {
          delay_remaining_ = kInterKillDelaySeconds;
          target_hp_ = params.types[roster_.front()].mob->max_hp();
        }
      }
    }
  }

  respawning_ = roster_.empty();
  if (roster_.empty()) {
    target_name_.clear();
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
  } else {
    const Mob& target = *params.types[roster_.front()].mob;
    target_name_ = target.name();
    target_hp_fraction_ =
        target.max_hp() > 0 ? std::clamp(target_hp_ / target.max_hp(), 0.0, 1.0)
                            : 0.0;
    attack_fraction_ = std::clamp(attack_phase_ / swing, 0.0, 1.0);
  }
}

}  // namespace ms
