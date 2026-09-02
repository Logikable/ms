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
                       const Party& party)
    : id_(std::move(id)),
      boss_key_(std::move(boss_key)),
      boss_(&boss),
      difficulty_index_(difficulty_index),
      mobs_(&mobs) {
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
    // A phase naming monsters the catalog does not hold. There is nothing to
    // fight, so there is no fight.
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
  // The same roster the clients build, in the same order: every spawn the mob
  // catalog knows, one monster per spot. That order is what a slot number
  // means, so the two ends cannot disagree about which monster was hit.
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
  // One spot each, in the order the party is held. Every phase carries more
  // spots than a party has members, so nobody starts on top of anybody.
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
  // An abandoned fight is held for nothing: there is nobody left to show it
  // to, and a drain waiting one out would wait for nobody.
  hold_left_ =
      outcome == PartyFightState::kAbandoned ? 0.0 : kBossEndHoldSeconds;
}

void PartyFight::DealDrops() {
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
    // One roll for the fight, where a map rolls one per kill, and never more
    // than one of a line: drop rate lifts a chance, not a certainty.
    int64_t rolled =
        RollDrops(BossDropRate(drop.per_kill(), best_drop_pct), 1, rng_);
    std::fill(won.begin(), won.end(), 0);
    for (int64_t i = 0; i < rolled; ++i) {
      // Each of them drawn for separately, so a drop that fell twice can fall
      // to two different people.
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
  player->item_drop_pct = update.item_drop_pct();
  MoveTo(account_id, update.spot());
  if (update.phase() != phase_) {
    // A report that crossed a phase change names monsters that are gone. Its
    // numbers would land on whatever took their slots.
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
    // The overshoot goes to the fight rather than being thrown away, so a
    // slow pass cannot cost the party time on their clock.
    dt = -countdown_left_;
    countdown_left_ = 0.0;
    state_ = PartyFightState::kFighting;
  }
  switch (state_) {
    case PartyFightState::kFighting:
      RunPhase(dt);
      return;
    case PartyFightState::kPhaseGap:
      // The clock keeps running between phases, as it does for one player.
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
