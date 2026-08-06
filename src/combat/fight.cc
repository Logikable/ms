#include "src/combat/fight.h"

#include <algorithm>
#include <cmath>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {

void CombatSim::TopUp(const CombatParams& params) {
  std::vector<int> standing(params.types.size(), 0);
  for (const QueuedMob& mob : queue_) {
    ++standing[mob.type];
  }
  int first_new = static_cast<int>(queue_.size());
  for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
    for (int k = standing[i]; k < params.types[i].simultaneous; ++k) {
      queue_.push_back({i, static_cast<double>(params.types[i].mob->max_hp())});
    }
  }
  // Interleave the newcomers so a swing does not face one whole type at a
  // time. Only they are shuffled: the mobs already in the queue are being
  // fought, and moving a wounded one out of the front window would hand back
  // the damage done to it.
  std::shuffle(queue_.begin() + first_new, queue_.end(), rng_);
}

const AttackOption* CombatSim::BestAttack(const CombatParams& params) const {
  if (queue_.empty()) {
    return nullptr;  // nothing to hit, so nothing to choose between
  }
  const AttackOption* best = nullptr;
  double best_total = -1.0;
  for (const AttackOption& attack : params.attacks) {
    int reach = std::max(1, attack.max_enemies);
    int hit = std::min(reach, static_cast<int>(queue_.size()));
    double total = 0.0;
    for (int j = 0; j < hit; ++j) {
      int type = queue_[j].type;
      if (type < static_cast<int>(attack.damage_per_hit.size())) {
        total += attack.damage_per_hit[type];
      }
    }
    if (total > best_total) {
      best_total = total;
      best = &attack;
    }
  }
  return best;
}

int CombatSim::player_hp() const {
  return static_cast<int>(std::ceil(player_hp_));
}

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  active_ = params.active;
  kills_this_step_.assign(params.types.size(), 0);
  died_this_step_ = false;
  if (!params.active || params.types.empty() || params.attacks.empty() ||
      params.swing_seconds <= 0.0) {
    initialized_ = false;
    respawning_ = false;
    target_name_.clear();
    target_level_ = 0;
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
    attack_name_.clear();
    reach_ = 1;
    player_hp_ = 0.0;
    player_hp_fraction_ = 0.0;
    player_max_hp_ = 0;
    hit_phase_ = 0.0;
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
    attack_phase_ = 0.0;
    hit_phase_ = 0.0;
    player_hp_ = params.max_player_hp;
    queue_.clear();
    TopUp(params);
    initialized_ = true;
  }
  // A level-up widens the pool and fills it, as GMS does -- player_max_hp_ is
  // still last step's, so this catches the moment it moves. Without the fill,
  // a character who levelled at full health would watch their bar drop to a
  // fraction it never lost anything to get to.
  if (params.max_player_hp != player_max_hp_) {
    player_hp_ = params.max_player_hp;
  }

  // The respawn beat brings the map back to a full roster.
  respawn_phase_ += dt;
  if (respawn_phase_ >= params.respawn_seconds) {
    respawn_phase_ -= params.respawn_seconds;
    // A beat that ends an idle stretch starts a fresh swing: the clock stopped
    // when the last mob died, so the new one is met from zero. A beat that
    // lands mid-fight is just more monsters arriving, and the swing already
    // being wound up keeps its charge -- restarting it there would throw away
    // real progress, not only the bar the player is watching.
    bool was_idle = queue_.empty();
    TopUp(params);
    // Every beat hands back a slice of the pool, cleared map or not. It is the
    // only healing there is, and it is what makes a map survivable by
    // outlasting it: hold out for a beat taking less than the slice and the
    // player never falls behind, however long the fight runs.
    player_hp_ =
        std::min(static_cast<double>(params.max_player_hp),
                 player_hp_ + params.beat_heal_fraction * params.max_player_hp);
    if (was_idle) {
      attack_phase_ = 0.0;
      // Emptying the map is the bigger breather, and it is worth the whole
      // pool: a player killing that fast has earned the map outright.
      player_hp_ = params.max_player_hp;
      hit_phase_ = 0.0;
    }
  }

  // The mob at the front of the queue is the one the player is in melee with,
  // and the only one that hits back, however many are on the map -- see
  // fight.h. It swings before the player does, so the last mob standing still
  // gets its hit in on the way out.
  //
  // An empty map has nothing to be hit by, and its clock waits where it is
  // rather than banking a free hit for whatever arrives next.
  if (queue_.empty() || params.hit_seconds <= 0.0) {
    hit_phase_ = 0.0;
  } else {
    hit_phase_ += dt;
    if (hit_phase_ >= params.hit_seconds) {
      hit_phase_ -= params.hit_seconds;
      player_hp_ = std::max(
          0.0, player_hp_ - params.types[queue_.front().type].damage_to_player);
      died_this_step_ = player_hp_ <= 0.0;
    }
  }

  // The attack is chosen against the queue as it stands, so the charge bar
  // names the swing that is actually coming and the pick can change as mobs
  // die out from under it. With the queue empty there is no swing coming and
  // the name goes blank.
  const AttackOption* attack = BestAttack(params);
  attack_name_ = attack != nullptr ? attack->name : "";
  if (attack != nullptr) {
    reach_ = std::max(1, attack->max_enemies);
  }

  if (attack != nullptr) {
    attack_phase_ += dt;
    if (attack_phase_ >= swing) {
      attack_phase_ -= swing;
      // One swing hits the front `reach_` mobs at once; each takes its own
      // type's damage. Overkill on any of them is wasted. Dead mobs leave the
      // queue and the ones behind slide into the window next swing.
      int hit = std::min(reach_, static_cast<int>(queue_.size()));
      for (int j = 0; j < hit; ++j) {
        queue_[j].hp -= attack->damage_per_hit[queue_[j].type];
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
      // The queue moved, so the next swing's pick may differ from this one's.
      const AttackOption* next = BestAttack(params);
      attack_name_ = next != nullptr ? next->name : "";
      if (next != nullptr) {
        reach_ = std::max(1, next->max_enemies);
      }
    }
  }

  player_max_hp_ = params.max_player_hp;
  player_hp_fraction_ =
      params.max_player_hp > 0
          ? std::clamp(player_hp_ / params.max_player_hp, 0.0, 1.0)
          : 0.0;

  respawning_ = queue_.empty();
  engaged_groups_.clear();
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

    // Merge the front window (the mobs the next swing hits) into one HP bar per
    // type: its member count and their average HP fraction, in queue order.
    int window = std::min(reach_, static_cast<int>(queue_.size()));
    for (int j = 0; j < window; ++j) {
      int type = queue_[j].type;
      const Mob& mob = *params.types[type].mob;
      double frac = mob.max_hp() > 0
                        ? std::clamp(queue_[j].hp / mob.max_hp(), 0.0, 1.0)
                        : 0.0;
      std::vector<EngagedGroup>::iterator it = std::find_if(
          engaged_groups_.begin(), engaged_groups_.end(),
          [&mob](const EngagedGroup& g) { return g.name == mob.name(); });
      if (it == engaged_groups_.end()) {
        engaged_groups_.push_back({mob.name(), mob.level(), 1, frac});
      } else {
        // Running average of the fractions seen so far for this type.
        it->hp_fraction =
            (it->hp_fraction * it->count + frac) / (it->count + 1);
        ++it->count;
      }
    }
  }
}

}  // namespace ms
