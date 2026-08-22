#include "src/combat/boss_run.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// What a drop is called, for the card that names what the fight paid. Empty
// for a drop neither catalog knows, which is a drop nothing was granted for.
std::string DropName(const GameState& state, const MobDrop& drop) {
  if (drop.has_equip()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    return it == state.equips.end() ? "" : it->second.name();
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(drop.item());
  return it == state.items.end() ? "" : it->second.name();
}

// How far out anyone stands on each axis, counted in cells: the size an arena
// that names none is measured to.
ArenaSpot ArenaExtent(const BossPhase& phase) {
  ArenaSpot extent;
  for (const Spawn& spawn : phase.spawns()) {
    for (const ArenaSpot& spot : spawn.spots()) {
      extent.set_x(std::max(extent.x(), spot.x() + 1));
      extent.set_y(std::max(extent.y(), spot.y() + 1));
    }
  }
  for (const ArenaSpot& spot : phase.player_spots()) {
    extent.set_x(std::max(extent.x(), spot.x() + 1));
    extent.set_y(std::max(extent.y(), spot.y() + 1));
  }
  return extent;
}

}  // namespace

int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy) {
  if (from < 0 || from >= phase.player_spots_size()) {
    return from;
  }
  const ArenaSpot& at = phase.player_spots(from);
  int best = from;
  int best_along = 0;
  int best_across = 0;
  bool tied = false;
  for (int i = 0; i < phase.player_spots_size(); ++i) {
    const ArenaSpot& spot = phase.player_spots(i);
    int step_x = spot.x() - at.x();
    int step_y = spot.y() - at.y();
    // How far the spot lies the way the arrow points, and how far off that
    // line. Only one of dx and dy is ever set, so each is one term.
    int along = step_x * dx + step_y * dy;
    int across = std::abs(step_x * dy) + std::abs(step_y * dx);
    // Further across the arrow than along it is not that way at all: without
    // this, Right in Horntail's top corner would fetch the spot under his
    // tail, which is nearer along the arrow than the far corner is.
    if (along <= 0 || across > along) {
      continue;
    }
    if (best == from || along < best_along ||
        (along == best_along && across < best_across)) {
      best = i;
      best_along = along;
      best_across = across;
      tied = false;
      continue;
    }
    tied = tied || (along == best_along && across == best_across);
  }
  return tied ? from : best;
}

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
  StandPlayerAtStart();
}

void BossRun::StandPlayerAtStart() {
  const BossPhase* phase = current_phase();
  player_at_ = phase != nullptr && phase->player_spots_size() > 0 ? 0 : -1;
}

void BossRun::MovePlayer(int dx, int dy) {
  if (done() || player_at_ < 0) {
    return;
  }
  const BossPhase* phase = current_phase();
  if (phase != nullptr) {
    player_at_ = NextPlayerSpot(*phase, player_at_, dx, dy);
  }
}

const BossDifficulty* BossRun::difficulty() const {
  if (boss_ == nullptr || difficulty_index_ < 0 ||
      difficulty_index_ >= boss_->difficulties_size()) {
    return nullptr;
  }
  return &boss_->difficulties(difficulty_index_);
}

const BossPhase* BossRun::current_phase() const {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr || phase_ < 0 || phase_ >= chosen->phases_size()) {
    return nullptr;
  }
  return &chosen->phases(phase_);
}

ArenaSpot BossRun::player_spot() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return ArenaSpot();
  }
  if (player_at_ < 0 || player_at_ >= phase->player_spots_size()) {
    return ArenaSpot();
  }
  return phase->player_spots(player_at_);
}

std::vector<ArenaSpot> BossRun::player_spots() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return {};
  }
  return std::vector<ArenaSpot>(phase->player_spots().begin(),
                                phase->player_spots().end());
}

int BossRun::arena_width() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return 0;
  }
  // Measured off what stands in it when the phase says nothing: as wide as the
  // cell furthest to the right and no wider, which leaves it no margin.
  return phase->arena_width() > 0 ? phase->arena_width()
                                  : ArenaExtent(*phase).x();
}

int BossRun::arena_height() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return 0;
  }
  return phase->arena_height() > 0 ? phase->arena_height()
                                   : ArenaExtent(*phase).y();
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
  hold_left_ = outcome == BossRunState::kAborted ? 0.0 : kBossEndHoldSeconds;
}

void BossRun::AgeDamageStacks(double dt) {
  std::vector<DamageStack> live;
  live.reserve(damage_stacks_.size());
  for (DamageStack& stack : damage_stacks_) {
    stack.age += dt;
    if (stack.age < kDamageStackSeconds) {
      live.push_back(std::move(stack));
    }
  }
  damage_stacks_ = std::move(live);
}

void BossRun::Replace(DamageStack stack) {
  // What this source last left on this monster goes, whatever life it had:
  // two lots of numbers from one source read as one stack that cannot make up
  // its mind.
  damage_stacks_.erase(
      std::remove_if(damage_stacks_.begin(), damage_stacks_.end(),
                     [&stack](const DamageStack& old) {
                       return old.mob_id == stack.mob_id &&
                              old.source == stack.source;
                     }),
      damage_stacks_.end());
  damage_stacks_.push_back(std::move(stack));
}

void BossRun::CollectDamageStacks() {
  const std::vector<DamageLine>& lines = sim_.damage_lines_this_step();
  // The lines of one landing arrive together, so a run of them under one event
  // is the stack. Nothing here sorts: the order they landed in is the order
  // they are read down the screen.
  std::uniform_int_distribution<int> side(0, 3);
  for (std::size_t i = 0; i < lines.size();) {
    DamageStack stack;
    stack.mob_id = lines[i].mob_id;
    stack.source = lines[i].source;
    stack.preference = side(rng_);
    int event = lines[i].event;
    for (; i < lines.size() && lines[i].event == event; ++i) {
      // Rounded up off zero: a line that landed at all is worth a 1 rather
      // than a number that says nothing happened.
      int64_t damage = static_cast<int64_t>(std::llround(lines[i].damage));
      stack.lines.push_back({std::max<int64_t>(1, damage), lines[i].crit});
    }
    Replace(std::move(stack));
  }
  if (static_cast<int>(damage_stacks_.size()) > kMaxDamageStacks) {
    damage_stacks_.erase(
        damage_stacks_.begin(),
        damage_stacks_.begin() +
            (static_cast<int>(damage_stacks_.size()) - kMaxDamageStacks));
  }
}

void BossRun::FillSlots(const CombatParams& params) {
  // A type's spots are handed out in the order its monsters come off the
  // roster. They are all the same monster, so which one takes which is only
  // ever a question about identical bars. A type with no spots stands at the
  // origin, which is a fight nobody drew an arena for.
  std::vector<int> placed(params.types.size(), 0);
  for (const MobStatus& mob : sim_.roster()) {
    const std::vector<ArenaSpot>& spots = params.types[mob.type].spots;
    int taken = placed[mob.type]++;
    ArenaSpot spot;
    if (taken < static_cast<int>(spots.size())) {
      spot = spots[taken];
    }
    slots_.push_back(
        {mob.id, mob.name, spot.x(), spot.y(), mob.hp_fraction, true, true});
  }
}

void BossRun::SyncSlots(double dt) {
  const std::vector<MobStatus>& roster = sim_.roster();
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
  CollectDamageStacks();
  if (slots_.empty()) {
    FillSlots(params);
  } else {
    SyncSlots(dt);
  }
  ComputePhaseHp(params);
  seconds_left_ = std::max(0.0, seconds_left_ - dt);
  if (!sim_.roster().empty()) {
    if (seconds_left_ <= 0.0) {
      Finish(BossRunState::kTimedOut);
    }
    return;
  }
  if (phase_ + 1 >= phases_) {
    PayReward(state, params.item_drop_pct);
    Finish(BossRunState::kWon);
    return;
  }
  state_ = BossRunState::kPhaseGap;
  hold_left_ = kBossPhaseGapSeconds;
}

void BossRun::PayReward(GameState& state, double item_drop_pct) {
  const BossDifficulty* chosen = difficulty();
  reward_.meso = chosen->meso();
  if (reward_.meso > 0) {
    state.character.AddMeso(reward_.meso);
  }
  reward_.exp = chosen->exp();
  if (reward_.exp > 0) {
    state.character.AddExp(reward_.exp);
  }
  for (const MobDrop& drop : chosen->drops()) {
    // One roll for the fight, where a map rolls one per kill. Drop rate lifts
    // the chance the same way it lifts a monster's.
    int64_t rolled =
        RollDrops(drop.per_kill() * (1.0 + item_drop_pct), 1, state.rng);
    if (rolled <= 0) {
      continue;
    }
    int64_t granted = GrantDrop(state, drop, rolled);
    std::string name = DropName(state, drop);
    if (granted > 0 && !name.empty()) {
      reward_.items.push_back({std::move(name), granted});
    }
  }
}

void BossRun::Advance(GameState& state, double elapsed_seconds) {
  if (done() || difficulty() == nullptr) {
    return;
  }
  double dt = std::max(0.0, elapsed_seconds);
  // Ahead of everything, and whatever the run is doing: the numbers left by
  // the swing that ended a phase should fade out over the gap rather than
  // hang there until the next phase lands one.
  AgeDamageStacks(dt);
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
      // A monster id means nothing outside the encounter that handed it out,
      // and the arena is a different one anyway.
      damage_stacks_.clear();
      StandPlayerAtStart();
      state_ = BossRunState::kFighting;
      RunPhase(state, -hold_left_);
      return;
    default:
      hold_left_ = std::max(0.0, hold_left_ - dt);
      return;
  }
}

}  // namespace ms
