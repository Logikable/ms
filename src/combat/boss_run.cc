#include "src/combat/boss_run.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/character/honor.h"
#include "src/combat/combat.h"
#include "src/combat/drop.h"
#include "src/combat/encounter.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// The beat between reports, in the seconds the run counts in.
constexpr double kReportSeconds = kFightPublishInterval.count() / 1000.0;

// A clock a slot does not keep, later than any fight is long.
constexpr double kNeverMoves = std::numeric_limits<double>::infinity();

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

// A number that looks drawn but is not: the same monster and the same step
// always give the same one, on every client, with nothing sent between them.
uint32_t Mixed(int id, int step) {
  uint32_t h = static_cast<uint32_t>(id) * 2654435761u +
               static_cast<uint32_t>(step) * 2246822519u;
  h ^= h >> 15;
  h *= 2654435761u;
  h ^= h >> 13;
  return h;
}

bool PlayerMayStand(const BossPhase& phase, int x, int y) {
  for (const ArenaSpot& spot : phase.player_spots()) {
    if (spot.x() == x && spot.y() == y) {
      return true;
    }
  }
  return false;
}

// Whether a monster may stand on (x, y) at all: inside the arena, and not on
// a cell the phase lets a player stand on.
bool MayEnter(const BossPhase& phase, int x, int y, int width, int height) {
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return false;
  }
  return !PlayerMayStand(phase, x, y);
}

// Everywhere one step of `walk` could carry a monster on (x, y). Never the
// cell it is on, so a step always moves it, and never a player's: the arena
// draws one bar per cell.
std::vector<ArenaSpot> WalkTargets(const BossPhase& phase,
                                   const ArenaWalk& walk, int x, int y,
                                   int width, int height) {
  std::vector<ArenaSpot> targets;
  std::vector<ArenaSpot> tried;
  if (walk.range() == ArenaWalk::RANGE_STEP ||
      walk.range() == ArenaWalk::RANGE_ROW_STEP) {
    // Sideways first, so a walk held to its own row takes the first two.
    const int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int sides = walk.range() == ArenaWalk::RANGE_ROW_STEP ? 2 : 4;
    for (int side = 0; side < sides; ++side) {
      ArenaSpot to;
      to.set_x(x + kSteps[side][0]);
      to.set_y(y + kSteps[side][1]);
      tried.push_back(to);
    }
  } else {
    for (int cell = 0; cell < width; ++cell) {
      ArenaSpot to;
      to.set_x(cell);
      to.set_y(y);
      tried.push_back(to);
    }
  }
  for (const ArenaSpot& to : tried) {
    if ((to.x() == x && to.y() == y) ||
        !MayEnter(phase, to.x(), to.y(), width, height)) {
      continue;
    }
    targets.push_back(to);
  }
  return targets;
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
    // Further across the arrow than along it is not that way at all, or
    // Right in Horntail's top corner would fetch the spot under his tail.
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
                 FightAuthority* authority, bool practice)
    : boss_key_(std::move(boss_key)),
      boss_(&boss),
      difficulty_index_(difficulty_index),
      authority_(authority),
      practice_(practice) {
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
  members_[0] = {"", player_at_, sim_.view().attack_name,
                 sim_.view().attack_fraction, sim_.view().buff_count};
}

std::string_view BossRun::bgm() const {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr) {
    return {};
  }
  // Walk back from the phase being fought: the first track named at or above
  // it is what is playing, which is how one track covers a whole fight.
  for (int i = std::min(phase_, chosen->phases_size() - 1); i >= 0; --i) {
    if (!chosen->phases(i).bgm().empty()) {
      return chosen->phases(i).bgm();
    }
  }
  return {};
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

void BossRun::AgeDamageNumbers(double dt) {
  std::vector<DamageStack> stacks;
  stacks.reserve(damage_stacks_.size());
  for (DamageStack& stack : damage_stacks_) {
    stack.age += dt;
    if (stack.age < kDamageStackSeconds) {
      stacks.push_back(std::move(stack));
    }
  }
  damage_stacks_ = std::move(stacks);
  std::vector<DamageWrite> writes;
  writes.reserve(damage_writes_.size());
  for (DamageWrite& write : damage_writes_) {
    write.age += dt;
    // A write that is not due yet has its whole life ahead of it.
    if (write.showing() < kDamageStackSeconds) {
      writes.push_back(std::move(write));
    }
  }
  damage_writes_ = std::move(writes);
}

void BossRun::Replace(DamageStack stack) {
  // What this source last left goes, whatever life it had: two lots from one
  // source read as one stack that cannot make up its mind.
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

// The strike showing now: one per kDamageStrikeSeconds, and the last of them
// stands for whatever is left of the stack's life.
std::pair<int, int> DamageStack::StrikeAt(double age) const {
  if (strike_starts.empty()) {
    return {0, static_cast<int>(lines.size())};
  }
  int last = static_cast<int>(strike_starts.size()) - 1;
  int at = std::clamp(static_cast<int>(age / kDamageStrikeSeconds), 0, last);
  int end = at == last ? static_cast<int>(lines.size()) : strike_starts[at + 1];
  return {strike_starts[at], end};
}

int DamageStack::TallestStrike() const {
  int tallest = 0;
  for (int i = 0; i < static_cast<int>(strike_starts.size()); ++i) {
    int end = i + 1 < static_cast<int>(strike_starts.size())
                  ? strike_starts[i + 1]
                  : static_cast<int>(lines.size());
    tallest = std::max(tallest, end - strike_starts[i]);
  }
  return strike_starts.empty() ? static_cast<int>(lines.size()) : tallest;
}

std::vector<DamageRow> DamageColumn(const std::vector<DamageWrite>& writes,
                                    int mob_id) {
  std::vector<DamageRow> rows;
  // How long ago each row was written, so a row keeps the freshest number that
  // reached it rather than the last one the list happened to hold.
  std::vector<double> since;
  for (const DamageWrite& write : writes) {
    if (write.mob_id != mob_id || !write.live()) {
      continue;
    }
    if (write.lines.size() > rows.size()) {
      rows.resize(write.lines.size());
      since.resize(write.lines.size(), kDamageStackSeconds);
    }
    for (std::size_t row = 0; row < write.lines.size(); ++row) {
      if (rows[row].filled && write.showing() > since[row]) {
        continue;
      }
      rows[row] = {true, write.lines[row]};
      since[row] = write.showing();
    }
  }
  return rows;
}

void BossRun::CollectDamageWrites() {
  const std::vector<DamageLine>& lines = sim_.damage_lines_this_step();
  // The lines of one landing arrive together, and a run under one strike is
  // one write. Nothing sorts: they are read up the screen in landing order.
  for (std::size_t i = 0; i < lines.size();) {
    int event = lines[i].event;
    std::map<int, int>::const_iterator slot =
        slot_of_mob_.find(lines[i].mob_id);
    DamageSource source = lines[i].source;
    // Counted here rather than taken from the line: the delay is how many
    // strikes of this landing came before, which is what the flash is.
    int strikes = 0;
    for (int strike = -1; i < lines.size() && lines[i].event == event; ++i) {
      if (lines[i].strike != strike) {
        strike = lines[i].strike;
        damage_writes_.push_back(
            {lines[i].mob_id, {}, strikes * kDamageStrikeSeconds, 0.0});
        ++strikes;
      }
      // Rounded up off zero: a line that landed at all is worth a 1 rather
      // than a number that says nothing happened.
      int64_t damage = static_cast<int64_t>(std::llround(lines[i].damage));
      damage = std::max<int64_t>(1, damage);
      damage_writes_.back().lines.push_back({damage, lines[i].crit});
      if (authority_ == nullptr || slot == slot_of_mob_.end()) {
        continue;
      }
      // The same number, so what the shared roster loses is what its players
      // watched come off it.
      landed_.push_back({0, slot->second, event, lines[i].strike, source,
                         damage, lines[i].crit});
    }
  }
  if (static_cast<int>(damage_writes_.size()) > kMaxDamageWrites) {
    damage_writes_.erase(
        damage_writes_.begin(),
        damage_writes_.begin() +
            (static_cast<int>(damage_writes_.size()) - kMaxDamageWrites));
  }
}

void BossRun::FillSlots(const CombatParams& params) {
  // A type's spots are handed out in roster order. They are the same monster,
  // so which takes which is a question about identical bars. A type with no
  // spots stands at the origin.
  std::vector<int> placed(params.types.size(), 0);
  // Where each type begins in the phase's roster. A slot is that number and
  // is the same on every client; the queue it comes off is shuffled.
  std::vector<int> first(params.types.size(), 0);
  int counted = 0;
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    first[i] = counted;
    counted += params.types[i].simultaneous;
  }
  slot_of_mob_.clear();
  mob_of_slot_.assign(counted, 0);
  for (const MobStatus& mob : sim_.view().roster) {
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
    BossSlot bar;
    bar.id = mob.id;
    bar.name = mob.name;
    bar.x = spot.x();
    bar.y = spot.y();
    bar.walk = params.types[mob.type].walk;
    // Every clock runs from the start of the fight rather than the start of
    // the phase, so a body that comes out late walks the time already spent.
    bar.next_move_at = bar.walk.interval_ms() / 1000.0;
    bar.next_dash_at = bar.walk.dash().interval_ms() / 1000.0;
    bar.next_jump_at = bar.walk.jump().interval_ms() / 1000.0;
    bar.ground_y = bar.y;
    bar.hp_fraction = mob.hp_fraction;
    slots_.push_back(std::move(bar));
  }
}

void BossRun::StepSlot(const BossPhase& phase, BossSlot& slot) {
  std::vector<ArenaSpot> targets = WalkTargets(phase, slot.walk, slot.x, slot.y,
                                               arena_width(), arena_height());
  if (targets.empty()) {
    return;
  }
  // Drawn off the step it is, not rolled: every client walks it the same way.
  const ArenaSpot& to =
      targets[Mixed(slot.id, slot.steps_taken) % targets.size()];
  slot.x = to.x();
  slot.y = to.y();
}

bool BossRun::DashSlot(const BossPhase& phase, BossSlot& slot) {
  int to = slot.x + slot.dash_dx;
  if (!MayEnter(phase, to, slot.y, arena_width(), arena_height())) {
    return false;
  }
  slot.x = to;
  return true;
}

double BossRun::NextJumpAt(const BossSlot& slot) {
  if (slot.walk.jump().interval_ms() <= 0) {
    return kNeverMoves;
  }
  return slot.airborne ? slot.land_at : slot.next_jump_at;
}

void BossRun::JumpSlot(BossSlot& slot) {
  const ArenaJump& jump = slot.walk.jump();
  if (slot.airborne) {
    slot.y = slot.ground_y;
    slot.airborne = false;
    // A fresh interval from the landing: she does not owe the step she was
    // in the air for.
    slot.next_move_at = slot.land_at + slot.walk.interval_ms() / 1000.0;
    return;
  }
  slot.ground_y = slot.y;
  slot.y = jump.y();
  slot.airborne = true;
  slot.land_at = slot.next_jump_at + jump.hang_ms() / 1000.0;
  slot.next_jump_at += jump.interval_ms() / 1000.0;
}

double BossRun::NextMoveAt(const BossSlot& slot) {
  if (slot.dash_left > 0 || slot.walk.dash().interval_ms() <= 0) {
    return slot.next_move_at;
  }
  return std::min(slot.next_move_at, slot.next_dash_at);
}

void BossRun::MoveSlot(const BossPhase& phase, BossSlot& slot) {
  const ArenaDash& dash = slot.walk.dash();
  ++slot.steps_taken;
  // A dash falling due takes the step that was coming, and runs from the
  // moment it was due rather than from whenever the step was.
  if (slot.dash_left == 0 && dash.interval_ms() > 0 &&
      slot.next_dash_at <= slot.next_move_at) {
    slot.dash_left = std::max(1, dash.cells());
    slot.next_move_at = slot.next_dash_at;
    slot.next_dash_at += dash.interval_ms() / 1000.0;
    slot.dash_dx = Mixed(slot.id, slot.steps_taken) % 2 == 0 ? 1 : -1;
    // Already against that wall: a dash with nowhere to go is no dash at all,
    // so it turns round instead of standing there for its whole length.
    if (!MayEnter(phase, slot.x + slot.dash_dx, slot.y, arena_width(),
                  arena_height())) {
      slot.dash_dx = -slot.dash_dx;
    }
  }
  if (slot.dash_left > 0) {
    --slot.dash_left;
    if (!DashSlot(phase, slot)) {
      slot.dash_left = 0;  // Stopped at the wall, and stops there.
    }
  } else {
    StepSlot(phase, slot);
  }
  slot.next_move_at +=
      (slot.dash_left > 0 ? dash.step_ms() : slot.walk.interval_ms()) / 1000.0;
}

void BossRun::DriftSlot(const BossPhase& phase, BossSlot& slot,
                        double elapsed) {
  while (true) {
    // Nothing walks while it is in the air, and a slot with no walk at all is
    // here for its jump alone.
    double move = slot.airborne || slot.walk.interval_ms() <= 0
                      ? kNeverMoves
                      : NextMoveAt(slot);
    double jump = NextJumpAt(slot);
    if (std::min(move, jump) > elapsed) {
      return;
    }
    if (jump <= move) {
      JumpSlot(slot);
    } else {
      MoveSlot(phase, slot);
    }
  }
}

void BossRun::DriftSlots() {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return;
  }
  double limit = difficulty() == nullptr
                     ? 0.0
                     : static_cast<double>(difficulty()->time_limit_seconds());
  double elapsed = std::max(0.0, limit - seconds_left_);
  for (BossSlot& slot : slots_) {
    if (slot.walk.interval_ms() <= 0 && slot.walk.jump().interval_ms() <= 0) {
      continue;
    }
    DriftSlot(*phase, slot, elapsed);
  }
}

void BossRun::SyncSlots(double dt) {
  const std::vector<MobStatus>& roster = sim_.view().roster;
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
  for (const MobStatus& mob : sim_.view().roster) {
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
  CollectDamageWrites();
  if (slots_.empty()) {
    FillSlots(params);
  } else {
    SyncSlots(dt);
  }
  ComputePhaseHp(params);
  seconds_left_ = std::max(0.0, seconds_left_ - dt);
  // After the clock, which is what says where anything that walks stands.
  DriftSlots();
  if (!sim_.view().roster.empty()) {
    if (seconds_left_ <= 0.0) {
      Finish(BossRunState::kTimedOut);
    }
    return;
  }
  if (phase_ + 1 >= phases_) {
    PayReward(state, RollAwards(state, params.drop_roll_item_drop_pct));
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
    // One roll for the fight, where a map rolls one per kill. What drop rate
    // buys depends on what falls -- see BossDropRate.
    int64_t rolled = RollDrops(BossDropRate(drop, item_drop_pct), 1, state.rng);
    if (rolled > 0) {
      awards.push_back({drop, rolled});
    }
  }
  return awards;
}

void BossRun::PayReward(GameState& state,
                        const std::vector<SharedAward>& awards) {
  const BossDifficulty* chosen = difficulty();
  clear_seconds_ = std::max(0.0, chosen->time_limit_seconds() - seconds_left_);
  // A practice run is the fight and nothing else: no meso, no EXP, no honor
  // and no drops. The clock above still stands, being what the player came to
  // beat.
  if (practice_) {
    return;
  }
  // A party splits the purse and nothing else. The EXP is what the fight is
  // worth to a character, and three people beating a boss have each beaten it.
  double share = 1.0 / std::max(1, share_count_);
  reward_.meso = static_cast<int64_t>(chosen->meso() * share);
  if (reward_.meso > 0) {
    state.character.AddMeso(reward_.meso);
  }
  // Honor, like EXP, is not divided: a party splits the purse, and everyone
  // who beat the boss beat him. Held to the fights the reset gates.
  if (chosen->reset() != RESET_PERIOD_UNSPECIFIED) {
    reward_.honor = kBossClearHonor;
    state.character.AddHonor(reward_.honor);
  }
  reward_.exp = chosen->exp();
  if (reward_.exp > 0) {
    AwardExp(state, reward_.exp);
  }
  for (const SharedAward& award : awards) {
    int64_t granted = GrantDrop(state, award.drop, award.count);
    std::string name = DropName(state, award.drop);
    if (granted > 0 && !name.empty()) {
      reward_.items.push_back({std::move(name), granted,
                               DropIsPrize(state, award.drop),
                               award.drop.per_kill()});
    }
  }
}

void BossRun::AdvanceShared(GameState& state, double dt) {
  AgeDamageNumbers(dt);
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
    // An id means nothing outside the encounter that handed it out, and
    // lines waiting on a report name slots of a phase that is over.
    damage_stacks_.clear();
    landed_.clear();
    report_due_ = 0.0;
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
      // Their client has gone: the arena loses their panel and their spot is
      // walkable again, but they stay on the reward split.
      continue;
    }
    member_of_player_[i] = static_cast<int>(members_.size());
    members_.push_back({player.name, player.spot, player.attack_name,
                        player.attack_fraction, player.buff_count});
  }
  // Where this player stands is theirs to say -- they walked there without
  // waiting. The server's answer is taken for a phase they have not stood in,
  // and where somebody else turns out to be on their cell.
  const std::vector<int> taken = TakenSpots();
  if (player_at_ < 0 ||
      std::find(taken.begin(), taken.end(), player_at_) != taken.end()) {
    player_at_ = stood;
  }
}

void BossRun::RunSharedPhase(GameState& state, double dt,
                             const SharedFight& shared) {
  // Built every step rather than held as RunPhase holds it: the table is
  // built partly from the party's membership, which moves inside a phase.
  CombatParams params =
      ComputeBossParams(state, boss_key_, *difficulty(), phase_);
  if (!params.active) {
    // Nothing to swing with, or a phase the catalogs do not hold. They can
    // still watch the party fight it.
    SyncSlots(dt);
    return;
  }
  item_drop_pct_ = params.drop_roll_item_drop_pct;
  AdvanceCombat(state, sim_, params, dt);
  if (slots_.empty()) {
    FillSlots(params);
  }
  CollectDamageWrites();
  ReportToParty(dt);
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
  DriftSlots();
  ComputePhaseHp(params);
}

// The screen runs at kBossFightStep and the wire at kFightPublishInterval, so
// a report carries every line since the last rather than one step's. Told
// faster, the server would only sit on it until its own beat.
void BossRun::ReportToParty(double dt) {
  report_due_ -= dt;
  if (report_due_ > 0.0) {
    return;
  }
  // Added rather than reset, so a run of short steps keeps the beat rather
  // than drifting a step later every time.
  report_due_ += kReportSeconds;
  authority_->Report({phase_, landed_, player_at_, sim_.view().attack_name,
                      sim_.view().attack_fraction, item_drop_pct_,
                      sim_.view().buff_count});
  landed_.clear();
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
    int strike = -1;
    for (; i < lines.size() && lines[i].event == first.event &&
           lines[i].owner == first.owner && lines[i].slot == first.slot;
         ++i) {
      if (lines[i].strike != strike) {
        strike = lines[i].strike;
        stack.strike_starts.push_back(static_cast<int>(stack.lines.size()));
      }
      stack.lines.push_back({lines[i].damage, lines[i].crit});
    }
    // A monster this client has already buried has nowhere left to hold them.
    if (placed && stack.owner > 0) {
      Replace(std::move(stack));
    }
  }
  if (static_cast<int>(damage_stacks_.size()) > kMaxDamageStacks) {
    damage_stacks_.erase(
        damage_stacks_.begin(),
        damage_stacks_.begin() +
            (static_cast<int>(damage_stacks_.size()) - kMaxDamageStacks));
  }
}

void BossRun::Advance(GameState& state, double elapsed_seconds) {
  if (done() || difficulty() == nullptr) {
    return;
  }
  double dt = std::max(0.0, elapsed_seconds);
  elapsed_seconds_ += dt;
  if (authority_ != nullptr) {
    AdvanceShared(state, dt);
    return;
  }
  RunAlone(state, dt);
  StandSelf();
}

void BossRun::RunAlone(GameState& state, double dt) {
  // Ahead of everything and whatever the run is doing, so the numbers left by
  // a phase-ending swing fade over the gap rather than hanging.
  AgeDamageNumbers(dt);
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
