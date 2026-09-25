/* The force a map requires (Arcane Force in Arcane River, Sacred Power in
 * Grandis, nothing elsewhere) and how a character measures up to it.
 *
 * One answer for the fight and every screen that shows the penalty, so none of
 * them needs to know which force a map uses.
 */
#ifndef MS_SRC_MAP_FORCE_H_
#define MS_SRC_MAP_FORCE_H_

#include <string>

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/protos/map.pb.h"

namespace ms {

struct MapForce {
  // "Arcane Force" or "Sacred Power", and "AF" or "SAC" for narrow columns.
  // Both empty on a map requiring neither.
  std::string name;
  std::string abbreviation;
  int required = 0;
  int owned = 0;
  ForceFactors factors;
};

// What `map` requires of `character`. On a map requiring neither force,
// required is 0 and the factors are 1.
MapForce MapForceFor(const MapData& map, const CharacterInstance& character);

// Whether `map` requires either force: Arcane River and Grandis, which are also
// where V Points drop.
bool AsksForForce(const MapData& map);

}  // namespace ms

#endif  // MS_SRC_MAP_FORCE_H_
