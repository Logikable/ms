/* Loads the game's catalogs once and builds characters on them.
 *
 * Every sim that plays the game forward starts the same way: read the data
 * compiled into the binary, then build a GameState on it. Doing that here means
 * a catalog added to the game reaches every sim at once.
 */
#ifndef MS_ANALYSIS_SIM_WORLD_H_
#define MS_ANALYSIS_SIM_WORLD_H_

#include <map>
#include <string>

#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Every sim runs with Autoswap Presets on, whatever the game's default. A plan
// that builds separate farming and bossing allocations assumes a player who
// gets to use both.

// Everything the game ships, by catalog key. Kept apart from any GameState
// because a sweep builds a character per row and they all read the same data.
struct Catalogs {
  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
  std::map<std::string, Boss> bosses;
  std::map<std::string, EquipSet> sets;
};

Catalogs LoadCatalogs();

// Returns a fresh level 1 character in a world built from `catalogs`. `seed`
// fixes the random stream, since rewards are rolled and an unseeded sweep would
// hide real changes in noise. Includes equip sets, which make up most of the
// value of top-tier gear. Callers assign the bosses they fight themselves.
GameState NewState(const Catalogs& catalogs, unsigned int seed);

// Returns the character --mode=max seeds at `level` in the branch `advancement`
// ends in. Measuring against it compares with the game's own answer rather than
// a sim's.
GameState NewMaxState(const Catalogs& catalogs, JobAdvancement advancement,
                      int level, unsigned int seed);

// Returns the maps that have mobs on them, in the order a player meets them: by
// weighted mob level, then by key so maps of the same level keep a stable
// order.
std::vector<std::string> HuntingGrounds(const Catalogs& catalogs);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_WORLD_H_
