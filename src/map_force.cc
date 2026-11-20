#include "src/map_force.h"

#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/force/arcane_force.h"
#include "src/character/force/sacred_power.h"
#include "src/character/stat_preset.h"
#include "src/protos/boss.pb.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

MapForce SacredPowerFor(int required, const CharacterInstance& character) {
  MapForce force;
  force.name = "Sacred Power";
  force.abbreviation = "SAC";
  force.required = required;
  force.owned = character.sacred_power();
  force.factors = SacredFactorsFor(force.owned, force.required);
  return force;
}

MapForce ArcaneForceFor(int required, const CharacterInstance& character,
                        const std::map<std::string, Skill>& skills,
                        Activity activity) {
  MapForce force;
  force.name = "Arcane Force";
  force.abbreviation = "AF";
  force.required = required;
  force.owned = OwnedArcaneForce(character, skills, activity);
  force.factors = ArcaneFactorsFor(force.owned, force.required);
  return force;
}

}  // namespace

MapForce MapForceFor(const MapData& map, const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills) {
  MapForce force;
  if (map.arcane_force() > 0) {
    force = ArcaneForceFor(map.arcane_force(), character, skills,
                           Activity::kFarming);
  } else if (map.sacred_power() > 0) {
    force = SacredPowerFor(map.sacred_power(), character);
  }
  return force;
}

MapForce BossForceFor(const BossDifficulty& difficulty,
                      const CharacterInstance& character,
                      const std::map<std::string, Skill>& skills) {
  if (difficulty.arcane_force() > 0) {
    return ArcaneForceFor(difficulty.arcane_force(), character, skills,
                          Activity::kBossing);
  }
  if (difficulty.sacred_power() > 0) {
    return SacredPowerFor(difficulty.sacred_power(), character);
  }
  return {};
}

bool AsksForForce(const MapData& map) {
  return map.arcane_force() > 0 || map.sacred_power() > 0;
}

}  // namespace ms
