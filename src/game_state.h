/* GameState holds all runtime game state: loaded static data, a seeded RNG,
 * and the active character. Constructed once at startup with the loaded equip
 * and scroll maps. Non-copyable because CharacterInstance holds a reference
 * to rng.
 */
#ifndef MS_SRC_GAME_STATE_H_
#define MS_SRC_GAME_STATE_H_

#include <cstdint>
#include <map>
#include <random>
#include <string>

#include "src/character/character.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The two states the game can be started in.
//
// kPlay is the game as a player meets it: a level 1 Beginner on Maple Island
// with a Sword and nothing else. kTest is the workbench -- a level 10 Beginner
// standing at their first advancement, with the meso and the spread of items
// the upgrade and shop screens need to be exercised without playing up to
// them.
enum class GameMode {
  kPlay,
  kTest,
};

struct GameState {
  // Constructs the catalogs and puts the character, bag and map into the
  // state `mode` begins from. Anything the catalogs do not name is skipped, so
  // a state built for a test need not carry the game's data files.
  GameState(std::map<std::string, EquipPrototype> equips,
            std::map<std::string, Scroll> scrolls,
            std::map<std::string, ItemPrototype> items,
            std::map<std::string, Mob> mobs,
            std::map<std::string, MapData> maps,
            std::map<std::string, Skill> skills = {},
            GameMode mode = GameMode::kPlay);
  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
  std::mt19937 rng;
  CharacterInstance character;

  // Name of the map being farmed (key into `maps`); empty means none.
  std::string current_map;
  // Fractional drops banked per item name, carried across AdvanceCombat calls.
  // Whole drops are deposited into the character's stackable storage.
  std::map<std::string, double> drop_progress;
  // Fractional meso banked across AdvanceCombat calls; whole meso is added to
  // the character's balance.
  double meso_progress = 0.0;
};

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
