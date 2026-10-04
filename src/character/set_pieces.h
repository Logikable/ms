/* Each job's own piece of the boss sets that come in per-class versions, as
 * catalog keys. Both workbench modes dress from these: kTest wears every tier
 * its level opens, kMax what //analysis:progression_sim wore (see
 * max_character.h). A job below its 4th advancement has no set weapon, so the
 * weapon lookups return "" for it.
 */
#ifndef MS_SRC_CHARACTER_SET_PIECES_H_
#define MS_SRC_CHARACTER_SET_PIECES_H_

#include <string>
#include <vector>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// The branch's Root Abyss hat, top and bottom.
std::vector<std::string> RootAbyssArmour(Job job);

// The Root Abyss (Fafnir) weapon, on the same line choice as the Frozen weapon
// the workbench gives.
std::string RootAbyssWeapon(Job job);

// The secondary Princess No's fragments buy. It belongs to no set.
std::vector<std::string> PrincessNoSecondary(Job job);

// The branch's AbsoLab armour: hat, top, bottom, shoes, gloves, cape and
// shoulder. The shoulder takes Cygnus's slot, so the Boss Accessory Set loses
// a piece to it.
std::vector<std::string> AbsoLabArmour(Job job);

// The AbsoLab weapon, on the same line choice as the two tiers below.
std::string AbsoLabWeapon(Job job);

// The branch's Arcane Umbra armour, the same seven slots as AbsoLab's.
std::vector<std::string> ArcaneUmbraArmour(Job job);

// The Arcane Umbra weapon, on the same line choice as AbsoLab's.
std::string ArcaneUmbraWeapon(Job job);

// The branch's Cygnus shoulder, sold on the shop's Equips shelf.
std::string CygnusShoulder(Job job);

// A family of per-class pieces, for a table that names the family and the slot
// instead of each job's item.
enum class SetFamily {
  kRootAbyss,   // hat, top, bottom and the Fafnir weapon
  kPrincessNo,  // the secondary
  kAbsoLab,
  kArcaneUmbra,
  kCygnus,  // the shoulder
};

// `job`'s piece of `family` in `slot`, or "" for a slot the family has none
// in.
std::string SetPieceFor(SetFamily family, EquipSlot slot, Job job);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SET_PIECES_H_
