/* The Liberation: GMS's Genesis weapon questline as one boss gauntlet. GMS
 * sends the Adversary after the Black Mage's commanders, one mission a week,
 * each a solo fight against an ordinary boss with a handicap. Here the missions
 * are fought back to back in one run, with no quest, no Traces of Darkness and
 * nothing to do between fights; losing any fight ends the run. Winning pays the
 * Genesis weapon of the type worn, once per character.
 *
 * GMS's first mission, Hard Von Leon, is left out: there is no Von Leon here.
 */
#ifndef MS_SRC_COMBAT_BOSS_LIBERATION_H_
#define MS_SRC_COMBAT_BOSS_LIBERATION_H_

#include <map>
#include <string>

#include "src/character/character.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {

// What the boss screen lists it under. Its key and level are in game_state.h.
inline constexpr char kLiberationName[] = "Liberation";

// One mission: an ordinary boss difficulty and GMS's handicap on it.
struct LiberationMission {
  const char* boss;  // data/bosses key
  const char* difficulty;
  // The Final Damage left after GMS's cut: 0.25 for "decreased by 75%".
  double damage_factor;
  double hp_factor;
  bool sealed;
};

// GMS's current missions (maplestorywiki.net, Genesis Weapon), in order.
// Damien's, Will's and Lucid's handicaps are a death count, damage taken and a
// potion limit, none of which this game has, so those are plain solo fights.
inline constexpr LiberationMission kLiberationMissions[] = {
    {"arkarium", "Normal", 0.25, 1.0, true},
    {"magnus", "Hard", 0.50, 1.0, true},
    {"lotus", "Hard", 0.80, 1.0, false},
    {"damien", "Hard", 1.0, 1.0, false},
    {"will", "Hard", 1.0, 1.0, false},
    {"lucid", "Hard", 1.0, 1.0, false},
    {"verus_hilla", "Hard", 1.0, 0.75, false},
};
inline constexpr int kLiberationMissionCount =
    sizeof(kLiberationMissions) / sizeof(kLiberationMissions[0]);
// GMS's first awakening, after Lotus, grants Tanadian Ruin's 10% Final Damage
// for the missions after it.
inline constexpr int kTanadianRuinFromMission = 3;
inline constexpr double kTanadianRuinFactor = 1.10;

// Mission `index`'s handicaps, Tanadian Ruin's 10% folded in from Damien on.
MissionRules LiberationRules(int index);

// Whether this character holds a Genesis weapon, in the bag or worn in any
// preset.
bool HoldsGenesisWeapon(const CharacterInstance& character);

// Whether this character has won the Liberation or holds a Genesis weapon;
// either way it can't be run again.
bool Liberated(const CharacterInstance& character);

// The Genesis weapon of the type `state`'s character wears for bossing, or
// null when they wear none or no Genesis weapon is of that type.
const EquipPrototype* GenesisWeaponFor(const GameState& state);

// What a sealed mission is fought in: `genesis`, without its skills, and the
// secondary with no upgrades at all, the symbols and the projectile as they
// are, and nothing else. GMS allows the weapon, the secondary and the symbols;
// a projectile isn't equipment there.
std::map<EquipSlot, EquipInstance> SealedGear(const GameState& state,
                                              const EquipPrototype& genesis);

// Pays a won Liberation: `genesis` into the bag, fresh, and the character
// recorded as liberated. Returns false, paying nothing, if the bag is full.
bool GrantLiberation(GameState& state, const EquipPrototype& genesis);

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_LIBERATION_H_
