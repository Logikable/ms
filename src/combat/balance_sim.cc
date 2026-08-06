/* Sweeps every map against a range of character levels and reports how the
 * fight goes: how long the character lasts, and how many mobs they get before
 * it happens. The tool for tuning the mob-hit cadence, which is the one number
 * in damage-taken that is ours rather than GMS's.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //src/combat:balance_sim
 */
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/character/character.h"
#include "src/character/progression.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

ABSL_FLAG(double, minutes, 5.0,
          "How long to farm each (level, map) pair before giving up on the "
          "character dying.");

namespace ms {
namespace {

// The step the sim is driven in. Small enough that no swing or mob hit is
// rounded away, and the same order as the frontend's ticker.
constexpr double kStepSeconds = 0.05;

// Levels to sweep. Every band boundary the game has in its first thirty
// levels, plus the ends.
constexpr int kLevels[] = {1, 10, 20, 30, 40, 50, 60};

struct Catalogs {
  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
};

Catalogs LoadCatalogs() {
  Catalogs c;
  c.equips = LoadTextProtoMap<EquipPrototype>(EmbeddedEquips());
  c.scrolls = LoadTextProtoMap<Scroll>(EmbeddedScrolls());
  c.items = LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  c.mobs = LoadTextProtoMap<Mob>(EmbeddedMobs());
  c.maps = LoadTextProtoMap<MapData>(EmbeddedMaps());
  c.skills = LoadTextProtoMap<Skill>(EmbeddedSkills());
  return c;
}

// Which stat this job's damage is built on, so the sweep spends AP the way a
// player would rather than leaving it in the pool.
StatField PrimaryStatFor(Job job) {
  switch (job) {
    case JOB_ARCHER:
      return STAT_FIELD_DEX;
    case JOB_MAGICIAN:
      return STAT_FIELD_INT;
    case JOB_ROGUE:
      return STAT_FIELD_LUK;
    default:
      return STAT_FIELD_STR;
  }
}

// Brings `state`'s character up to `level` the way a player gets there: taking
// each advancement of `path` as it is offered, spending every AP on the
// primary stat, and every SP on whatever it will buy.
//
// Which skill the SP goes into first is the order of the catalog, which is
// arbitrary -- but a book costs exactly what its levels pay out, so a
// character standing at the end of a stage has bought the whole of it either
// way. Only the levels in between differ.
void GrowTo(GameState& state, int level, const std::vector<Job>& path) {
  CharacterInstance& character = state.character;
  int taken = 0;
  while (character.proto().level() < level) {
    character.LevelUp();
    if (character.CanAdvanceJob() && taken < static_cast<int>(path.size())) {
      character.AdvanceJob(path[taken++]);
    }
    while (character.AllocateStat(PrimaryStatFor(character.proto().job()))) {
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      while (character.LearnSkill(entry.second)) {
      }
    }
  }
}

// What one (level, map) pairing came to.
struct Outcome {
  int64_t kills = 0;
  // Seconds of game time until the character died, or -1 if they never did.
  double death_seconds = -1.0;
  // The lowest their HP got as a share of their pool, for a run they survived:
  // a map they clear comfortably reads near 1, one they only just hold reads
  // near 0.
  double low_water = 1.0;
};

Outcome Farm(const Catalogs& catalogs, int level, const std::vector<Job>& path,
             const std::string& map, double seconds) {
  GameState state(catalogs.equips, catalogs.scrolls, catalogs.items,
                  catalogs.mobs, catalogs.maps, catalogs.skills,
                  GameMode::kPlay);
  GrowTo(state, level, path);
  state.current_map = map;

  Outcome outcome;
  CombatSim sim;
  for (double t = 0.0; t < seconds; t += kStepSeconds) {
    AdvanceCombat(state, sim, kStepSeconds);
    for (int64_t killed : sim.kills_this_step()) {
      outcome.kills += killed;
    }
    if (sim.died_this_step()) {
      outcome.death_seconds = t;
      return outcome;
    }
    if (sim.active() && sim.player_hp_fraction() < outcome.low_water) {
      outcome.low_water = sim.player_hp_fraction();
    }
  }
  return outcome;
}

// The mean level of what a map spawns, weighted by how many of each -- the
// same figure the map select sorts on, and the one worth reading a row
// against.
double MapLevel(const Catalogs& catalogs, const MapData& map) {
  double total = 0.0;
  double count = 0.0;
  for (const MapData::Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator it =
        catalogs.mobs.find(spawn.mob());
    if (it == catalogs.mobs.end()) {
      continue;
    }
    total += it->second.level() * spawn.count();
    count += spawn.count();
  }
  return count > 0.0 ? total / count : 0.0;
}

void Run(double seconds) {
  Catalogs catalogs = LoadCatalogs();
  // Maps in the order the player meets them, weakest first.
  std::vector<std::pair<double, std::string>> maps;
  for (const std::pair<const std::string, MapData>& entry : catalogs.maps) {
    if (entry.second.spawns().empty()) {
      continue;  // a town, with nothing to be killed by
    }
    maps.push_back({MapLevel(catalogs, entry.second), entry.first});
  }
  std::sort(maps.begin(), maps.end());

  std::printf("%-28s %5s", "map", "mobLv");
  for (int level : kLevels) {
    std::printf("  %13s", ("Lv" + std::to_string(level)).c_str());
  }
  std::printf("\n%s\n",
              std::string(34 + 15 * (sizeof(kLevels) / sizeof(kLevels[0])), '-')
                  .c_str());

  for (const std::pair<double, std::string>& map : maps) {
    std::printf("%-28s %5.1f", catalogs.maps.at(map.second).name().c_str(),
                map.first);
    for (int level : kLevels) {
      Outcome outcome = Farm(catalogs, level, {JOB_SWORDMAN, JOB_SPEARMAN},
                             map.second, seconds);
      char cell[32];
      if (outcome.death_seconds >= 0.0) {
        // Died: how long it took, and what they took with them.
        std::snprintf(cell, sizeof(cell), "died %.0fs/%lld",
                      outcome.death_seconds,
                      static_cast<long long>(outcome.kills));
      } else {
        // Survived: the body count, and how close it got.
        std::snprintf(cell, sizeof(cell), "%lld/%.0f%%",
                      static_cast<long long>(outcome.kills),
                      100.0 * outcome.low_water);
      }
      std::printf("  %13s", cell);
      std::fflush(stdout);
    }
    std::printf("\n");
  }
  std::printf(
      "\nSurvived: kills / lowest HP the run reached.  Died: how long they "
      "lasted / how many they got first.\n");
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run(absl::GetFlag(FLAGS_minutes) * 60.0);
  return 0;
}
