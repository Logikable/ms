// Farms a save's character on chosen maps with chosen totems, faster than real
// time, through the same per-tick path the game runs (AdvanceCombat), so rent,
// drops and level-ups land as they would live. Prints what each pairing earns
// per real second.
//
//   bazel-bin/analysis/save_farm_sim --save=<copy> \
//     --maps="End of the World 2-7,Cernium Eastern City Ramparts 1" \
//     --totems=wild,frenzy

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "analysis/sim_world.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/save.h"

ABSL_FLAG(std::string, save, "", "The save to read. Use a copy.");
ABSL_FLAG(std::string, maps, "",
          "Comma-separated maps, by name or key. Empty: the saved map.");
ABSL_FLAG(std::string, totems, "saved",
          "Comma-separated totem settings to try: saved, none, wild, frenzy.");
ABSL_FLAG(double, seconds, 1200.0,
          "Real seconds measured per pairing. 1200 is two minutes of game "
          "time at 10x game speed, so most buffs cycle at least once.");
ABSL_FLAG(double, settle, 30.0,
          "Real seconds farmed before measuring, so the full opening map's "
          "burst isn't counted.");
ABSL_FLAG(double, step, 0.15,
          "Seconds per tick: the game's own repaint interval (kMarqueeStep).");

namespace ms {
namespace {

// What one pairing earned over the measured window.
struct FarmRate {
  double exp = 0.0;
  double meso = 0.0;  // the purse's change: drops less rent
  double rent = 0.0;
  double etc_value = 0.0;  // what the Etc drops would sell for
  double kills = 0.0;
  bool died = false;
};

bool Load(const Catalogs& catalogs, const std::string& path, GameState& state) {
  state.bosses = catalogs.bosses;
  const LoadResult load = LoadGameFromFile(state, path);
  if (load.status != LoadStatus::kLoaded) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), load.message.c_str());
    return false;
  }
  return true;
}

// The catalog key for `name_or_key`, or "" when no map matches.
std::string MapKey(const Catalogs& catalogs, const std::string& name_or_key) {
  if (catalogs.maps.count(name_or_key) > 0) {
    return name_or_key;
  }
  for (const auto& [key, map] : catalogs.maps) {
    if (map.name() == name_or_key) {
      return key;
    }
  }
  return "";
}

// Switches both totems off, then `totem` on. "saved" leaves them alone.
bool SetTotem(CharacterInstance& character, const std::string& totem) {
  if (totem == "saved") {
    return true;
  }
  for (ConsumableType type :
       {CONSUMABLE_TYPE_WILD_TOTEM, CONSUMABLE_TYPE_FRENZY_TOTEM}) {
    if (character.ConsumableActive(type)) {
      character.ToggleConsumable(type);
    }
  }
  if (totem == "none") {
    return true;
  }
  if (totem == "wild") {
    return character.ToggleConsumable(CONSUMABLE_TYPE_WILD_TOTEM);
  }
  if (totem == "frenzy") {
    return character.ToggleConsumable(CONSUMABLE_TYPE_FRENZY_TOTEM);
  }
  return false;
}

double EtcValue(const CharacterInstance& character) {
  double value = 0.0;
  for (const StackableItem& stack : character.stackables()) {
    value +=
        static_cast<double>(stack.prototype().sell_price()) * stack.count();
  }
  return value;
}

// Farms for `seconds` of real time in steps of `step`; adds what each step pays
// to `rate` once `counting` is set.
void Farm(GameState& state, CombatSim& sim, double seconds, double step,
          bool counting, FarmRate& rate) {
  int level = state.character.proto().level();
  CombatParams params = ComputeCombatParams(state);
  for (double t = 0.0; t < seconds; t += step) {
    RewardTally tally = AdvanceCombat(state, sim, params, step);
    if (sim.view().died_this_step) {
      rate.died = true;
      return;
    }
    if (counting) {
      rate.exp += tally.exp;
      rate.rent += tally.consumable_cost;
      for (int64_t kills : sim.view().kills_this_step) {
        rate.kills += kills;
      }
    }
    // A level-up changes the character, so the fight is rebuilt as the live
    // game's per-tick rebuild would.
    if (state.character.proto().level() != level) {
      level = state.character.proto().level();
      params = ComputeCombatParams(state);
    }
  }
}

bool Measure(const Catalogs& catalogs, const std::string& path,
             const std::string& map, const std::string& totem, double seconds,
             double settle, double step, FarmRate& rate) {
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, path, state)) {
    return false;
  }
  if (!map.empty()) {
    state.current_map = map;
  }
  if (!SetTotem(state.character, totem)) {
    std::fprintf(stderr, "can't set totem %s\n", totem.c_str());
    return false;
  }
  CombatSim sim;
  Farm(state, sim, settle, step, /*counting=*/false, rate);
  const int64_t meso_before = state.character.meso();
  const double etc_before = EtcValue(state.character);
  Farm(state, sim, seconds, step, /*counting=*/true, rate);
  rate.meso = static_cast<double>(state.character.meso() - meso_before);
  rate.etc_value = EtcValue(state.character) - etc_before;
  for (double* field :
       {&rate.exp, &rate.meso, &rate.rent, &rate.etc_value, &rate.kills}) {
    *field /= seconds;
  }
  return true;
}

int Run() {
  const std::string path = absl::GetFlag(FLAGS_save);
  const Catalogs catalogs = LoadCatalogs();
  std::vector<std::string> maps;
  for (absl::string_view name :
       absl::StrSplit(absl::GetFlag(FLAGS_maps), ',', absl::SkipEmpty())) {
    const std::string key = MapKey(catalogs, std::string(name));
    if (key.empty()) {
      std::fprintf(stderr, "no map %s\n", std::string(name).c_str());
      return 1;
    }
    maps.push_back(key);
  }
  if (maps.empty()) {
    maps.push_back("");
  }
  const std::vector<std::string> totems =
      absl::StrSplit(absl::GetFlag(FLAGS_totems), ',', absl::SkipEmpty());
  std::printf("%-34s %-7s %12s %10s %8s %10s %8s\n", "Map", "Totem", "EXP/s",
              "Meso/s", "Rent/s", "Etc/s", "Kills/s");
  for (const std::string& map : maps) {
    for (const std::string& totem : totems) {
      FarmRate rate;
      if (!Measure(catalogs, path, map, totem, absl::GetFlag(FLAGS_seconds),
                   absl::GetFlag(FLAGS_settle), absl::GetFlag(FLAGS_step),
                   rate)) {
        return 1;
      }
      const std::string name =
          map.empty() ? "(saved)" : catalogs.maps.at(map).name();
      if (rate.died) {
        std::printf("%-34s %-7s died\n", name.c_str(), totem.c_str());
        continue;
      }
      std::printf("%-34s %-7s %12.0f %10.0f %8.0f %10.0f %8.2f\n", name.c_str(),
                  totem.c_str(), rate.exp, rate.meso, rate.rent, rate.etc_value,
                  rate.kills);
    }
  }
  return 0;
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  return ms::Run();
}
