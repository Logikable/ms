#include "src/combat/boss_run.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

namespace ms {

BossRun::BossRun(std::string boss_key, const Boss& boss, int difficulty_index)
    : boss_key_(std::move(boss_key)),
      boss_(&boss),
      difficulty_index_(difficulty_index) {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr) {
    state_ = BossRunState::kAborted;
    return;
  }
  title_ = chosen->name() + " " + boss.name();
  boss_name_ = boss.name();
  phases_ = chosen->phases_size();
  seconds_left_ = chosen->time_limit_seconds();
}

const BossDifficulty* BossRun::difficulty() const {
  if (boss_ == nullptr || difficulty_index_ < 0 ||
      difficulty_index_ >= boss_->difficulties_size()) {
    return nullptr;
  }
  return &boss_->difficulties(difficulty_index_);
}

bool BossRun::done() const {
  switch (state_) {
    case BossRunState::kWon:
    case BossRunState::kTimedOut:
    case BossRunState::kAborted:
      return hold_left_ <= 0.0;
    default:
      return false;
  }
}

void BossRun::Abort() {
  if (!done()) {
    Finish(BossRunState::kAborted);
  }
}

void BossRun::Finish(BossRunState outcome) {
  state_ = outcome;
  hold_left_ = kBossEndHoldSeconds;
}

void BossRun::SyncSlots(double dt) {
  const std::vector<MobStatus>& roster = sim_.roster();
  if (slots_.empty()) {
    for (const MobStatus& mob : roster) {
      slots_.push_back({mob.id, mob.name, mob.hp_fraction, true, true});
    }
    return;
  }
  for (BossSlot& slot : slots_) {
    std::vector<MobStatus>::const_iterator it =
        std::find_if(roster.begin(), roster.end(),
                     [&slot](const MobStatus& m) { return m.id == slot.id; });
    if (it != roster.end()) {
      slot.hp_fraction = it->hp_fraction;
      continue;
    }
    if (slot.alive) {
      // Just died: the empty bar stands for a beat before the slot goes dark.
      slot.alive = false;
      slot.hp_fraction = 0.0;
      slot.dead_for = 0.0;
    }
    slot.dead_for += dt;
    slot.visible = slot.dead_for < kBossDeathHoldSeconds;
  }
}

void BossRun::ComputePhaseHp(const CombatParams& params) {
  double full = 0.0;
  for (const CombatType& type : params.types) {
    full += static_cast<double>(type.simultaneous) * type.mob->max_hp();
  }
  double left = 0.0;
  for (const MobStatus& mob : sim_.roster()) {
    left += mob.hp_fraction * params.types[mob.type].mob->max_hp();
  }
  phase_hp_fraction_ = full > 0.0 ? std::clamp(left / full, 0.0, 1.0) : 0.0;
}

void BossRun::RunPhase(GameState& state, double dt) {
  CombatParams params =
      ComputeBossParams(state, boss_key_, *difficulty(), phase_);
  if (!params.active) {
    // Nothing to fight: a phase naming mobs the catalog does not hold, or a
    // character who is not holding a weapon.
    Finish(BossRunState::kAborted);
    return;
  }
  AdvanceCombat(state, sim_, params, dt);
  SyncSlots(dt);
  ComputePhaseHp(params);
  seconds_left_ = std::max(0.0, seconds_left_ - dt);
  if (!sim_.roster().empty()) {
    if (seconds_left_ <= 0.0) {
      Finish(BossRunState::kTimedOut);
    }
    return;
  }
  if (phase_ + 1 >= phases_) {
    Finish(BossRunState::kWon);
    return;
  }
  state_ = BossRunState::kPhaseGap;
  hold_left_ = kBossPhaseGapSeconds;
}

void BossRun::Advance(GameState& state, double elapsed_seconds) {
  if (done() || difficulty() == nullptr) {
    return;
  }
  double dt = std::max(0.0, elapsed_seconds);
  if (state_ == BossRunState::kCountdown) {
    // The monsters are on screen before the count-in starts: what the player
    // is about to fight is the whole point of being given three seconds.
    if (slots_.empty()) {
      RunPhase(state, 0.0);
    }
    countdown_left_ -= dt;
    if (countdown_left_ > 0.0) {
      return;
    }
    // The overshoot goes to the fight rather than being thrown away, so a slow
    // tick cannot cost the player time on their clock.
    dt = -countdown_left_;
    countdown_left_ = 0.0;
    state_ = BossRunState::kFighting;
  }
  switch (state_) {
    case BossRunState::kFighting:
      RunPhase(state, dt);
      return;
    case BossRunState::kPhaseGap:
      // The clock keeps running between phases, and the arms that just died
      // keep fading.
      seconds_left_ = std::max(0.0, seconds_left_ - dt);
      SyncSlots(dt);
      hold_left_ -= dt;
      if (hold_left_ > 0.0) {
        return;
      }
      ++phase_;
      slots_.clear();
      state_ = BossRunState::kFighting;
      RunPhase(state, -hold_left_);
      return;
    default:
      hold_left_ = std::max(0.0, hold_left_ - dt);
      return;
  }
}

}  // namespace ms
