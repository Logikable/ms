#include "analysis/sim_boss.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "src/combat/boss_run.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

// The step the fight is walked in: the frame the game draws at, so a swing
// lands on the same tick it would land on in front of a player.
constexpr double kStepSeconds = 1.0 / 60.0;

// What a phase is holding, over every monster standing in it.
int64_t PhaseHp(const std::map<std::string, Mob>& mobs,
                const BossPhase& phase) {
  int64_t hp = 0;
  for (const Spawn& spawn : phase.spawns()) {
    std::map<std::string, Mob>::const_iterator it = mobs.find(spawn.mob());
    if (it != mobs.end()) {
      hp += static_cast<int64_t>(SpawnCount(spawn)) * it->second.max_hp();
    }
  }
  return hp;
}

}  // namespace

BossOutcome FightBoss(GameState& state, const std::string& boss_key,
                      int difficulty_index, double limit_seconds) {
  std::map<std::string, Boss>::iterator found = state.bosses.find(boss_key);
  if (found == state.bosses.end() ||
      difficulty_index >= found->second.difficulties_size()) {
    return BossOutcome();
  }
  BossDifficulty* difficulty =
      found->second.mutable_difficulties(difficulty_index);
  double clock = difficulty->time_limit_seconds();
  if (limit_seconds > 0.0) {
    difficulty->set_time_limit_seconds(static_cast<int>(limit_seconds));
    clock = limit_seconds;
  }
  BossRun run(boss_key, found->second, difficulty_index);
  while (!run.done()) {
    run.Advance(state, kStepSeconds);
  }
  BossOutcome outcome;
  outcome.won = run.won();
  if (outcome.won) {
    outcome.seconds = clock - run.seconds_left();
    return outcome;
  }
  int phases = std::max(1, run.phase_count());
  outcome.left = (phases - run.phase() + run.phase_hp_fraction()) / phases;
  return outcome;
}

int64_t BossTotalHp(const std::map<std::string, Mob>& mobs,
                    const BossDifficulty& difficulty) {
  int64_t hp = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    hp += PhaseHp(mobs, phase);
  }
  return hp;
}

int BossPdr(const std::map<std::string, Mob>& mobs,
            const BossDifficulty& difficulty) {
  int pdr = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    for (const Spawn& spawn : phase.spawns()) {
      std::map<std::string, Mob>::const_iterator it = mobs.find(spawn.mob());
      if (it != mobs.end()) {
        pdr = std::max(pdr, it->second.pdr());
      }
    }
  }
  return pdr;
}

std::set<std::string> BossOwnDrops(const BossDifficulty& difficulty) {
  std::set<std::string> keys;
  for (const MobDrop& drop : difficulty.drops()) {
    if (drop.has_equip()) {
      keys.insert(drop.equip());
    }
  }
  return keys;
}

int BossObjectivePhase(const std::map<std::string, Mob>& mobs,
                       const BossDifficulty& difficulty) {
  int best = 0;
  int64_t most = -1;
  for (int i = 0; i < difficulty.phases_size(); ++i) {
    int64_t hp = PhaseHp(mobs, difficulty.phases(i));
    if (hp > most) {
      most = hp;
      best = i;
    }
  }
  return best;
}

int BossDifficultyIndex(const Boss& boss, const std::string& name) {
  if (name.empty()) {
    return 0;
  }
  for (int i = 0; i < boss.difficulties_size(); ++i) {
    if (boss.difficulties(i).name() == name) {
      return i;
    }
  }
  return -1;
}

}  // namespace ms
