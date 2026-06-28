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

  // Applies elapsed_seconds of farming on the current map: banks whole kills
  // per mob type, grants their EXP, and accrues their drops. No-op without a
  // current map or an equipped weapon.
  void AdvanceFarming(double elapsed_seconds);

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
  std::map<std::string, double> drop_progress;
  // Whole items farmed so far, keyed by item name. Temporary home until the
  // character has real stackable Use/Etc storage.
  std::map<std::string, int64_t> drop_counts;
};

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
