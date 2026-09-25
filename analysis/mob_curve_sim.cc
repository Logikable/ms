/* mob_curve_sim: the shape of the mob ladder. Prints what each mob costs to
 * kill and pays, and what that comes to on each map once spawn counts are
 * included.
 *
 * There are two tables. The first lists mobs in level order with how fast HP,
 * EXP and attack grew per level since the mob below. A ladder that follows a
 * curve keeps those steady; a mob off the curve shows as a spike, followed by a
 * dip on the next mob. Attack has its own curve and can be out of step with the
 * other two, since how hard a mob is to kill and how hard it hits are set
 * separately.
 *
 * That table is printed four times, because four ladders span the same levels
 * without sharing a curve: the overworld, Arcane River, Grandis, and the
 * bosses. Comparing across them is meaningless. Black Heaven next to the river
 * would read as a 65% drop in HP per level, which it isn't.
 *
 * The second table weights each map by its spawn counts, which is what a player
 * actually meets, and carries the counts through to a kill rate. EXP/HP is the
 * column to read: spawn count caps kills per second, so a map with twice the HP
 * at its level pays about half the EXP per second.
 *
 *   bazelisk run //analysis:mob_curve_sim
 *   bazelisk run //analysis:mob_curve_sim -- --maps=false
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/combat/constants.h"
#include "src/embedded_data.h"
#include "src/proto_loader.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

ABSL_FLAG(bool, mobs, true, "Print the per-mob ladder.");
ABSL_FLAG(bool, maps, true, "Print the per-map table.");

namespace ms {
namespace {

// One map, with its spawn counts already applied.
struct MapRow {
  std::string name;
  double level = 0.0;
  double hp = 0.0;
  double exp = 0.0;
  double attack = 0.0;
  int spawns = 0;
};

// Compound per-level growth from `from` to `to` over `levels` levels, as a
// multiplier. Used instead of the plain ratio so mobs one level apart and six
// levels apart compare in the same column.
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

// Mobs on maps that require a force, by data file stem: Arcane Force for the
// river, Sacred Power for Grandis. Only the map says so. Tenebris drops no
// symbol of its own, and level doesn't tell either: Black Heaven runs to 219
// and requires no force.
std::set<std::string> MobsAskedFor(const std::map<std::string, MapData>& maps,
                                   int (MapData::*force)() const) {
  std::set<std::string> asked;
  for (const std::pair<const std::string, MapData>& entry : maps) {
    if ((entry.second.*force)() == 0) {
      continue;
    }
    for (const Spawn& spawn : entry.second.spawns()) {
      asked.insert(spawn.mob());
    }
  }
  return asked;
}

void PrintLadder(const char* title, const std::vector<Mob>& ladder) {
  if (ladder.empty()) {
    return;
  }
  printf("\n%s, in level order\n\n", title);
  printf("%-22s %4s %11s %7s %6s %8s %8s %8s %8s\n", "mob", "lv", "hp", "exp",
         "att", "exp/hp", "hp x/lv", "exp x/lv", "att x/lv");
  for (int i = 0; i < static_cast<int>(ladder.size()); ++i) {
    const Mob& mob = ladder[i];
    double exp_per_hp =
        mob.max_hp() > 0 ? static_cast<double>(mob.exp()) / mob.max_hp() : 0.0;
    printf("%-22s %4d %11lld %7lld %6d %8.3f", mob.name().c_str(), mob.level(),
           static_cast<long long>(mob.max_hp()),
           static_cast<long long>(mob.exp()), mob.attack(), exp_per_hp);
    // Nothing to grow from on the first row, or between two mobs of the same
    // level (the mushrooms share one).
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

void PrintMobs(const std::map<std::string, Mob>& mobs,
               const std::map<std::string, MapData>& maps) {
  std::set<std::string> river_mobs = MobsAskedFor(maps, &MapData::arcane_force);
  std::set<std::string> grandis_mobs =
      MobsAskedFor(maps, &MapData::sacred_power);
  std::map<std::string, Mob> overworld;
  std::map<std::string, Mob> river;
  std::map<std::string, Mob> grandis;
  std::map<std::string, Mob> bosses;
  for (const std::pair<const std::string, Mob>& entry : mobs) {
    std::map<std::string, Mob>* ladder = &overworld;
    if (entry.second.boss()) {
      ladder = &bosses;
    } else if (river_mobs.count(entry.first) > 0) {
      ladder = &river;
    } else if (grandis_mobs.count(entry.first) > 0) {
      ladder = &grandis;
    }
    ladder->insert(entry);
  }
  PrintLadder("the overworld ladder", MobsByLevel(overworld));
  PrintLadder("Arcane River", MobsByLevel(river));
  PrintLadder("Grandis", MobsByLevel(grandis));
  // Bosses don't share a curve with each other either, since each is its own
  // fight at its own unlock level, so their growth columns mean nothing.
  // They're listed for the HP and attack each boss has at its level.
  PrintLadder("the bosses", MobsByLevel(bosses));
}

// Averages a map's mobs by spawn count. Two of one mob and four of another
// isn't the same encounter as one of each, and spawn counts are how maps are
// tuned.
MapRow WeightMap(const MapData& map, const std::map<std::string, Mob>& mobs) {
  MapRow row;
  row.name = map.name();
  for (const Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator it = mobs.find(spawn.mob());
    if (it == mobs.end()) {
      continue;
    }
    row.spawns += SpawnCount(spawn);
    row.level += it->second.level() * SpawnCount(spawn);
    row.hp += it->second.max_hp() * SpawnCount(spawn);
    row.exp += it->second.exp() * SpawnCount(spawn);
    row.attack += it->second.attack() * SpawnCount(spawn);
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
    // A town has no mobs to average.
    if (row.spawns > 0) {
      rows.push_back(row);
    }
  }
  std::sort(rows.begin(), rows.end(),
            [](const MapRow& a, const MapRow& b) { return a.level < b.level; });

  printf("\nwhat a player meets, weighted by spawn count\n");
  printf("dps to cap is the damage per second that holds the spawn cap; ");
  printf("compare //analysis:bench_sim\n\n");
  printf("%-32s %5s %6s %11s %9s %7s %8s %8s %10s %12s\n", "map", "lv", "spawn",
         "avg hp", "avg exp", "avg att", "exp/hp", "kills/s", "exp/s",
         "dps to cap");
  for (const MapRow& row : rows) {
    double kills = row.spawns / kRespawnIntervalSeconds;
    printf("%-32s %5.1f %6d %11.0f %9.1f %7.0f %8.3f %8.2f %10.1f %12.0f\n",
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
    ms::PrintMobs(mobs, maps);
  }
  if (absl::GetFlag(FLAGS_maps)) {
    ms::PrintMaps(maps, mobs);
  }
  printf("\n");
  return 0;
}
