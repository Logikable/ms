#include "src/combat/fight.h"

#include <algorithm>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Pause between clearing one mob and engaging the next: 3 game ticks (900ms).
constexpr double kInterKillDelaySeconds = 0.9;

}  // namespace

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
