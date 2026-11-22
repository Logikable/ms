#include "src/combat/boss/boss_run.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/build_config.h"
#include "src/character/account/guild.h"
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
#include "src/spawn.h"

namespace ms {
namespace {

// Time between reports to the server, in run seconds.
constexpr double kReportSeconds = kFightPublishInterval.count() / 1000.0;

// A time later than any fight lasts, for events a slot never has.
constexpr double kNeverMoves = std::numeric_limits<double>::infinity();

// A pseudo-random number from a monster ID and a step count. The same inputs
// give the same result on every client, so nothing needs to be sent.
uint32_t Mixed(int id, int step) {
  uint32_t h = static_cast<uint32_t>(id) * 2654435761u +
               static_cast<uint32_t>(step) * 2246822519u;
  h ^= h >> 15;
  h *= 2654435761u;
  h ^= h >> 13;
  return h;
}

bool PlayerMayStand(const BossPhase& phase, int x, int y) {
  for (const ArenaSpot& spot : AllPlayerSpots(phase)) {
    if (spot.x() == x && spot.y() == y) {
      return true;
    }
  }
  return false;
}

// The columns [left, right) and rows a monster may move in.
struct Bounds {
  int left = 0;
  int right = 0;
  int height = 0;
};

// Whether a monster may stand on (x, y): inside its bounds, and not on a
// player spot.
bool MayEnter(const BossPhase& phase, int x, int y, const Bounds& bounds) {
  if (x < bounds.left || x >= bounds.right || y < 0 || y >= bounds.height) {
    return false;
  }
  return !PlayerMayStand(phase, x, y);
}

// Every cell one step of `walk` could move a monster at (x, y) to. Never its
// current cell, so a step always moves it, and never a player spot, since each
// cell holds one bar.
std::vector<ArenaSpot> WalkTargets(const BossPhase& phase,
                                   const ArenaWalk& walk, int x, int y,
                                   const Bounds& bounds) {
  std::vector<ArenaSpot> targets;
  std::vector<ArenaSpot> tried;
  if (walk.range() == ArenaWalk::RANGE_STEP ||
      walk.range() == ArenaWalk::RANGE_ROW_STEP) {
    // Sideways directions first, so a row-only walk just takes the first two.
    const int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int sides = walk.range() == ArenaWalk::RANGE_ROW_STEP ? 2 : 4;
    for (int side = 0; side < sides; ++side) {
      ArenaSpot to;
      to.set_x(x + kSteps[side][0]);
      to.set_y(y + kSteps[side][1]);
      tried.push_back(to);
    }
  } else {
    for (int cell = bounds.left; cell < bounds.right; ++cell) {
      ArenaSpot to;
      to.set_x(cell);
      to.set_y(y);
      tried.push_back(to);
    }
  }
  for (const ArenaSpot& to : tried) {
    if ((to.x() == x && to.y() == y) ||
        !MayEnter(phase, to.x(), to.y(), bounds)) {
      continue;
    }
    targets.push_back(to);
  }
  return targets;
}

// The room `zone` is, or null if it isn't one.
const ArenaRoom* RoomOf(const BossPhase& phase, int zone) {
  if (zone < 1 || zone > phase.zones_size() ||
      phase.zones(zone - 1).room().width() <= 0) {
    return nullptr;
  }
  return &phase.zones(zone - 1).room();
}

// Where a monster in `zone` may move: its room, or the whole arena.
Bounds BoundsOf(const BossPhase& phase, int zone) {
  ArenaSpot size = ArenaSize(phase);
  const ArenaRoom* room = RoomOf(phase, zone);
  if (room == nullptr) {
    return {0, size.x(), size.y()};
  }
  return {room->x(), room->x() + room->width(), size.y()};
}

}  // namespace

BossRun::BossRun(std::string boss_key, const Boss& boss, int difficulty_index,
                 FightAuthority* authority, const BossOptions& options)
    : boss_key_(std::move(boss_key)),
      boss_(&boss),
      difficulty_index_(difficulty_index),
      authority_(authority),
      options_(options) {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr) {
    state_ = BossRunState::kAborted;
    return;
  }
  title_ = chosen->name() + " " + boss.name();
  boss_name_ = boss.name();
  phases_ = chosen->phases_size();
  seconds_left_ = chosen->time_limit_seconds();
  if (phases_ > 0) {
    clock_cut_ = CutToPhaseClock(chosen->phases(0), seconds_left_);
  }
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
  // Move locally first and tell the server later, rather than waiting for it,
  // so movement never stutters.
  std::vector<int> taken = TakenSpots();
  std::vector<int> closed = ClosedSpots(*phase, FightSeconds());
  taken.insert(taken.end(), closed.begin(), closed.end());
  // A room's walls hold: the other rooms' spots aren't a step away.
  const ArenaRoom* room = SelfRoom();
  if (room != nullptr) {
    std::vector<ArenaSpot> spots = AllPlayerSpots(*phase);
    for (int i = 0; i < static_cast<int>(spots.size()); ++i) {
      if (spots[i].x() < room->x() ||
          spots[i].x() >= room->x() + room->width()) {
        taken.push_back(i);
      }
    }
  }
  MoveSelf(NextPlayerSpot(*phase, player_at_, dx, dy, taken));
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
  // Always first, so this player's stacks have owner 0.
  members_[0] = {"", player_at_, sim_.view().attack_name,
                 sim_.view().attack_fraction, sim_.view().buff_count};
}

const PhaseForm* BossRun::current_form() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr || phase->forms().empty()) {
    return nullptr;
  }
  int cycle = 0;
  for (const PhaseForm& form : phase->forms()) {
    cycle += std::max(0, form.seconds());
  }
  if (cycle <= 0) {
    return &phase->forms(0);
  }
  double into =
      std::fmod(std::max(0.0, elapsed_seconds_ - phase_began_), cycle);
  for (const PhaseForm& form : phase->forms()) {
    into -= std::max(0, form.seconds());
    if (into < 0.0) {
      return &form;
    }
  }
  return &phase->forms(phase->forms_size() - 1);
}

std::string_view BossRun::bgm() const {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr) {
    return {};
  }
  const PhaseForm* form = current_form();
  if (form != nullptr && !form->bgm().empty()) {
    return form->bgm();
  }
  const ArenaRoom* room = SelfRoom();
  if (room != nullptr && !room->bgm().empty()) {
    return room->bgm();
  }
  // Walk back from the current phase to the nearest one that sets a track. That
  // lets one track cover a whole fight.
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
  std::vector<ArenaSpot> spots = AllPlayerSpots(*phase);
  if (player_at_ < 0 || player_at_ >= static_cast<int>(spots.size())) {
    return ArenaSpot();
  }
  return spots[player_at_];
}

std::vector<ArenaSpot> BossRun::player_spots() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return {};
  }
  return AllPlayerSpots(*phase);
}

bool BossRun::spot_open(int index) const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return false;
  }
  std::vector<int> closed = ClosedSpots(*phase, FightSeconds());
  return std::find(closed.begin(), closed.end(), index) == closed.end();
}

double BossRun::FightSeconds() const {
  const BossDifficulty* chosen = difficulty();
  if (chosen == nullptr) {
    return 0.0;
  }
  return std::max(0.0,
                  chosen->time_limit_seconds() - seconds_left_ - clock_cut_);
}

const ArenaRoom* BossRun::SelfRoom() const {
  const BossPhase* phase = current_phase();
  return phase == nullptr ? nullptr
                          : RoomOf(*phase, ZoneOfSpot(*phase, player_at_));
}

int BossRun::view_left() const {
  const ArenaRoom* room = SelfRoom();
  return room == nullptr ? 0 : room->x();
}

int BossRun::arena_width() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return 0;
  }
  const ArenaRoom* room = SelfRoom();
  return room == nullptr ? ArenaSize(*phase).x() : room->width();
}

int BossRun::arena_height() const {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return 0;
  }
  return ArenaSize(*phase).y();
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
    // A write whose delay hasn't passed yet still has its full life ahead.
    if (write.showing() < kDamageStackSeconds) {
      writes.push_back(std::move(write));
    }
  }
  damage_writes_ = std::move(writes);
}

void BossRun::Replace(DamageStack stack) {
  // Remove whatever this source last left here, however old: two stacks from
  // one source would look like one stack flickering.
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

// The hit showing now: each lasts kDamageStrikeSeconds, and the last one stays
// for the rest of the stack's life.
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
  // How long ago each row was written, so a row keeps the newest number that
  // reached it rather than whichever came last in the list.
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
  // Lines from one landing arrive together, and consecutive lines with the same
  // hit number make one write. No sorting needed; they stack up the screen in
  // landing order.
  for (std::size_t i = 0; i < lines.size();) {
    int event = lines[i].event;
    std::map<int, int>::const_iterator slot =
        slot_of_mob_.find(lines[i].mob_id);
    DamageSource source = lines[i].source;
    // Count hits here rather than reading them from the line: the delay depends
    // on how many hits of this landing came before.
    int strikes = 0;
    for (int strike = -1; i < lines.size() && lines[i].event == event; ++i) {
      if (lines[i].strike != strike) {
        strike = lines[i].strike;
        damage_writes_.push_back(
            {lines[i].mob_id, {}, strikes * kDamageStrikeSeconds, 0.0});
        ++strikes;
      }
      // Round up from zero: a line that landed shows at least 1.
      breakdown_.AddLine(sim_.damage_credit_name(lines[i].credit),
                         lines[i].damage, lines[i].cast);
      int64_t damage = static_cast<int64_t>(std::llround(lines[i].damage));
      damage = std::max<int64_t>(1, damage);
      damage_writes_.back().lines.push_back({damage, lines[i].crit});
      if (authority_ == nullptr || slot == slot_of_mob_.end()) {
        continue;
      }
      // Report the same rounded number, so the shared HP drops by what players
      // saw.
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
  // A type's spots are handed out in mob-list order. Mobs of one type are
  // identical, so the order doesn't matter. A type with no spots stands at the
  // origin.
  std::vector<int> placed(params.types.size(), 0);
  // Where each type's slots begin in the phase. Slot numbers are the same on
  // every client, even though each client's mob queue is shuffled.
  std::vector<int> first(params.types.size(), 0);
  int counted = 0;
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    first[i] = counted;
    counted += params.types[i].simultaneous;
  }
  slot_of_mob_.clear();
  mob_of_slot_.assign(counted, 0);
  slot_spawns_.assign(counted, -1);
  slot_types_.assign(counted, -1);
  shields_taken_.assign(counted, 0.0);
  shields_until_ = FightSeconds();
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
      slot_spawns_[slot] = params.types[mob.type].spawn;
      slot_types_[slot] = mob.type;
    }
    BossSlot bar;
    bar.id = mob.id;
    bar.name = mob.name;
    bar.x = spot.x();
    bar.y = spot.y();
    bar.walk = params.types[mob.type].walk;
    bar.giant = params.types[mob.type].giant;
    bar.hp_bars = params.types[mob.type].hp_bars;
    const BossPhase* phase = current_phase();
    int spawn = params.types[mob.type].spawn;
    if (phase != nullptr && spawn >= 0 && spawn < phase->spawns_size()) {
      bar.zone = phase->spawns(spawn).zone();
    }
    // All timers count from the start of the fight, not the phase, so a monster
    // that appears later picks up its walk where the fight's time already is.
    bar.next_move_at = bar.walk.interval_ms() / 1000.0;
    bar.next_special_at = bar.walk.specials().max_interval_ms() > 0
                              ? SpecialWait(bar, 0)
                              : bar.walk.dash().interval_ms() / 1000.0;
    bar.next_jump_at = bar.walk.jump().interval_ms() / 1000.0;
    bar.next_teleport_at = bar.walk.teleport().interval_ms() / 1000.0;
    bar.ground_y = bar.y;
    bar.hp_fraction = mob.hp_fraction;
    slots_.push_back(std::move(bar));
  }
}

void BossRun::StepSlot(const BossPhase& phase, BossSlot& slot) {
  if (slot.walk.range() == ArenaWalk::RANGE_ABOVE_PLAYER ||
      slot.walk.range() == ArenaWalk::RANGE_OVER_PLAYER) {
    std::optional<ArenaSpot> hunted = HuntedSpot(phase, slot);
    if (!hunted) {
      return;
    }
    const int y = slot.walk.range() == ArenaWalk::RANGE_ABOVE_PLAYER
                      ? hunted->y() - 1
                      : slot.y;
    if (MayEnter(phase, hunted->x(), y, BoundsOf(phase, slot.zone))) {
      slot.x = hunted->x();
      slot.y = y;
    }
    return;
  }
  std::vector<ArenaSpot> targets =
      WalkTargets(phase, slot.walk, slot.x, slot.y, BoundsOf(phase, slot.zone));
  if (targets.empty()) {
    return;
  }
  // Pick the target from the step count instead of rolling, so every client
  // moves it the same way.
  const ArenaSpot& to =
      targets[Mixed(slot.id, slot.steps_taken) % targets.size()];
  slot.x = to.x();
  slot.y = to.y();
}

bool BossRun::DashSlot(const BossPhase& phase, BossSlot& slot) {
  int to = slot.x + slot.dash_dx;
  if (!MayEnter(phase, to, slot.y, BoundsOf(phase, slot.zone))) {
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
    // Start a fresh interval from the landing: the step it missed while in the
    // air is skipped.
    slot.next_move_at = slot.land_at + slot.walk.interval_ms() / 1000.0;
    return;
  }
  slot.ground_y = slot.y;
  slot.y = jump.y();
  slot.airborne = true;
  slot.land_at = slot.next_jump_at + jump.hang_ms() / 1000.0;
  slot.next_jump_at += jump.interval_ms() / 1000.0;
}

double BossRun::NextTeleportAt(const BossSlot& slot) {
  if (slot.walk.teleport().interval_ms() <= 0) {
    return kNeverMoves;
  }
  return slot.next_teleport_at;
}

void BossRun::TeleportSlot(const BossPhase& phase, BossSlot& slot) {
  slot.next_teleport_at += slot.walk.teleport().interval_ms() / 1000.0;
  // Mid-dash, mid-fall or in the air, it skips this one.
  if (slot.dash_left > 0 || slot.falling || slot.airborne) {
    return;
  }
  ++slot.steps_taken;
  std::optional<ArenaSpot> hunted = HuntedSpot(phase, slot);
  if (hunted &&
      MayEnter(phase, hunted->x(), slot.y, BoundsOf(phase, slot.zone))) {
    slot.x = hunted->x();
  }
}

double BossRun::NextMoveAt(const BossSlot& slot) {
  bool specials = slot.walk.specials().max_interval_ms() > 0 ||
                  slot.walk.dash().interval_ms() > 0;
  if (slot.dash_left > 0 || slot.falling || !specials) {
    return slot.next_move_at;
  }
  return std::min(slot.next_move_at, slot.next_special_at);
}

double BossRun::SpecialWait(const BossSlot& slot, int done) {
  const ArenaSpecials& specials = slot.walk.specials();
  int spread =
      std::max(0, specials.max_interval_ms() - specials.min_interval_ms());
  // Negative steps, so these draws never repeat one a step has used.
  uint32_t drawn = Mixed(slot.id, -1 - 2 * done) % (spread + 1);
  return (specials.min_interval_ms() + static_cast<int>(drawn)) / 1000.0;
}

bool BossRun::StartSpecial(const BossPhase& phase, BossSlot& slot) {
  slot.next_move_at = slot.next_special_at;
  bool fall = false;
  if (slot.walk.specials().max_interval_ms() > 0) {
    ++slot.specials_done;
    slot.next_special_at += SpecialWait(slot, slot.specials_done);
    bool can_dash = slot.walk.has_dash();
    bool can_fall = slot.walk.specials().has_fall();
    fall = can_fall &&
           (!can_dash || Mixed(slot.id, -2 * slot.specials_done) % 2 == 0);
  } else {
    slot.next_special_at += slot.walk.dash().interval_ms() / 1000.0;
  }
  if (fall && StartFall(phase, slot)) {
    return true;
  }
  if (!slot.walk.has_dash()) {
    return false;
  }
  slot.dash_left = std::max(1, slot.walk.dash().cells());
  slot.dash_dx = Mixed(slot.id, slot.steps_taken) % 2 == 0 ? 1 : -1;
  // If already against that wall, turn around instead of standing still for
  // the whole dash.
  if (!MayEnter(phase, slot.x + slot.dash_dx, slot.y,
                BoundsOf(phase, slot.zone))) {
    slot.dash_dx = -slot.dash_dx;
  }
  return false;
}

std::optional<ArenaSpot> BossRun::HuntedSpot(const BossPhase& phase,
                                             const BossSlot& slot) const {
  if (hunt_targets_.empty()) {
    return std::nullopt;
  }
  std::vector<ArenaSpot> spots = AllPlayerSpots(phase);
  // Only players it can see: in a room, those in the same room.
  const Bounds bounds = BoundsOf(phase, slot.zone);
  std::vector<int> seen;
  for (int target : hunt_targets_) {
    if (target >= 0 && target < static_cast<int>(spots.size()) &&
        spots[target].x() >= bounds.left && spots[target].x() < bounds.right) {
      seen.push_back(target);
    }
  }
  if (seen.empty()) {
    return std::nullopt;
  }
  return spots[seen[Mixed(slot.id, slot.steps_taken) % seen.size()]];
}

bool BossRun::StartFall(const BossPhase& phase, BossSlot& slot) {
  std::optional<ArenaSpot> hunted = HuntedSpot(phase, slot);
  if (!hunted || !MayEnter(phase, hunted->x(), 0, BoundsOf(phase, slot.zone))) {
    return false;
  }
  slot.ground_y = slot.y;
  slot.x = hunted->x();
  slot.y = 0;
  slot.falling = slot.y < slot.ground_y;
  return true;
}

void BossRun::FallSlot(const BossPhase& phase, BossSlot& slot) {
  if (MayEnter(phase, slot.x, slot.y + 1, BoundsOf(phase, slot.zone))) {
    ++slot.y;
  }
  slot.falling = slot.y < slot.ground_y && MayEnter(phase, slot.x, slot.y + 1,
                                                    BoundsOf(phase, slot.zone));
}

void BossRun::MoveSlot(const BossPhase& phase, BossSlot& slot) {
  ++slot.steps_taken;
  // A special that is due replaces the next step, and starts from when it was
  // due rather than when the step was.
  bool specials = slot.walk.specials().max_interval_ms() > 0 ||
                  slot.walk.dash().interval_ms() > 0;
  if (specials && slot.dash_left == 0 && !slot.falling &&
      slot.next_special_at <= slot.next_move_at && StartSpecial(phase, slot)) {
    // A fall's first move is its appearance on the top row.
    slot.next_move_at += (slot.falling ? slot.walk.specials().fall().step_ms()
                                       : slot.walk.interval_ms()) /
                         1000.0;
    return;
  }
  if (slot.falling) {
    FallSlot(phase, slot);
  } else if (slot.dash_left > 0) {
    --slot.dash_left;
    if (!DashSlot(phase, slot)) {
      slot.dash_left = 0;  // stopped at the wall; the dash ends
    }
  } else {
    StepSlot(phase, slot);
  }
  int wait = slot.falling         ? slot.walk.specials().fall().step_ms()
             : slot.dash_left > 0 ? slot.walk.dash().step_ms()
                                  : slot.walk.interval_ms();
  slot.next_move_at += wait / 1000.0;
}

void BossRun::DriftSlot(const BossPhase& phase, BossSlot& slot,
                        double elapsed) {
  while (true) {
    // Nothing walks while in the air, and a slot with no walk only jumps.
    double move = slot.airborne || slot.walk.interval_ms() <= 0
                      ? kNeverMoves
                      : NextMoveAt(slot);
    double jump = NextJumpAt(slot);
    double teleport = NextTeleportAt(slot);
    if (std::min({move, jump, teleport}) > elapsed) {
      return;
    }
    if (teleport < std::min(move, jump)) {
      TeleportSlot(phase, slot);
    } else if (jump <= move) {
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
  double elapsed = FightSeconds();
  for (BossSlot& slot : slots_) {
    if (slot.walk.interval_ms() <= 0 && slot.walk.jump().interval_ms() <= 0 &&
        slot.walk.teleport().interval_ms() <= 0) {
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
      slot.shield_fraction = it->shield_fraction;
      // A monster can come back: see TakeShields.
      slot.alive = true;
      slot.visible = true;
      slot.dead_for = 0.0;
      continue;
    }
    if (slot.alive) {
      // Just died: the empty bar stays up briefly before the slot goes dark.
      slot.alive = false;
      slot.hp_fraction = 0.0;
      slot.shield_fraction = 0.0;
      slot.dead_for = 0.0;
    }
    slot.dead_for += dt;
    slot.visible = slot.dead_for < kBossDeathHoldSeconds;
  }
}

std::vector<bool> BossRun::SlotsAlive() const {
  std::vector<bool> alive(mob_of_slot_.size(), false);
  for (const MobStatus& mob : sim_.view().roster) {
    std::map<int, int>::const_iterator slot = slot_of_mob_.find(mob.id);
    if (slot != slot_of_mob_.end()) {
      alive[slot->second] = true;
    }
  }
  return alive;
}

std::vector<int> BossRun::PartySpots() const {
  std::vector<int> spots = {player_at_};
  for (std::size_t i = 1; i < members_.size(); ++i) {
    spots.push_back(members_[i].spot);
  }
  return spots;
}

void BossRun::PlaceSelf() {
  const BossPhase* phase = current_phase();
  if (phase == nullptr || slots_.empty() ||
      phase->placement() == ZONE_PLACEMENT_UNSPECIFIED) {
    return;
  }
  std::vector<bool> zones = ZonesAlive(*phase, slot_spawns_, SlotsAlive());
  const bool fresh = zones_alive_.empty();
  if (!fresh && zones == zones_alive_) {
    return;
  }
  zones_alive_ = zones;
  const int current =
      fresh ? 0
            : ZoneOfSpot(*phase, walking_to_ >= 0 ? walking_to_ : player_at_);
  const int zone =
      PlacePlayers(phase->placement(), zones, {current}, rng_).front();
  if (zone == 0 || zone == current) {
    return;
  }
  const int spot = FreeSpotInZone(*phase, zone, TakenSpots());
  if (spot >= 0) {
    MoveSelf(spot);
  }
}

void BossRun::MoveSelf(int spot) {
  const BossPhase* phase = current_phase();
  const ArenaRoom* from = SelfRoom();
  const ArenaRoom* to =
      phase == nullptr ? nullptr : RoomOf(*phase, ZoneOfSpot(*phase, spot));
  if (from == nullptr || to == nullptr || from == to) {
    player_at_ = spot;
    walking_to_ = -1;
    return;
  }
  if (walking_to_ != spot) {
    walking_to_ = spot;
    walk_left_ = kBossPhaseGapSeconds;
  }
}

void BossRun::WalkBetweenRooms(double dt) {
  if (walking_to_ < 0) {
    return;
  }
  walk_left_ -= dt;
  if (walk_left_ <= 0.0) {
    player_at_ = walking_to_;
    walking_to_ = -1;
  }
}

void BossRun::TakeShields(const CombatParams& params,
                          const SharedFight& shared) {
  for (std::size_t slot = 0;
       slot < shared.shields.size() && slot < shields_taken_.size(); ++slot) {
    double owed = shared.shields[slot] - shields_taken_[slot];
    if (owed <= 0.0) {
      continue;
    }
    shields_taken_[slot] = shared.shields[slot];
    // Reaped here first means the shield beat the killing blow to the server.
    if (!sim_.AddShield(params, mob_of_slot_[slot], owed) &&
        slot < shared.hp_fractions.size() && shared.hp_fractions[slot] > 0.0) {
      sim_.Revive(params, mob_of_slot_[slot], slot_types_[slot],
                  shared.hp_fractions[slot]);
      if (slot < shared.shields_left.size()) {
        sim_.AddShield(params, mob_of_slot_[slot], shared.shields_left[slot]);
      }
    }
  }
}

void BossRun::ApplyPhaseRules(const CombatParams& params) {
  const BossPhase* phase = current_phase();
  if (phase == nullptr || slots_.empty() || !HasSlotRules(*phase)) {
    return;
  }
  const std::vector<bool> alive = SlotsAlive();
  const double now = FightSeconds();
  // A party's server gives the shields and says so; see RunSharedPhase.
  if (authority_ == nullptr) {
    std::vector<double> gained =
        ShieldsBetween(*phase, slot_spawns_, alive, shields_until_, now);
    for (std::size_t slot = 0; slot < gained.size(); ++slot) {
      if (gained[slot] > 0.0) {
        sim_.AddShield(params, mob_of_slot_[slot], gained[slot]);
      }
    }
  }
  shields_until_ = now;
  std::vector<SlotRule> rules =
      SlotRules(*phase, slot_spawns_, alive, now, player_at_, PartySpots());
  std::map<int, MobRule> by_id;
  for (std::size_t slot = 0; slot < rules.size(); ++slot) {
    by_id[mob_of_slot_[slot]] = {
        rules[slot].reachable, rules[slot].damage_taken, rules[slot].hp_floor};
  }
  sim_.SetMobRules(params, by_id);
}

void BossRun::ComputePhaseHp(const CombatParams& params) {
  double full = 0.0;
  for (const CombatType& type : params.types) {
    full += static_cast<double>(type.simultaneous) * type.mob->max_hp();
  }
  double left = 0.0;
  double shielded = 0.0;
  for (const MobStatus& mob : sim_.view().roster) {
    left += mob.hp_fraction * params.types[mob.type].mob->max_hp();
    shielded += mob.shield_fraction * params.types[mob.type].mob->max_hp();
  }
  phase_hp_fraction_ = full > 0.0 ? std::clamp(left / full, 0.0, 1.0) : 0.0;
  phase_shield_fraction_ = full > 0.0 ? shielded / full : 0.0;

  heading_hp_fraction_ = phase_hp_fraction_;
  heading_shield_fraction_ = phase_shield_fraction_;
  heading_dividers_.clear();
  for (std::size_t t = 0; t < params.types.size(); ++t) {
    const CombatType& type = params.types[t];
    if (type.hp_dividers.empty()) {
      continue;
    }
    heading_dividers_ = type.hp_dividers;
    double cut_left = 0.0;
    double cut_shield = 0.0;
    for (const MobStatus& mob : sim_.view().roster) {
      if (mob.type == static_cast<int>(t)) {
        cut_left += mob.hp_fraction;
        cut_shield += mob.shield_fraction;
      }
    }
    const double count = std::max(1, type.simultaneous);
    heading_hp_fraction_ = std::clamp(cut_left / count, 0.0, 1.0);
    heading_shield_fraction_ = cut_shield / count;
    break;
  }
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

void BossRun::ReleaseAscents(const GameState& state) {
  if (ascents_left_ <= 0 || current_phase() == nullptr) {
    return;
  }
  const BossDifficulty& fight = *difficulty();
  if (current_phase()->time_limit_seconds() > 0) {
    sim_.Release(ascents_left_, /*at_once=*/true);
    ascents_left_ = 0;
    return;
  }
  std::vector<double> hp;
  bool clock_ahead = false;
  for (int i = 0; i < fight.phases_size(); ++i) {
    const BossPhase& phase = fight.phases(i);
    clock_ahead = clock_ahead || (i > phase_ && phase.time_limit_seconds() > 0);
    double full = 0.0;
    for (const Spawn& spawn : phase.spawns()) {
      std::map<std::string, Mob>::const_iterator mob =
          state.mobs.find(spawn.mob());
      if (mob != state.mobs.end()) {
        full += SpawnCount(spawn) * mob->second.max_hp();
      }
    }
    hp.push_back(full);
  }
  if (clock_ahead) {
    return;
  }
  double total = 0.0;
  double left = 0.0;
  for (int i = 0; i < static_cast<int>(hp.size()); ++i) {
    total += hp[i];
    if (i == phase_) {
      left += hp[i] * phase_hp_fraction_;
    } else if (i > phase_) {
      left += hp[i];
    }
  }
  if (total <= 0.0) {
    return;
  }
  // The marks split the fight evenly among the uses: 75, 50 and 25% for three.
  const int uses = kAscentUsesPerBossFight;
  int passed = 0;
  for (int k = 1; k <= uses; ++k) {
    if (left / total < 1.0 - static_cast<double>(k) / (uses + 1)) {
      passed = k;
    }
  }
  while (uses - ascents_left_ < passed) {
    --ascents_left_;
    // The last use can't wait for a burst to lapse: the kill may come first.
    sim_.Release(1, /*at_once=*/ascents_left_ == 0);
  }
}

void BossRun::ResetCooldownsIfDue(const CombatParams& params) {
  if (cooldowns_due_) {
    sim_.ResetCooldowns(params);
    cooldowns_due_ = false;
  }
}

void BossRun::RunPhase(GameState& state, double dt) {
  const CombatParams& params = PhaseParams(state);
  if (!params.active) {
    // Nothing to fight: the phase names mobs the catalog doesn't have, or the
    // character has no weapon.
    Finish(BossRunState::kAborted);
    return;
  }
  ResetCooldownsIfDue(params);
  ApplyPhaseRules(params);
  AdvanceCombat(state, sim_, params, dt);
  breakdown_.AddSeconds(dt);
  CollectDamageWrites();
  const bool filling = slots_.empty();
  if (filling) {
    FillSlots(params);
  } else {
    SyncSlots(dt);
  }
  WalkBetweenRooms(dt);
  PlaceSelf();
  if (filling) {
    ApplyPhaseRules(params);
  }
  ComputePhaseHp(params);
  ReleaseAscents(state);
  seconds_left_ = std::max(0.0, seconds_left_ - dt);
  hunt_targets_.assign(1, player_at_);
  // After the timer update, since walking positions follow the timer.
  DriftSlots();
  player_at_ =
      DropFromClosedSpots(*current_phase(), FightSeconds(), {player_at_})[0];
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

void BossRun::ClaimClear(GameState& state, double seconds) {
  if (state_ == BossRunState::kWon || state_ == BossRunState::kTimedOut ||
      state_ == BossRunState::kAborted) {
    return;
  }
  const double item_drop_pct = PhaseParams(state).drop_roll_item_drop_pct;
  seconds_left_ = std::max(0.0, difficulty()->time_limit_seconds() - seconds);
  PayReward(state, RollAwards(state, item_drop_pct));
  Finish(BossRunState::kWon);
}

std::vector<SharedAward> BossRun::RollAwards(GameState& state,
                                             double item_drop_pct) const {
  std::vector<SharedAward> awards;
  for (const MobDrop& drop : difficulty()->drops()) {
    if (options_.void_drops() && drop.has_equip()) {
      continue;
    }
    // Bosses roll each drop once per fight; maps roll once per kill. How drop
    // rate applies depends on the item; see BossDropRate.
    int64_t rolled = RollDrops(BossDropRate(drop, item_drop_pct),
                               DropRolls(drop), state.rng);
    if (rolled > 0) {
      awards.push_back({drop, rolled});
    }
  }
  return awards;
}

void BossRun::PayReward(GameState& state,
                        const std::vector<SharedAward>& awards) {
  const BossDifficulty* chosen = difficulty();
  clear_seconds_ = FightSeconds();
  // A practice run pays nothing: no meso, EXP, honor or drops. The clear time
  // above is still recorded, since beating it is the point.
  if (options_.practice()) {
    return;
  }
  // A party splits the meso and nothing else. Every member beat the boss, so
  // each gets the full EXP.
  double share = 1.0 / std::max(1, share_count_);
  reward_.meso = static_cast<int64_t>(chosen->meso() * share);
  if (reward_.meso > 0) {
    state.character.AddMeso(reward_.meso);
  }
  // Honor isn't split either. Only bosses with a reset period pay it.
  if (chosen->reset() != RESET_PERIOD_UNSPECIFIED) {
    reward_.honor = kBossClearHonor;
    state.character.AddHonor(reward_.honor);
  }
  if (kSixthJobEnabled) {
    reward_.sol_erda_energy = chosen->sol_erda_energy();
    state.character.AddSolErdaEnergy(reward_.sol_erda_energy);
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
                               award.drop.per_kill() * DropRolls(award.drop)});
    }
  }
  if (authority_ == nullptr) {
    int before = NoblesseSpEarned(state.account.solo_clears(), state.bosses);
    state.account.RecordSoloClear(boss_key_, chosen->name());
    int after = NoblesseSpEarned(state.account.solo_clears(), state.bosses);
    reward_.noblesse_sp = after - before;
    state.character.set_noblesse_sp_earned(after);
  }
  if (state.character.RecordDefeat(boss_key_)) {
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      if (entry.second.unlock_boss() == boss_key_) {
        reward_.skills.push_back(entry.second.name());
      }
    }
  }
}

void BossRun::AdvanceShared(GameState& state, double dt) {
  AgeDamageNumbers(dt);
  SharedFight shared;
  if (!authority_->Fetch(shared)) {
    // Nothing has arrived yet. A party run decides nothing itself, so it waits
    // rather than starting its own countdown.
    return;
  }
  bool paid = state_ == BossRunState::kWon;
  TakeShared(shared);
  if (state_ == BossRunState::kFighting) {
    RunSharedPhase(state, dt, shared);
  } else if (state_ == BossRunState::kCountdown && slots_.empty()) {
    // Show the monsters during the countdown, as in a solo run: a zero-length
    // step places them and nobody attacks.
    RunSharedPhase(state, 0.0, shared);
  } else {
    SyncSlots(dt);
  }
  AddSharedStacks(shared.lines);
  StandSelf();
  if (state_ == BossRunState::kWon && !paid) {
    // The server rolled these and says which are this player's.
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
    phase_began_ = elapsed_seconds_;
    // The server cut the clock as the phase began; this player's last reading
    // is the nearest thing to what it cut from.
    if (current_phase() != nullptr) {
      clock_cut_ += CutToPhaseClock(*current_phase(), seconds_left_);
      cooldowns_due_ = current_phase()->reset_cooldowns();
    }
    slots_.clear();
    zones_alive_.clear();
    // Mob IDs are only valid within the encounter that assigned them, and
    // damage waiting to be reported refers to slots of the finished phase.
    damage_stacks_.clear();
    landed_.clear();
    report_due_ = 0.0;
    // The server decides where everyone starts in a new phase.
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
  if (shared.self >= 0 &&
      shared.self < static_cast<int>(shared.players.size())) {
    self_account_ = shared.players[shared.self].account_id;
  }
  if (!shared.breakdowns.empty()) {
    shared_breakdowns_ = shared.breakdowns;
  }
  if (shared.share_count > 0) {
    share_count_ = shared.share_count;
  }
  hunt_targets_.clear();
  for (const SharedPlayer& player : shared.players) {
    if (player.present && player.spot >= 0) {
      hunt_targets_.push_back(player.spot);
    }
  }
  // This player first, so their stacks have owner 0.
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
      // Their client disconnected: their panel disappears and their spot is
      // free again, but they still get a share of the reward.
      continue;
    }
    member_of_player_[i] = static_cast<int>(members_.size());
    members_.push_back({player.name, player.spot, player.attack_name,
                        player.attack_fraction, player.buff_count});
  }
  // This player's position is their own, since they move without waiting for
  // the server. Use the server's position only in a new phase, or if another
  // player turns out to be on the same spot.
  const std::vector<int> taken = TakenSpots();
  // A new placement moves this player too: the server sent them somewhere.
  if (player_at_ < 0 || shared.placements != placements_seen_ ||
      std::find(taken.begin(), taken.end(), player_at_) != taken.end()) {
    MoveSelf(stood);
  }
  placements_seen_ = shared.placements;
  DropShared(shared);
}

void BossRun::DropShared(const SharedFight& shared) {
  const BossPhase* phase = current_phase();
  if (phase == nullptr) {
    return;
  }
  // Everyone, in party order, as the server drops them: it may not have caught
  // up with a spot closing yet, and a player shown hanging in the air for a
  // frame would look wrong.
  std::vector<int> standing(shared.players.size(), -1);
  for (std::size_t i = 0; i < shared.players.size(); ++i) {
    if (static_cast<int>(i) == shared.self) {
      standing[i] = player_at_;
    } else if (shared.players[i].present) {
      standing[i] = shared.players[i].spot;
    }
  }
  standing = DropFromClosedSpots(*phase, FightSeconds(), standing);
  for (std::size_t i = 0; i < standing.size(); ++i) {
    if (static_cast<int>(i) == shared.self) {
      player_at_ = standing[i];
    } else if (member_of_player_[i] > 0) {
      members_[member_of_player_[i]].spot = standing[i];
    }
  }
}

void BossRun::RunSharedPhase(GameState& state, double dt,
                             const SharedFight& shared) {
  // Rebuild every step instead of caching like RunPhase: the table depends
  // partly on party membership, which can change mid-phase.
  CombatParams params =
      ComputeBossParams(state, boss_key_, *difficulty(), phase_);
  if (!params.active) {
    // No weapon, or a phase the catalogs don't have. The player can still watch
    // the party fight.
    SyncSlots(dt);
    return;
  }
  item_drop_pct_ = params.drop_roll_item_drop_pct;
  ResetCooldownsIfDue(params);
  ApplyPhaseRules(params);
  AdvanceCombat(state, sim_, params, dt);
  breakdown_.AddSeconds(dt);
  WalkBetweenRooms(dt);
  if (slots_.empty()) {
    FillSlots(params);
    ApplyPhaseRules(params);
  }
  CollectDamageWrites();
  ReportToParty(dt);
  // The server's HP values are what everyone is hitting, so they win. This
  // local copy may be ahead of the server's, but never behind.
  std::map<int, double> said;
  for (std::size_t slot = 0;
       slot < shared.hp_fractions.size() && slot < mob_of_slot_.size();
       ++slot) {
    said[mob_of_slot_[slot]] = shared.hp_fractions[slot];
  }
  std::map<int, double> shields;
  for (std::size_t slot = 0;
       slot < shared.shields_left.size() && slot < mob_of_slot_.size();
       ++slot) {
    shields[mob_of_slot_[slot]] = shared.shields_left[slot];
  }
  sim_.ClampRoster(params, said, shields);
  TakeShields(params, shared);
  SyncSlots(dt);
  DriftSlots();
  ComputePhaseHp(params);
  ReleaseAscents(state);
}

// The screen updates every kBossFightStep but the network every
// kFightPublishInterval, so each report includes every line since the last one.
// Reporting faster would gain nothing; the server only reads on its own timer.
void BossRun::ReportToParty(double dt) {
  report_due_ -= dt;
  if (report_due_ > 0.0) {
    return;
  }
  // Add the interval rather than resetting, so short steps don't make reports
  // drift later and later.
  report_due_ += kReportSeconds;
  authority_->Report({phase_, landed_, player_at_, sim_.view().attack_name,
                      sim_.view().attack_fraction, item_drop_pct_,
                      sim_.view().buff_count, breakdown_.Rows(),
                      placements_seen_});
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
    // Drop stacks for a monster this client has already removed.
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

std::vector<PlayerBreakdown> BossRun::breakdowns() const {
  std::vector<PlayerBreakdown> all = {{self_account_, "", breakdown_.Rows()}};
  for (const PlayerBreakdown& player : shared_breakdowns_) {
    if (player.account_id != self_account_) {
      all.push_back(player);
    }
  }
  return all;
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
  // Age numbers first, whatever state the run is in, so numbers from the attack
  // that ended a phase fade during the gap instead of freezing.
  AgeDamageNumbers(dt);
  if (state_ == BossRunState::kCountdown) {
    // Show the monsters before the countdown starts; seeing the boss is the
    // point of the countdown.
    if (slots_.empty()) {
      RunPhase(state, 0.0);
    }
    countdown_left_ -= dt;
    if (countdown_left_ > 0.0) {
      return;
    }
    // Carry the leftover time into the fight so a slow tick doesn't cost the
    // player time on their timer.
    dt = -countdown_left_;
    countdown_left_ = 0.0;
    phase_began_ = elapsed_seconds_ - dt;
    state_ = BossRunState::kFighting;
  }
  switch (state_) {
    case BossRunState::kFighting:
      RunPhase(state, dt);
      return;
    case BossRunState::kPhaseGap:
      // The timer keeps running between phases, and bars of monsters that just
      // died keep fading.
      seconds_left_ = std::max(0.0, seconds_left_ - dt);
      SyncSlots(dt);
      hold_left_ -= dt;
      if (hold_left_ > 0.0) {
        return;
      }
      ++phase_;
      // Less what the step ran past the hold, which the phase has already had.
      phase_began_ = elapsed_seconds_ + hold_left_;
      slots_.clear();
      zones_alive_.clear();
      walking_to_ = -1;
      // Mob IDs are only valid within the encounter that assigned them, and the
      // new phase is a different arena anyway.
      damage_stacks_.clear();
      StandPlayerAtStart();
      clock_cut_ += CutToPhaseClock(*current_phase(), seconds_left_);
      cooldowns_due_ = current_phase()->reset_cooldowns();
      state_ = BossRunState::kFighting;
      RunPhase(state, -hold_left_);
      return;
    default:
      hold_left_ = std::max(0.0, hold_left_ - dt);
      return;
  }
}

}  // namespace ms
