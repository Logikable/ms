#include "src/combat/fight.h"

#include <algorithm>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {

void CombatSim::Refill(const CombatParams& params) {
  queue_.clear();
  for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
    for (int k = 0; k < params.types[i].simultaneous; ++k) {
      queue_.push_back({i, static_cast<double>(params.types[i].mob->max_hp())});
    }
  }
  // Fight the queue in a random order, interleaving the types. Beyond feeling
  // less mechanical, this keeps kills fair when clears lag the respawn beat:
  // the beat refills the whole queue on a fixed timer, and the sim always
  // attacks the front, so a fixed order would let only the front types die.
  std::shuffle(queue_.begin(), queue_.end(), rng_);
  attack_phase_ = 0.0;
}

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  active_ = params.active;
  kills_this_step_.assign(params.types.size(), 0);
  if (!params.active || params.types.empty() || params.swing_seconds <= 0.0) {
    initialized_ = false;
    respawning_ = false;
    target_name_.clear();
    target_level_ = 0;
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
    return;
  }

  double swing = params.swing_seconds;
  // Clamp large real-time gaps (e.g. after a pause) to one swing so the
  // animation resumes gracefully rather than jumping.
  double dt = std::min(elapsed_seconds, swing);

  // (Re)initialize on first activation, or on a move to a different map: the
  // queue holds indices into that map's types, and its HP values are that map's
  // mobs'. Carried over, both would describe the wrong monsters.
  if (!initialized_ || map_ != params.map) {
    map_ = params.map;
    respawn_phase_ = 0.0;
    Refill(params);
    initialized_ = true;
  }

  // The respawn beat refills the whole queue.
  respawn_phase_ += dt;
  if (respawn_phase_ >= params.respawn_seconds) {
    respawn_phase_ -= params.respawn_seconds;
    Refill(params);
  }

  if (!queue_.empty()) {
    attack_phase_ += dt;
    if (attack_phase_ >= swing) {
      attack_phase_ -= swing;
      // One swing hits the front `attack_targets` mobs at once; each takes its
      // own type's damage. Overkill on any of them is wasted. Dead mobs leave
      // the queue and the ones behind slide into the window next swing.
      int reach = std::max(1, params.attack_targets);
      int hit = std::min(reach, static_cast<int>(queue_.size()));
      for (int j = 0; j < hit; ++j) {
        queue_[j].hp -= params.types[queue_[j].type].damage_per_hit;
      }
      std::vector<QueuedMob> survivors;
      survivors.reserve(queue_.size());
      for (int j = 0; j < static_cast<int>(queue_.size()); ++j) {
        if (j < hit && queue_[j].hp <= 0.0) {
          ++kills_this_step_[queue_[j].type];
        } else {
          survivors.push_back(queue_[j]);
        }
      }
      queue_ = std::move(survivors);
    }
  }

  respawning_ = queue_.empty();
  if (queue_.empty()) {
    target_name_.clear();
    target_level_ = 0;
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
  } else {
    const QueuedMob& front = queue_.front();
    const Mob& target = *params.types[front.type].mob;
    target_name_ = target.name();
    target_level_ = target.level();
    target_hp_fraction_ = target.max_hp() > 0
                              ? std::clamp(front.hp / target.max_hp(), 0.0, 1.0)
                              : 0.0;
    attack_fraction_ = std::clamp(attack_phase_ / swing, 0.0, 1.0);
  }
}

}  // namespace ms
