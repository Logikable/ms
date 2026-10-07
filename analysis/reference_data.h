/* The data behind the player reference page: every table a player looks up for
 * cubes, star force and Inner Ability, as JSON.
 *
 * Read from the game's own functions and catalog, so a page built from it
 * can't drift from the game. The page's star force calculator recomputes gains
 * for an ATT or DEF the player types in; ReferenceDataJson checks that model
 * against StarForceStatGains and fails if the game no longer matches it.
 */
#ifndef MS_ANALYSIS_REFERENCE_DATA_H_
#define MS_ANALYSIS_REFERENCE_DATA_H_

#include <map>
#include <string>

#include "src/protos/equip.pb.h"

namespace ms {

// Every table, as one JSON object. `equips` decides which item levels and
// slots the tables list.
std::string ReferenceDataJson(
    const std::map<std::string, EquipPrototype>& equips);

// `page_template` with `json` in place of kReferenceDataPlaceholder. Fails if
// the template has no placeholder.
std::string ReferencePage(const std::string& page_template,
                          const std::string& json);

inline constexpr char kReferenceDataPlaceholder[] = "/*REFERENCE_DATA*/null";

}  // namespace ms

#endif  // MS_ANALYSIS_REFERENCE_DATA_H_
