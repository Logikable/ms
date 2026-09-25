/* The force a map asks for -- Arcane Force in Arcane River, Sacred Power in
 * Grandis, nothing anywhere else -- and where a character stands against it.
 *
 * One answer for the fight and every screen that shows the toll, so none of
 * them has to know which of the two a map is keyed on.
 */
#ifndef MS_SRC_MAP_FORCE_H_
#define MS_SRC_MAP_FORCE_H_

#include <string>

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/protos/map.pb.h"

namespace ms {

struct MapForce {
  // "Arcane Force" or "Sacred Power", and "AF" or "SAC" where a column is
  // narrow. Both empty on a map asking neither.
  std::string name;
  std::string abbreviation;
  int required = 0;
  int owned = 0;
  ForceFactors factors;
};

// What `map` asks of `character`. required is 0, and the factors the
// identity, on a map asking for neither force.
MapForce MapForceFor(const MapData& map, const CharacterInstance& character);

// Whether `map` asks for either force: Arcane River and Grandis, which are
// also where V Points fall.
bool AsksForForce(const MapData& map);

}  // namespace ms

#endif  // MS_SRC_MAP_FORCE_H_
