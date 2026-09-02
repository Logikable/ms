#include "src/combat/boss_run.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/honor.h"
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
  return NextPlayerSpot(phase, from, dx, dy, {});
}

int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy,
                   const std::vector<int>& taken) {
  if (from < 0 || from >= phase.player_spots_size()) {
    return from;
  }
  const ArenaSpot& at = phase.player_spots(from);
  int best = from;
  int best_along = 0;
  int best_across = 0;
  bool tied = false;
  for (int i = 0; i < phase.player_spots_size(); ++i) {
    if (std::find(taken.begin(), taken.end(), i) != taken.end()) {
      continue;
    }
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

BossRun::BossRun(std::string boss_key, const Boss& boss, int difficulty_index,
                 FightAuthority* authority)
    : boss_key_(std::move(boss_key)),
      boss_(&boss),
      difficulty_index_(difficulty_index),
      authority_(authority) {
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
  if (phase == nullptr) {
    return;
  }
  // Walked here and told to the server afterwards, rather than asked for and
  // waited on: a step across the arena is worth nothing if it stutters.
  player_at_ = NextPlayerSpot(*phase, player_at_, dx, dy, TakenSpots());
  if (!members_.empty()) {
    members_[0].spot = player_at_;
  }
}

std::vector<int> BossRun::TakenSpots() const {
  std::vector<int> taken;
  for (std::size_t i = 1; i < members_.size(); ++i) {
    if (members_[i].spot >= 0) {
      taken.push_back(members_[i].spot);
    }
  }
  return taken;
}

void BossRun::StandSelf() {
  if (members_.empty()) {
    members_.resize(1);
  }
  // Always the first of them, so a stack this player landed is the one with
  // owner 0.
  members_[0] = {"", player_at_, sim_.attack_name(), sim_.attack_fraction()};
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
                              old.owner == stack.owner &&
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
  landed_.clear();
  for (std::size_t i = 0; i < lines.size();) {
    DamageStack stack;
    stack.mob_id = lines[i].mob_id;
    stack.source = lines[i].source;
    stack.preference = side(rng_);
    int event = lines[i].event;
    std::map<int, int>::const_iterator slot = slot_of_mob_.find(stack.mob_id);
    for (; i < lines.size() && lines[i].event == event; ++i) {
      // Rounded up off zero: a line that landed at all is worth a 1 rather
      // than a number that says nothing happened.
      int64_t damage = static_cast<int64_t>(std::llround(lines[i].damage));
      damage = std::max<int64_t>(1, damage);
      stack.lines.push_back({damage, lines[i].crit});
      if (authority_ == nullptr || slot == slot_of_mob_.end()) {
        continue;
      }
      // The same number, so what the shared roster loses is what its players
      // watched come off it.
      landed_.push_back(
          {0, slot->second, event, stack.source, damage, lines[i].crit});
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
  // Where each type's monsters begin in the phase's roster. A slot is that
  // number, and it is the same on every client -- the queue the monsters come
  // off is shuffled per client and is not.
  std::vector<int> first(params.types.size(), 0);
  int counted = 0;
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    first[i] = counted;
    counted += params.types[i].simultaneous;
  }
  slot_of_mob_.clear();
  mob_of_slot_.assign(counted, 0);
  for (const MobStatus& mob : sim_.roster()) {
    const std::vector<ArenaSpot>& spots = params.types[mob.type].spots;
    int taken = placed[mob.type]++;
    ArenaSpot spot;
    if (taken < static_cast<int>(spots.size())) {
      spot = spots[taken];
    }
    int slot = first[mob.type] + taken;
    if (slot < counted) {
      slot_of_mob_[mob.id] = slot;
      mob_of_slot_[slot] = mob.id;
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

const CombatParams& BossRun::PhaseParams(const GameState& state) {
  int level = state.character.proto().level();
  if (params_phase_ != phase_ || params_level_ != level) {
    params_ = ComputeBossParams(state, boss_key_, *difficulty(), phase_);
    params_phase_ = phase_;
    params_level_ = level;
  }
  return params_;
}

void BossRun::RunPhase(GameState& state, double dt) {
  const CombatParams& params = PhaseParams(state);
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
    PayReward(state, RollAwards(state, params.item_drop_pct));
    Finish(BossRunState::kWon);
    return;
  }
  state_ = BossRunState::kPhaseGap;
  hold_left_ = kBossPhaseGapSeconds;
}

std::vector<SharedAward> BossRun::RollAwards(GameState& state,
                                             double item_drop_pct) const {
  std::vector<SharedAward> awards;
  for (const MobDrop& drop : difficulty()->drops()) {
    // One roll for the fight, where a map rolls one per kill. Drop rate lifts
    // the chance the same way it lifts a monster's, up to certain.
    int64_t rolled =
        RollDrops(BossDropRate(drop.per_kill(), item_drop_pct), 1, state.rng);
    if (rolled > 0) {
      awards.push_back({drop, rolled});
    }
  }
  return awards;
}

void BossRun::PayReward(GameState& state,
                        const std::vector<SharedAward>& awards) {
  const BossDifficulty* chosen = difficulty();
  // A party splits the purse and nothing else. The EXP is what the fight is
  // worth to a character, and three people beating a boss have each beaten it.
  double share = 1.0 / std::max(1, share_count_);
  reward_.meso = static_cast<int64_t>(chosen->meso() * share);
  if (reward_.meso > 0) {
    state.character.AddMeso(reward_.meso);
  }
  // The honor, like the EXP, is not divided: what a party splits is the
  // purse, and everyone who beat the boss beat him. Held to the fights the
  // reset gates, so a boss with no lockout cannot be run for it all day.
  if (chosen->reset() != RESET_PERIOD_UNSPECIFIED) {
    reward_.honor = kBossClearHonor;
    state.character.AddHonor(reward_.honor);
  }
  reward_.exp = chosen->exp();
  if (reward_.exp > 0) {
    int before = state.character.proto().level();
    state.character.AddExp(reward_.exp);
    GrantLevelRewards(state, before, state.character.proto().level());
  }
  for (const SharedAward& award : awards) {
    int64_t granted = GrantDrop(state, award.drop, award.count);
    std::string name = DropName(state, award.drop);
    if (granted > 0 && !name.empty()) {
      reward_.items.push_back(
          {std::move(name), granted, award.drop.has_equip()});
    }
  }
}

void BossRun::AdvanceShared(GameState& state, double dt) {
  AgeDamageStacks(dt);
  SharedFight shared;
  if (!authority_->Fetch(shared)) {
    // Nothing has arrived. A run with an authority decides nothing itself, so
    // it waits rather than counting itself in.
    return;
  }
  bool paid = state_ == BossRunState::kWon;
  TakeShared(shared);
  if (state_ == BossRunState::kFighting) {
    RunSharedPhase(state, dt, shared);
  } else {
    SyncSlots(dt);
  }
  AddSharedStacks(shared.lines);
  StandSelf();
  if (state_ == BossRunState::kWon && !paid) {
    // The authority rolled these and said which of them are this player's.
    PayReward(state, shared.awards);
  }
  switch (state_) {
    case BossRunState::kWon:
    case BossRunState::kTimedOut:
    case BossRunState::kAborted:
      hold_left_ = std::max(0.0, hold_left_ - dt);
      return;
    default:
      return;
  }
}

void BossRun::TakeShared(const SharedFight& shared) {
  if (shared.phase != phase_) {
    phase_ = shared.phase;
    slots_.clear();
    // A monster id means nothing outside the encounter that handed it out, and
    // the arena is a different one anyway.
    damage_stacks_.clear();
    // Where everyone stands in a new phase is the server's to say.
    player_at_ = -1;
  }
  if (state_ != shared.state) {
    state_ = shared.state;
    if (done() || state_ == BossRunState::kWon ||
        state_ == BossRunState::kTimedOut) {
      hold_left_ = kBossEndHoldSeconds;
    }
  }
  seconds_left_ = shared.seconds_left;
  countdown_left_ = shared.countdown_left;
  if (shared.share_count > 0) {
    share_count_ = shared.share_count;
  }
  // This player first, so a stack landed by them is the one with owner 0.
  members_.assign(1, FightMember());
  member_of_player_.assign(shared.players.size(), 0);
  int stood = player_at_;
  for (std::size_t i = 0; i < shared.players.size(); ++i) {
    const SharedPlayer& player = shared.players[i];
    if (static_cast<int>(i) == shared.self) {
      stood = player.spot;
      continue;
    }
    if (!player.present) {
      // Their client has gone. The arena loses their panel and their spot is
      // somewhere to walk to again; they are still on the reward split. What
      // they landed before they went is left at member 0, which is where a
      // stack is dropped rather than drawn.
      continue;
    }
    member_of_player_[i] = static_cast<int>(members_.size());
    members_.push_back(
        {player.name, player.spot, player.attack_name, player.attack_fraction});
  }
  // Where this player stands is theirs to say: they walked there without
  // waiting to be told. The server's answer is taken for a phase they have not
  // stood in yet, and when somebody else turns out to be standing where they
  // think they are.
  const std::vector<int> taken = TakenSpots();
  if (player_at_ < 0 ||
      std::find(taken.begin(), taken.end(), player_at_) != taken.end()) {
    player_at_ = stood;
  }
}

void BossRun::RunSharedPhase(GameState& state, double dt,
                             const SharedFight& shared) {
  // Built every step rather than held the way RunPhase holds it: a party's
  // own membership is one of the things the table is built from, and it moves
  // inside a phase.
  CombatParams params =
      ComputeBossParams(state, boss_key_, *difficulty(), phase_);
  if (!params.active) {
    // Nothing to swing with, or a phase the catalogs do not hold. They can
    // still watch the party fight it.
    SyncSlots(dt);
    return;
  }
  item_drop_pct_ = params.item_drop_pct;
  AdvanceCombat(state, sim_, params, dt);
  if (slots_.empty()) {
    FillSlots(params);
  }
  CollectDamageStacks();
  authority_->Report({phase_, landed_, player_at_, sim_.attack_name(),
                      sim_.attack_fraction(), item_drop_pct_});
  // The shared roster is what everybody is hitting, so it decides what is
  // left. This copy of it may run ahead of the party's, never behind.
  std::map<int, double> said;
  for (std::size_t slot = 0;
       slot < shared.hp_fractions.size() && slot < mob_of_slot_.size();
       ++slot) {
    said[mob_of_slot_[slot]] = shared.hp_fractions[slot];
  }
  sim_.ClampRoster(params, said);
  SyncSlots(dt);
  ComputePhaseHp(params);
}

void BossRun::AddSharedStacks(const std::vector<SharedLine>& lines) {
  std::uniform_int_distribution<int> side(0, 3);
  for (std::size_t i = 0; i < lines.size();) {
    const SharedLine& first = lines[i];
    DamageStack stack;
    stack.owner =
        first.owner >= 0 &&
                first.owner < static_cast<int>(member_of_player_.size())
            ? member_of_player_[first.owner]
            : 0;
    stack.source = first.source;
    stack.preference = side(rng_);
    bool placed =
        first.slot >= 0 && first.slot < static_cast<int>(mob_of_slot_.size());
    stack.mob_id = placed ? mob_of_slot_[first.slot] : 0;
    for (; i < lines.size() && lines[i].event == first.event &&
           lines[i].owner == first.owner && lines[i].slot == first.slot;
         ++i) {
      stack.lines.push_back({lines[i].damage, lines[i].crit});
    }
    // A monster this client has already buried has nowhere left to hold them.
    if (placed && stack.owner > 0) {
      Replace(std::move(stack));
    }
  }
}

void BossRun::Advance(GameState& state, double elapsed_seconds) {
  if (done() || difficulty() == nullptr) {
    return;
  }
  double dt = std::max(0.0, elapsed_seconds);
  if (authority_ != nullptr) {
    AdvanceShared(state, dt);
    return;
  }
  RunAlone(state, dt);
  StandSelf();
}

void BossRun::RunAlone(GameState& state, double dt) {
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
