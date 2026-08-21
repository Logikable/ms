/* The game's catalogs, loaded once, and a character to try them on.
 *
 * Every sim that plays the game forward starts the same two ways: read the
 * data compiled into the binary, then stand up a GameState on it. That is
 * here rather than in each of them, so a catalog added to the game is added
 * to every sim at once.
 */
#ifndef MS_ANALYSIS_SIM_WORLD_H_
#define MS_ANALYSIS_SIM_WORLD_H_

#include <map>
#include <string>

#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Everything the game ships, by catalog key. Held apart from any one
// GameState because a sweep builds a character per row and they all read the
// same data.
struct Catalogs {
  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
  std::map<std::string, Boss> bosses;
};

Catalogs LoadCatalogs();

// A fresh level 1 character in a world built from `catalogs`. `seed` fixes the
// random stream: rewards are rolled, so an unseeded sweep would print a table
// that moved a little each run and hide a real change under the noise.
GameState NewState(const Catalogs& catalogs, unsigned int seed);

// The maps worth fighting on: the ones with something on them, in the order a
// player meets them -- by weighted mob level, then by key so two maps of a
// level hold the same order every run.
std::vector<std::string> HuntingGrounds(const Catalogs& catalogs);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_WORLD_H_
