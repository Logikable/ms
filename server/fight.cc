#include "server/fight.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/boss_timing.h"
#include "src/combat/loot.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {

PartyFight::PartyFight(std::string id, std::string boss_key, const Boss& boss,
                       int difficulty_index,
                       const std::map<std::string, Mob>& mobs,
                       const Party& party, const BossOptions& options)
    : id_(std::move(id)),
      boss_key_(std::move(boss_key)),
      boss_(&boss),
      difficulty_index_(difficulty_index),
      mobs_(&mobs),
      options_(options) {
  for (const PartyMember& member : party.members()) {
    FightPlayer player;
    player.account_id = member.player().account_id();
    player.name = member.player().name();
    players_.push_back(std::move(player));
  }
  share_count_ = static_cast<int>(players_.size());
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr) {
    Finish(PartyFightState::kAbandoned);
    return;
  }
  phases_ = chosen->phases_size();
  seconds_left_ = chosen->time_limit_seconds();
  countdown_left_ = kBossCountdownSeconds;
  EnterPhase(0);
  if (hp_.empty()) {
    // The phase names no mob the catalog has, so there is nothing to fight.
    Finish(PartyFightState::kAbandoned);
  }
}

const BossDifficulty* PartyFight::difficulty() const {
  if (boss_ == nullptr || difficulty_index_ < 0 ||
      difficulty_index_ >= boss_->difficulties_size()) {
    return nullptr;
  }
  return &boss_->difficulties(difficulty_index_);
}

const BossPhase* PartyFight::current_phase() const {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr || phase_ < 0 || phase_ >= chosen->phases_size()) {
    return nullptr;
  }
  return &chosen->phases(phase_);
}

FightPlayer* PartyFight::Find(const std::string& account_id) {
  for (FightPlayer& player : players_) {
    if (player.account_id == account_id) {
      return &player;
    }
  }
  return nullptr;
}

void PartyFight::EnterPhase(int phase) {
  phase_ = phase;
  hp_.clear();
  max_hp_.clear();
  const BossPhase* current = current_phase();
  if (current == nullptr) {
    return;
  }
  // Build the mob list the same way clients do, so slot numbers mean the same
  // mob on both ends.
  for (const Spawn& spawn : current->spawns()) {
    std::map<std::string, Mob>::const_iterator it = mobs_->find(spawn.mob());
    if (it == mobs_->end()) {
      continue;
    }
    for (int i = 0; i < SpawnCount(spawn); ++i) {
      max_hp_.push_back(it->second.max_hp());
      hp_.push_back(it->second.max_hp());
    }
  }
  hp_fractions_.assign(hp_.size(), 1.0);
  // Give each player their own spot, in party order. This assumes each phase
  // has at least as many spots as the party has members.
  int spots = current->player_spots_size();
  for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
    players_[i].spot = spots > 0 ? std::min(i, spots - 1) : -1;
    players_[i].lines.clear();
  }
}

bool PartyFight::AnyoneAlive() const {
  for (double hp : hp_) {
    if (hp > 0.0) {
      return true;
    }
  }
  return false;
}

bool PartyFight::over() const {
  switch (state_) {
    case PartyFightState::kWon:
    case PartyFightState::kTimedOut:
    case PartyFightState::kAbandoned:
      return true;
    default:
      return false;
  }
}

bool PartyFight::done() const {
  return over() && hold_left_ <= 0.0;
}

void PartyFight::Finish(PartyFightState outcome) {
  state_ = outcome;
  if (outcome == PartyFightState::kWon) {
    DealDrops();
  }
  // Skip the end pause for an abandoned fight. Nobody is left to see it, and
  // a drain should not wait on it.
  hold_left_ =
      outcome == PartyFightState::kAbandoned ? 0.0 : kBossEndHoldSeconds;
}

void PartyFight::DealDrops() {
  // A practice clear pays nothing. The server skips the roll itself rather
  // than relying on clients to discard what it deals.
  if (options_.practice()) {
    return;
  }
  const BossDifficulty* chosen = difficulty();
  std::vector<FightPlayer*> paid;
  double best_drop_pct = 0.0;
  for (FightPlayer& player : players_) {
    if (player.present) {
      paid.push_back(&player);
      best_drop_pct = std::max(best_drop_pct, player.item_drop_pct);
    }
  }
  if (chosen == nullptr || paid.empty()) {
    return;
  }
  std::uniform_int_distribution<size_t> who(0, paid.size() - 1);
  std::vector<int64_t> won(paid.size());
  for (const MobDrop& drop : chosen->drops()) {
    // One roll per fight, where a map rolls once per kill. BossDropRate
    // decides how drop rate affects each drop.
    int64_t rolled = RollDrops(BossDropRate(drop, best_drop_pct), 1, rng_);
    std::fill(won.begin(), won.end(), 0);
    for (int64_t i = 0; i < rolled; ++i) {
      // Pick a player for each copy, so two copies can go to two players.
      ++won[who(rng_)];
    }
    for (size_t i = 0; i < paid.size(); ++i) {
      if (won[i] == 0) {
        continue;
      }
      FightAward& award = paid[i]->awards.emplace_back();
      if (drop.has_equip()) {
        award.set_equip(drop.equip());
      } else {
        award.set_item(drop.item());
      }
      award.set_count(won[i]);
    }
  }
}

void PartyFight::Hit(const std::string& account_id, int slot, double damage) {
  if (state_ != PartyFightState::kFighting || damage <= 0.0) {
    return;
  }
  if (slot < 0 || slot >= static_cast<int>(hp_.size())) {
    return;
  }
  FightPlayer* player = Find(account_id);
  if (player == nullptr || !player->present) {
    return;
  }
  hp_[slot] = std::max(0.0, hp_[slot] - damage);
  hp_fractions_[slot] = max_hp_[slot] > 0.0 ? hp_[slot] / max_hp_[slot] : 0.0;
}

void PartyFight::Report(const std::string& account_id,
                        const FightUpdate& update) {
  FightPlayer* player = Find(account_id);
  if (player == nullptr || !player->present) {
    return;
  }
  player->attack_name = update.attack_name();
  player->attack_fraction = update.attack_fraction();
  player->buff_count = update.buff_count();
  player->item_drop_pct = update.item_drop_pct();
  // Each report carries the full table, so take it even from a report that
  // crossed a phase change.
  if (update.breakdown_size() > 0) {
    player->breakdown.assign(update.breakdown().begin(),
                             update.breakdown().end());
  }
  MoveTo(account_id, update.spot());
  if (update.phase() != phase_) {
    // This report's slots refer to the previous phase's mobs, so its damage
    // would hit the wrong ones.
    return;
  }
  for (const FightDamage& line : update.lines()) {
    Hit(account_id, line.slot(), static_cast<double>(line.damage()));
    player->lines.push_back(line);
  }
}

void PartyFight::TakeLines() {
  for (FightPlayer& player : players_) {
    player.lines.clear();
  }
}

bool PartyFight::MoveTo(const std::string& account_id, int spot) {
  const BossPhase* current = current_phase();
  if (over() || current == nullptr || spot < 0 ||
      spot >= current->player_spots_size()) {
    return false;
  }
  FightPlayer* moving = Find(account_id);
  if (moving == nullptr || !moving->present) {
    return false;
  }
  for (const FightPlayer& player : players_) {
    if (player.present && player.spot == spot &&
        player.account_id != account_id) {
      return false;
    }
  }
  moving->spot = spot;
  return true;
}

void PartyFight::Disconnect(const std::string& account_id) {
  FightPlayer* player = Find(account_id);
  if (player == nullptr) {
    return;
  }
  player->present = false;
  if (over()) {
    return;
  }
  for (const FightPlayer& other : players_) {
    if (other.present) {
      return;
    }
  }
  Finish(PartyFightState::kAbandoned);
}

void PartyFight::RunPhase(double dt) {
  seconds_left_ = std::max(0.0, seconds_left_ - dt);
  if (AnyoneAlive()) {
    if (seconds_left_ <= 0.0) {
      Finish(PartyFightState::kTimedOut);
    }
    return;
  }
  if (phase_ + 1 >= phases_) {
    Finish(PartyFightState::kWon);
    return;
  }
  state_ = PartyFightState::kPhaseGap;
  hold_left_ = kBossPhaseGapSeconds;
}

void PartyFight::Advance(double elapsed_seconds) {
  if (done()) {
    return;
  }
  double dt = std::max(0.0, elapsed_seconds);
  if (state_ == PartyFightState::kCountdown) {
    countdown_left_ -= dt;
    if (countdown_left_ > 0.0) {
      return;
    }
    // Carry the overshoot into the fight, so a slow step does not cost the
    // party time.
    dt = -countdown_left_;
    countdown_left_ = 0.0;
    state_ = PartyFightState::kFighting;
  }
  switch (state_) {
    case PartyFightState::kFighting:
      RunPhase(dt);
      return;
    case PartyFightState::kPhaseGap:
      // The timer keeps running between phases, as in solo play.
      seconds_left_ = std::max(0.0, seconds_left_ - dt);
      hold_left_ -= dt;
      if (hold_left_ > 0.0) {
        return;
      }
      EnterPhase(phase_ + 1);
      if (hp_.empty()) {
        Finish(PartyFightState::kAbandoned);
        return;
      }
      state_ = PartyFightState::kFighting;
      RunPhase(-hold_left_);
      return;
    default:
      hold_left_ = std::max(0.0, hold_left_ - dt);
      return;
  }
}

}  // namespace ms
