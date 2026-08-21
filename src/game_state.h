/* GameState holds all runtime game state: loaded static data, a seeded RNG,
 * and the active character. Constructed once at startup with the loaded equip
 * and scroll maps. Non-copyable because CharacterInstance holds a reference
 * to rng.
 */
#ifndef MS_SRC_GAME_STATE_H_
#define MS_SRC_GAME_STATE_H_

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <random>
#include <string>

#include "src/character/character.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The town a character begins in and is returned to when they die. A key into
// GameState::maps, and the one map name the code names for itself -- every
// other one is the player's choice.
inline constexpr char kHomeMap[] = "maple_island";

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
  //
  // `test_job` is --job, and applies to kTest alone: the advancement to start
  // at the top of, with that job's SP left unspent for the tester. Unset takes
  // the workbench's own job with its whole book already bought.
  //
  // `seed` fixes the random stream. The game leaves it unset and gets a
  // different one every time; a sim or a test passes one so that a run it
  // repeats pays what it paid last time -- rewards are rolled, and two runs of
  // the same fight otherwise disagree.
  GameState(std::map<std::string, EquipPrototype> equips,
            std::map<std::string, Scroll> scrolls,
            std::map<std::string, ItemPrototype> items,
            std::map<std::string, Mob> mobs,
            std::map<std::string, MapData> maps,
            std::map<std::string, Skill> skills = {},
            GameMode mode = GameMode::kPlay,
            JobAdvancement test_job = JOB_ADVANCEMENT_UNSPECIFIED,
            std::optional<unsigned int> seed = std::nullopt);
  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
  // The bosses that can be fought, by data file stem. Filled in after
  // construction rather than passed to it: nothing the constructor seeds
  // depends on them, and a boss is not somewhere a new character stands.
  std::map<std::string, Boss> bosses;
  std::mt19937 rng;
  CharacterInstance character;

  // What every EXP award from combat is multiplied by. kTest gets a standing
  // bonus so the workbench can climb the level ladder in a sitting rather than
  // farming the early levels at play speed; kPlay earns what it earns.
  int exp_multiplier = 1;

  // Name of the map being farmed (key into `maps`); empty means none.
  std::string current_map;

  // When this character was started, as seconds since the Unix epoch. Stamped
  // here rather than at save time so that both of the ways it can go
  // unanswered settle themselves: a new game gets the moment it began, and a
  // save written before the field existed keeps this value instead of loading
  // a zero. A load overwrites it only when the save carries one.
  int64_t created_unix_seconds;

  // Total seconds with the game open, across every session. Accumulated by the
  // frontend's tick from a monotonic clock, so changing the system time or
  // crossing a daylight-saving boundary neither grants nor takes playtime.
  double playtime_seconds = 0.0;
};

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
