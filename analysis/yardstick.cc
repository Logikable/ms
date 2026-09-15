#include "analysis/yardstick.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "analysis/sim_boss.h"
#include "src/character/character_stats.h"
#include "src/combat/encounter.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The monster a character with no fight to aim at is ranked against: one of
// their own level, with no defence and no boss flag. Nothing they buy is
// wasted against it -- the point is only that damage still orders the shelf.
Mob StandIn(int level) {
  Mob mob;
  mob.set_name("Yardstick");
  mob.set_level(level);
  return mob;
}

// The body of `difficulty`'s objective phase: the part the fight is decided
// against, and so the part a purchase should be judged on.
Mob ObjectiveBody(const GameState& state, const BossDifficulty& difficulty) {
  int phase = BossObjectivePhase(state.mobs, difficulty);
  const Mob* toughest = nullptr;
  for (const Spawn& spawn : difficulty.phases(phase).spawns()) {
    std::map<std::string, Mob>::const_iterator it =
        state.mobs.find(spawn.mob());
    if (it == state.mobs.end()) {
      continue;
    }
    if (toughest == nullptr || it->second.max_hp() > toughest->max_hp()) {
      toughest = &it->second;
    }
  }
  return toughest == nullptr ? StandIn(state.character.proto().level())
                             : *toughest;
}

// The attack the character would settle on: the one landing the most damage a
// second. Off the fight's own params rather than a measured run, because this
// is asked once a shopping pass and a run is asked of every candidate.
//
// The BOSS's params where there is a fight to aim at, not the map's. A boss
// fight reads the bossing preset and halves reach, and a character picks a
// different swing against one lone body than against a crowd -- ranking the
// gear bought for a boss on the swing they use while farming is the same
// mistake in a different place.
const Skill* SettledSwing(const GameState& state, const Yardstick& yard,
                          int* level) {
  std::pair<std::string, int> fight;
  CombatParams params;
  if (AimedFight(state, &fight) &&
      state.bosses.find(fight.first) != state.bosses.end()) {
    const BossDifficulty& difficulty =
        state.bosses.at(fight.first).difficulties(fight.second);
    params = ComputeBossParams(state, fight.first, difficulty,
                               BossObjectivePhase(state.mobs, difficulty));
  }
  if (!params.active) {
    params = ComputeCombatParams(state);
  }
  const AttackOption* best = nullptr;
  double best_rate = 0.0;
  for (const AttackOption& attack : params.attacks) {
    if (attack.swing_seconds <= 0.0) {
      continue;
    }
    double landed = 0.0;
    for (double hit : attack.damage_per_hit) {
      landed += hit;
    }
    double rate = landed / attack.swing_seconds;
    if (rate > best_rate) {
      best_rate = rate;
      best = &attack;
    }
  }
  if (best == nullptr) {
    return nullptr;
  }
  // Back to the catalog by the name the option carries: what the damage chain
  // wants is the skill's own data, which the option has already spent.
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.name() == best->name) {
      *level = state.character.skill_level(entry.second);
      return &entry.second;
    }
  }
  return nullptr;
}

}  // namespace

Yardstick YardstickFor(const GameState& state) {
  Yardstick yard;
  std::pair<std::string, int> fight;
  if (AimedFight(state, &fight)) {
    std::map<std::string, Boss>::const_iterator boss =
        state.bosses.find(fight.first);
    if (boss != state.bosses.end()) {
      yard.target =
          ObjectiveBody(state, boss->second.difficulties(fight.second));
    }
  }
  if (yard.target.name().empty()) {
    yard.target = StandIn(state.character.proto().level());
  }
  yard.swing = SettledSwing(state, yard, &yard.swing_level);
  return yard;
}

double Worth(const Yardstick& yard, const OffenseStats& offense) {
  return ExpectedAttackDamage(offense, yard.target);
}

double WorthOf(const GameState& state, const Yardstick& yard,
               const EquipStats& stats, const PassiveOffense& passives) {
  const Character& proto = state.character.proto();
  return Worth(
      yard, OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                            stats, state.character.weapon_type(), yard.swing,
                            yard.swing_level, passives));
}

}  // namespace ms
