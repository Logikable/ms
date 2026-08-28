/* Sweeps every map against a range of character levels and reports how the
 * fight goes: how long the character lasts, and how many mobs they get before
 * it happens. The tool for tuning the mob-hit cadence, which is the one number
 * in damage-taken that is ours rather than GMS's.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:balance_sim
 */
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "analysis/parallel.h"
#include "analysis/sim_format.h"
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/progression.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/map_level.h"
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
ABSL_FLAG(std::string, job, "SPEARMAN",
          "The 2nd-job branch to sweep, as its Job enum name without the "
          "JOB_ prefix. What a character survives depends on their book.");
ABSL_FLAG(std::string, levels, "10,20,40,60,80,100,110,120,130,140",
          "Comma-separated character levels to sweep, one column each. Narrow "
          "it when the table is wider than the terminal. Starts at 10 because "
          "a level 1 has no job to sweep and ParseLevels refuses it, and ends "
          "at the cap, in tens through the band the Frozen tier is worn in.");
ABSL_FLAG(bool, drops, false,
          "Also wear the armour, accessories and symbol that drop rather than "
          "sell. Off by default, which is the shopped slots alone -- what this "
          "table was calibrated against. On is what a player is standing in, "
          "and is the only honest reading past level 140, where the map's own "
          "level is close enough to the character's that armour decides "
          "whether they hold it at all.");
ABSL_FLAG(std::string, maps, "",
          "Comma-separated map keys to sweep, one row each, in the order "
          "given. Empty sweeps every hunting ground, which is what the table "
          "is for -- name one when one map is the question.");
ABSL_FLAG(int, kills, 0,
          "Report the seconds it took to reach this many kills, rather than "
          "the kills a whole run came to. What it answers is how long a map "
          "takes to clear -- name the map's own spawn count. 0 is the "
          "body-count table this sim is otherwise.");

namespace ms {
namespace {

// Fixes the random stream every run of this sim draws from. Rewards are
// rolled, so an unseeded run would print a table that moved a little each
// time and hide a real change under the noise.
constexpr unsigned int kSimSeed = 20260813;

// The step the sim is driven in. Small enough that no swing or mob hit is
// rounded away, and the same order as the frontend's ticker.
constexpr double kStepSeconds = 0.05;

// Levels to sweep, from --levels. Read once: every row is the same sweep, and
// re-parsing per row would let a bad flag fail halfway through a table.
std::vector<int> SweptLevels() {
  return ParseLevels(absl::GetFlag(FLAGS_levels), "--levels");
}

// The rows, from --maps, or every hunting ground when it is empty. A key the
// catalog does not hold is fatal: a sweep quietly missing the map it was
// pointed at would read as a map nobody can clear.
std::vector<std::string> SweptMaps(const Catalogs& catalogs) {
  std::string spec = absl::GetFlag(FLAGS_maps);
  if (spec.empty()) {
    return HuntingGrounds(catalogs);
  }
  std::vector<std::string> maps;
  for (absl::string_view key : absl::StrSplit(spec, ',', absl::SkipEmpty())) {
    std::string name(absl::StripAsciiWhitespace(key));
    if (catalogs.maps.find(name) == catalogs.maps.end()) {
      LOG(FATAL) << "--maps names no such map '" << name << "'";
    }
    maps.push_back(name);
  }
  return maps;
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
  // Seconds to the --kills'th kill, or -1 where the run never reached it.
  double kills_seconds = -1.0;
};

Outcome Farm(const Catalogs& catalogs, int level, const std::vector<Job>& path,
             const std::string& map, double seconds, int target_kills) {
  GameState state = NewState(catalogs, kSimSeed);
  GrowTo(state, level, path);
  // Geared before the fight rather than during it: nothing here changes the
  // character once the run starts, so what a player would have bought by now
  // they have. Without a purse -- this table asks whether a build can hold a
  // map, and what it could afford is no part of that answer.
  Outfit(state, /*budget=*/false);
  if (absl::GetFlag(FLAGS_drops)) {
    OutfitDrops(state);
  }
  state.current_map = map;

  Outcome outcome;
  CombatSim sim;
  // Built once and rebuilt only when the character changes under it, which
  // here means levelling from the EXP the run itself earns. Rebuilding it
  // every step prices every attack against every mob afresh, and that was
  // almost the whole cost of this sim.
  CombatParams params = ComputeCombatParams(state);
  int at_level = state.character.proto().level();
  for (double t = 0.0; t < seconds; t += kStepSeconds) {
    AdvanceCombat(state, sim, params, kStepSeconds);
    if (state.character.proto().level() != at_level) {
      at_level = state.character.proto().level();
      params = ComputeCombatParams(state);
    }
    for (int64_t killed : sim.kills_this_step()) {
      outcome.kills += killed;
    }
    // The step the count came due on, which is as fine as this sim measures
    // anything. Recorded once: a roster that keeps topping up would otherwise
    // overwrite it with the second helping of the same number.
    if (target_kills > 0 && outcome.kills_seconds < 0.0 &&
        outcome.kills >= target_kills) {
      outcome.kills_seconds = t + kStepSeconds;
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

void Run(double seconds, Job branch) {
  int target_kills = absl::GetFlag(FLAGS_kills);
  Catalogs catalogs = LoadCatalogs();
  std::vector<Job> path = PathTo(branch);
  std::vector<std::string> maps = SweptMaps(catalogs);

  std::vector<int> levels = SweptLevels();
  std::printf("%-28s %5s", "map", "mobLv");
  for (int level : levels) {
    std::printf("  %13s", ("Lv" + std::to_string(level)).c_str());
  }
  std::printf("\n%s\n", std::string(34 + 15 * levels.size(), '-').c_str());

  // Every cell is its own character farming its own map, so the whole grid
  // runs at once and the table is printed from it afterwards.
  int rows = static_cast<int>(maps.size());
  int columns = static_cast<int>(levels.size());
  std::vector<std::string> cells(rows * columns);
  ParallelFor(rows * columns, [&](int i) {
    Outcome outcome = Farm(catalogs, levels[i % columns], path,
                           maps[i / columns], seconds, target_kills);
    char cell[32];
    // The count is the question where one was named, so a run that reached it
    // reports the time whether or not the character went on to die -- dying
    // half an hour later is a different fact from failing to clear the map.
    if (target_kills > 0 && outcome.kills_seconds >= 0.0) {
      if (outcome.death_seconds >= 0.0) {
        std::snprintf(cell, sizeof(cell), "%.1fs died %.0fs",
                      outcome.kills_seconds, outcome.death_seconds);
      } else {
        std::snprintf(cell, sizeof(cell), "%.1fs/%.0f%%", outcome.kills_seconds,
                      100.0 * outcome.low_water);
      }
    } else if (outcome.death_seconds >= 0.0) {
      // Died: how long it took, and what they took with them.
      std::snprintf(cell, sizeof(cell), "died %.0fs/%lld",
                    outcome.death_seconds,
                    static_cast<long long>(outcome.kills));
    } else if (target_kills > 0) {
      // Short of the count with the window run out, which is a map the
      // character holds and cannot clear.
      std::snprintf(cell, sizeof(cell), "%lld only/%.0f%%",
                    static_cast<long long>(outcome.kills),
                    100.0 * outcome.low_water);
    } else {
      // Survived: the body count, and how close it got.
      std::snprintf(cell, sizeof(cell), "%lld/%.0f%%",
                    static_cast<long long>(outcome.kills),
                    100.0 * outcome.low_water);
    }
    cells[i] = cell;
  });

  for (int row = 0; row < rows; ++row) {
    const MapData& map = catalogs.maps.at(maps[row]);
    std::printf("%-28s %5.1f", map.name().c_str(),
                MapLevel(catalogs.mobs, map));
    for (int column = 0; column < columns; ++column) {
      std::printf("  %13s", cells[row * columns + column].c_str());
    }
    std::printf("\n");
  }
  if (target_kills > 0) {
    std::printf(
        "\nReached %d kills: seconds it took / lowest HP the run reached.  "
        "Died: how long they lasted / how many they got first.\n",
        target_kills);
    return;
  }
  std::printf(
      "\nSurvived: kills / lowest HP the run reached.  Died: how long they "
      "lasted / how many they got first.\n");
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run(absl::GetFlag(FLAGS_minutes) * 60.0,
          ms::ParseBranch(absl::GetFlag(FLAGS_job), /*min_stage=*/2));
  return 0;
}
