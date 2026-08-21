/* The shape of the mob ladder: what every mob costs to kill and pays for it,
 * and what that comes to on each map once spawn counts are taken in.
 *
 * Two tables. The first walks the mobs in level order and prints, beside each,
 * how fast HP, EXP and attack grew per level since the mob below it. A ladder
 * that follows a curve holds those steady; a mob that is off the curve shows
 * as a spike and is paid for by a dip on whichever mob comes next. Attack has
 * its own curve, and it can be out of step with the other two -- what a mob
 * costs to kill and what it does to the player on the way are set separately.
 *
 * The second weights each map by its spawn counts, which is what a player
 * actually meets, and carries the counts through to a kill rate. EXP/HP is the
 * column to read: spawn count caps kills per second, so a map with twice the
 * HP at its level pays about half the EXP per second.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:mob_curve_sim
 *   bazelisk run //analysis:mob_curve_sim -- --maps=false
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/combat/constants.h"
#include "src/embedded_data.h"
#include "src/proto_loader.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

ABSL_FLAG(bool, mobs, true, "Print the per-mob ladder.");
ABSL_FLAG(bool, maps, true, "Print the per-map table.");

namespace ms {
namespace {

// One map, with its spawn counts already folded in.
struct MapRow {
  std::string name;
  double level = 0.0;
  double hp = 0.0;
  double exp = 0.0;
  double attack = 0.0;
  int spawns = 0;
};

// The compound per-level growth from `from` to `to` across `levels` levels,
// as a multiplier. Reported rather than the plain ratio so mobs spaced one
// level apart and mobs spaced six apart can be compared in the same column.
double GrowthPerLevel(double from, double to, int levels) {
  if (from <= 0.0 || to <= 0.0 || levels <= 0) {
    return 0.0;
  }
  return std::pow(to / from, 1.0 / levels);
}

std::vector<Mob> MobsByLevel(const std::map<std::string, Mob>& mobs) {
  std::vector<Mob> ladder;
  for (const std::pair<const std::string, Mob>& entry : mobs) {
    ladder.push_back(entry.second);
  }
  std::sort(ladder.begin(), ladder.end(), [](const Mob& a, const Mob& b) {
    return a.level() != b.level() ? a.level() < b.level() : a.name() < b.name();
  });
  return ladder;
}

void PrintMobs(const std::map<std::string, Mob>& mobs) {
  printf("\nthe ladder, in level order\n\n");
  printf("%-22s %4s %9s %6s %6s %8s %8s %8s %8s\n", "mob", "lv", "hp", "exp",
         "att", "exp/hp", "hp x/lv", "exp x/lv", "att x/lv");
  std::vector<Mob> ladder = MobsByLevel(mobs);
  for (int i = 0; i < static_cast<int>(ladder.size()); ++i) {
    const Mob& mob = ladder[i];
    double exp_per_hp =
        mob.max_hp() > 0 ? static_cast<double>(mob.exp()) / mob.max_hp() : 0.0;
    printf("%-22s %4d %9d %6d %6d %8.3f", mob.name().c_str(), mob.level(),
           mob.max_hp(), mob.exp(), mob.attack(), exp_per_hp);
    // Nothing to grow from on the first row, and nothing to grow across
    // between two mobs sharing a level -- the mushrooms do.
    int steps = i == 0 ? 0 : mob.level() - ladder[i - 1].level();
    if (steps <= 0) {
      printf(" %8s %8s %8s\n", "--", "--", "--");
      continue;
    }
    const Mob& below = ladder[i - 1];
    printf(" %8.3f %8.3f %8.3f\n",
           GrowthPerLevel(below.max_hp(), mob.max_hp(), steps),
           GrowthPerLevel(below.exp(), mob.exp(), steps),
           GrowthPerLevel(below.attack(), mob.attack(), steps));
  }
}

// A map's mobs averaged by spawn count: two of a thing and four of another is
// not the same encounter as one of each, and the counts are the dial the maps
// are tuned on.
MapRow WeightMap(const MapData& map, const std::map<std::string, Mob>& mobs) {
  MapRow row;
  row.name = map.name();
  for (const Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator it = mobs.find(spawn.mob());
    if (it == mobs.end()) {
      continue;
    }
    row.spawns += spawn.count();
    row.level += it->second.level() * spawn.count();
    row.hp += it->second.max_hp() * spawn.count();
    row.exp += it->second.exp() * spawn.count();
    row.attack += it->second.attack() * spawn.count();
  }
  if (row.spawns > 0) {
    row.level /= row.spawns;
    row.hp /= row.spawns;
    row.exp /= row.spawns;
    row.attack /= row.spawns;
  }
  return row;
}

void PrintMaps(const std::map<std::string, MapData>& maps,
               const std::map<std::string, Mob>& mobs) {
  std::vector<MapRow> rows;
  for (const std::pair<const std::string, MapData>& entry : maps) {
    MapRow row = WeightMap(entry.second, mobs);
    // A town has no mobs to average. Nothing here has anything to say about it.
    if (row.spawns > 0) {
      rows.push_back(row);
    }
  }
  std::sort(rows.begin(), rows.end(),
            [](const MapRow& a, const MapRow& b) { return a.level < b.level; });

  printf("\nwhat a player meets, weighted by spawn count\n");
  printf("dps to cap is the damage per second that holds the spawn cap; ");
  printf("compare //analysis:weapon_sim\n\n");
  printf("%-32s %5s %6s %9s %7s %7s %8s %8s %8s %10s\n", "map", "lv", "spawn",
         "avg hp", "avg exp", "avg att", "exp/hp", "kills/s", "exp/s",
         "dps to cap");
  for (const MapRow& row : rows) {
    double kills = row.spawns / kRespawnIntervalSeconds;
    printf("%-32s %5.1f %6d %9.0f %7.1f %7.0f %8.3f %8.2f %8.1f %10.0f\n",
           row.name.c_str(), row.level, row.spawns, row.hp, row.exp, row.attack,
           row.hp > 0.0 ? row.exp / row.hp : 0.0, kills, kills * row.exp,
           kills * row.hp);
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  std::map<std::string, ms::Mob> mobs =
      ms::LoadTextProtoMap<ms::Mob>(ms::EmbeddedMobs());
  std::map<std::string, ms::MapData> maps =
      ms::LoadTextProtoMap<ms::MapData>(ms::EmbeddedMaps());
  if (absl::GetFlag(FLAGS_mobs)) {
    ms::PrintMobs(mobs);
  }
  if (absl::GetFlag(FLAGS_maps)) {
    ms::PrintMaps(maps, mobs);
  }
  printf("\n");
  return 0;
}
