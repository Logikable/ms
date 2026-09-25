#include "src/map_force.h"

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/sacred_power.h"
#include "src/protos/map.pb.h"

namespace ms {

MapForce MapForceFor(const MapData& map, const CharacterInstance& character) {
  MapForce force;
  if (map.arcane_force() > 0) {
    force.name = "Arcane Force";
    force.abbreviation = "AF";
    force.required = map.arcane_force();
    force.owned = character.arcane_force();
    force.factors = ArcaneFactorsFor(force.owned, force.required);
  } else if (map.sacred_power() > 0) {
    force.name = "Sacred Power";
    force.abbreviation = "SAC";
    force.required = map.sacred_power();
    force.owned = character.sacred_power();
    force.factors = SacredFactorsFor(force.owned, force.required);
  }
  return force;
}

bool AsksForForce(const MapData& map) {
  return map.arcane_force() > 0 || map.sacred_power() > 0;
}

}  // namespace ms
