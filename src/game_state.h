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

#include "src/character.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

struct GameState {
  GameState(std::map<std::string, EquipPrototype> equips,
            std::map<std::string, Scroll> scrolls,
            std::map<std::string, ItemPrototype> items,
            std::map<std::string, Mob> mobs,
            std::map<std::string, MapData> maps);
  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::mt19937 rng;
  CharacterInstance character;

  // Name of the map being farmed (key into `maps`); empty means none.
  std::string current_map;
  // Fractional kills banked per mob name, carried across AdvanceFarming calls.
  std::map<std::string, double> kill_progress;
  // Fractional drops banked per item name, carried across AdvanceFarming calls.
  // Whole drops are deposited into the character's stackable storage.
  std::map<std::string, double> drop_progress;
  // Fractional meso banked across AdvanceFarming calls; whole meso is added to
  // the character's balance.
  double meso_progress = 0.0;
};

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
