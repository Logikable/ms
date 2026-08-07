#include "src/combat/fight.h"

#include <algorithm>
#include <cmath>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Whether there is a fight to advance at all.
bool CanFight(const CombatParams& params) {
  return params.active && !params.types.empty() && !params.attacks.empty() &&
         params.swing_seconds > 0.0;
}

}  // namespace

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

void CombatSim::Strike(const AttackOption& attack) {
  // One strike hits the front mobs at once; each takes its own type's damage.
  // Overkill on any of them is wasted. Dead mobs leave the queue and the ones
  // behind slide into the window next time.
  int hit = std::min(std::max(1, attack.max_enemies),
                     static_cast<int>(queue_.size()));
  for (int j = 0; j < hit; ++j) {
    queue_[j].hp -= attack.damage_per_hit[queue_[j].type];
  }
  // Final Attack follows the swing onto whatever the character is standing in
  // front of -- one mob, however many the swing itself reached.
  if (hit > 0 && !attack.final_attack_damage.empty()) {
    queue_.front().hp -= attack.final_attack_damage[queue_.front().type];
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

int CombatSim::player_hp() const {
  return static_cast<int>(std::ceil(player_hp_));
}

void CombatSim::GoIdle() {
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
  auto_phase_.clear();
}

void CombatSim::BeginMapIfChanged(const CombatParams& params) {
  // The queue holds indices into the map's types, and its HP values are that
  // map's mobs'. Carried to another map, both would describe the wrong
  // monsters.
  if (initialized_ && map_ == params.map) {
    return;
  }
  map_ = params.map;
  respawn_phase_ = 0.0;
  attack_phase_ = 0.0;
  hit_phase_ = 0.0;
  auto_phase_.assign(params.auto_attacks.size(), 0.0);
  player_hp_ = params.max_player_hp;
  queue_.clear();
  TopUp(params);
  initialized_ = true;
}

void CombatSim::RespawnBeat(const CombatParams& params, double dt) {
  respawn_phase_ += dt;
  if (respawn_phase_ < params.respawn_seconds) {
    return;
  }
  respawn_phase_ -= params.respawn_seconds;
  bool was_idle = queue_.empty();
  TopUp(params);
  // Every beat hands back a slice of the pool, cleared map or not. It is the
  // only healing there is: hold out for a beat taking less than the slice and
  // the fight can be won by outlasting it.
  player_hp_ =
      std::min(static_cast<double>(params.max_player_hp),
               player_hp_ + params.beat_heal_fraction * params.max_player_hp);
  if (!was_idle) {
    // Mobs arriving mid-fight leave a wound-up swing wound up: restarting it
    // would throw away real progress, not just the bar being watched.
    return;
  }
  // Clearing the map is the bigger breather, and worth the whole pool.
  attack_phase_ = 0.0;
  player_hp_ = params.max_player_hp;
  hit_phase_ = 0.0;
}

void CombatSim::TakeMobHit(const CombatParams& params, double dt) {
  // Only the mob at the front hits back, however many are on the map -- see
  // fight.h. It swings first, so the last one standing still lands its hit on
  // the way out. An empty map has nothing to be hit by, and its clock waits
  // rather than banking a free hit for whatever arrives next.
  if (queue_.empty() || params.hit_seconds <= 0.0) {
    hit_phase_ = 0.0;
    return;
  }
  hit_phase_ += dt;
  if (hit_phase_ < params.hit_seconds) {
    return;
  }
  hit_phase_ -= params.hit_seconds;
  double taken = params.types[queue_.front().type].damage_to_player;
  player_hp_ = std::max(0.0, player_hp_ - taken);
  died_this_step_ = player_hp_ <= 0.0;
  Reflect(params, taken);
}

void CombatSim::Reflect(const CombatParams& params, double damage_taken) {
  // Off the whole hit, not the sliver of it a dying player had left to lose:
  // what comes back is a share of what was thrown, not of what it emptied.
  if (params.damage_reflect_pct <= 0.0 || queue_.empty()) {
    return;
  }
  QueuedMob& front = queue_.front();
  front.hp -= params.damage_reflect_pct * damage_taken;
  if (front.hp > 0.0) {
    return;
  }
  ++kills_this_step_[front.type];
  queue_.erase(queue_.begin());
}

void CombatSim::RunAutoCasts(const CombatParams& params, double dt) {
  // Their clocks run only while there is something to hit: a summon has
  // nothing to do on an empty map, and waiting there earns it no free cast.
  auto_phase_.resize(params.auto_attacks.size(), 0.0);
  for (int i = 0; i < static_cast<int>(params.auto_attacks.size()); ++i) {
    const AttackOption& cast = params.auto_attacks[i];
    if (queue_.empty() || cast.interval_seconds <= 0.0) {
      continue;
    }
    auto_phase_[i] += dt;
    if (auto_phase_[i] >= cast.interval_seconds) {
      auto_phase_[i] -= cast.interval_seconds;
      Strike(cast);
    }
  }
}

const AttackOption* CombatSim::AimSwing(const CombatParams& params) {
  const AttackOption* attack = BestAttack(params);
  attack_name_ = attack != nullptr ? attack->name : "";
  if (attack != nullptr) {
    reach_ = std::max(1, attack->max_enemies);
  }
  return attack;
}

void CombatSim::RunSwing(const CombatParams& params, double dt) {
  // Aimed against the queue as it stands, so the charge bar names the swing
  // that is really coming and the pick can change as mobs die out from under
  // it. Aimed again after the strike, because the queue just moved.
  const AttackOption* attack = AimSwing(params);
  if (attack == nullptr) {
    return;
  }
  attack_phase_ += dt;
  if (attack_phase_ < params.swing_seconds) {
    return;
  }
  attack_phase_ -= params.swing_seconds;
  Strike(*attack);
  AimSwing(params);
}

void CombatSim::MergeEngagedWindow(const CombatParams& params) {
  // One HP bar per type in the front window (the mobs the next swing hits),
  // in queue order, each bar averaging its members' remaining HP.
  int window = std::min(reach_, static_cast<int>(queue_.size()));
  for (int j = 0; j < window; ++j) {
    const Mob& mob = *params.types[queue_[j].type].mob;
    double frac = mob.max_hp() > 0
                      ? std::clamp(queue_[j].hp / mob.max_hp(), 0.0, 1.0)
                      : 0.0;
    std::vector<EngagedGroup>::iterator it = std::find_if(
        engaged_groups_.begin(), engaged_groups_.end(),
        [&mob](const EngagedGroup& g) { return g.name == mob.name(); });
    if (it == engaged_groups_.end()) {
      engaged_groups_.push_back({mob.name(), mob.level(), 1, frac});
      continue;
    }
    it->hp_fraction = (it->hp_fraction * it->count + frac) / (it->count + 1);
    ++it->count;
  }
}

void CombatSim::PublishTarget(const CombatParams& params) {
  engaged_groups_.clear();
  respawning_ = queue_.empty();
  if (queue_.empty()) {
    target_name_.clear();
    target_level_ = 0;
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
    return;
  }
  const QueuedMob& front = queue_.front();
  const Mob& target = *params.types[front.type].mob;
  target_name_ = target.name();
  target_level_ = target.level();
  target_hp_fraction_ = target.max_hp() > 0
                            ? std::clamp(front.hp / target.max_hp(), 0.0, 1.0)
                            : 0.0;
  attack_fraction_ = std::clamp(attack_phase_ / params.swing_seconds, 0.0, 1.0);
  MergeEngagedWindow(params);
}

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  active_ = params.active;
  kills_this_step_.assign(params.types.size(), 0);
  died_this_step_ = false;
  if (!CanFight(params)) {
    GoIdle();
    return;
  }
  // Clamp a large real-time gap (a pause, say) to one swing, so the fight
  // resumes rather than jumping.
  double dt = std::min(elapsed_seconds, params.swing_seconds);

  BeginMapIfChanged(params);
  // A level-up widens the pool and fills it, as GMS does. player_max_hp_ is
  // still last step's, so this catches the moment it moves; without the fill a
  // character who levelled at full health would watch their bar drop.
  if (params.max_player_hp != player_max_hp_) {
    player_hp_ = params.max_player_hp;
  }

  RespawnBeat(params, dt);
  TakeMobHit(params, dt);
  RunAutoCasts(params, dt);
  RunSwing(params, dt);

  player_max_hp_ = params.max_player_hp;
  player_hp_fraction_ =
      params.max_player_hp > 0
          ? std::clamp(player_hp_ / params.max_player_hp, 0.0, 1.0)
          : 0.0;
  PublishTarget(params);
}

}  // namespace ms
