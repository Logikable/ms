#include "src/map_force.h"

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/sacred_power.h"
#include "src/protos/boss.pb.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

MapForce ArcaneForceFor(int required, const CharacterInstance& character) {
  MapForce force;
  force.name = "Arcane Force";
  force.abbreviation = "AF";
  force.required = required;
  force.owned = character.arcane_force();
  force.factors = ArcaneFactorsFor(force.owned, force.required);
  return force;
}

}  // namespace

MapForce MapForceFor(const MapData& map, const CharacterInstance& character) {
  MapForce force;
  if (map.arcane_force() > 0) {
    force = ArcaneForceFor(map.arcane_force(), character);
  } else if (map.sacred_power() > 0) {
    force.name = "Sacred Power";
    force.abbreviation = "SAC";
    force.required = map.sacred_power();
    force.owned = character.sacred_power();
    force.factors = SacredFactorsFor(force.owned, force.required);
  }
  return force;
}

MapForce BossForceFor(const BossDifficulty& difficulty,
                      const CharacterInstance& character) {
  if (difficulty.arcane_force() > 0) {
    return ArcaneForceFor(difficulty.arcane_force(), character);
  }
  return {};
}

bool AsksForForce(const MapData& map) {
  return map.arcane_force() > 0 || map.sacred_power() > 0;
}

}  // namespace ms
