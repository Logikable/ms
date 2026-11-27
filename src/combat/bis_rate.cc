#include "src/combat/bis_rate.h"

#include <algorithm>
#include <string>
#include <utility>

#include "src/bis_search.h"
#include "src/character/max_character.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

constexpr char kBisTarget[] = "__bis_target";
// Long enough for a two-minute cooldown to land and come back, the window the
// bench plans its own spending over.
constexpr double kLeastWindow = 180.0;

double Window(const GameState& state) {
  double cycle = 0.0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (state.character.HoldsSkillFrom(entry.second)) {
      cycle = std::max(cycle, entry.second.cooldown_seconds());
    }
  }
  return std::max(kLeastWindow, 2.0 * cycle);
}

}  // namespace

double BisFightRate(GameState& state) {
  const Mob* target =
      MaxBossTarget(state.bosses, state.mobs, state.character.proto().level());
  if (target == nullptr) {
    return BisSwingRate(state);
  }
  Mob dummy = *target;
  // A held skill stops when its target would have died.
  dummy.set_max_hp(kMeasuredMobHp);
  state.mobs[kBisTarget] = std::move(dummy);
  BossDifficulty fight;
  fight.set_name("BiS");
  Spawn* spawn = fight.add_phases()->add_spawns();
  spawn->set_mob(kBisTarget);
  spawn->set_count(1);
  const CombatParams params = ComputeBossParams(state, kBisTarget, fight, 0);
  const Sequence played = MeasureFight(params, Window(state));
  // Spent against the one target, the only body this fight has.
  const double ascents = MeasureAscents(params, Window(state), played.damage);
  state.mobs.erase(kBisTarget);
  return played.seconds > 0.0
             ? WithAscents(played.damage / played.seconds,
                           static_cast<double>(target->max_hp()), ascents)
             : 0.0;
}

}  // namespace ms
