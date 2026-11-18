// Measures each Sacred Symbol area's clears for a save's played character, or
// for sheets fetched from the server, and prints what the daily would pay
// under the median and under the 90th percentile clear.
//
//   bazel-bin/analysis/sacred_clear_sim --save=<copy>
//   bazel-bin/analysis/sacred_clear_sim --sheets=<dir of PlayerInfo files>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/sim_world.h"
#include "src/character/dailies.h"
#include "src/character/link.h"
#include "src/character/progression.h"
#include "src/combat/clear_time.h"
#include "src/game_state.h"
#include "src/protos/multiplayer.pb.h"
#include "src/sacred_dailies.h"
#include "src/save.h"

ABSL_FLAG(std::string, save, "",
          "A save to read, its played character. Use a copy.");
ABSL_FLAG(std::string, sheets, "",
          "A directory of serialized PlayerInfo messages, one per player.");

namespace ms {
namespace {

int Copies(const SacredArea& area, double share) {
  return SacredCopies(area.symbol->sacred_symbol().daily_copies(), share);
}

void Report(const GameState& state, const std::string& who) {
  const double speed = GameSpeedFactor(state.character.proto().level());
  auto begin = std::chrono::steady_clock::now();
  std::vector<SacredArea> areas = MeasureSacredAreas(state);
  double ms = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - begin)
                  .count();
  std::printf("\n%s (level %d, measured in %.0f ms)\n", who.c_str(),
              state.character.proto().level(), ms);
  if (areas.empty()) {
    std::printf("  no Sacred dailies (needs the 6th advancement)\n");
    return;
  }
  std::printf("  %-30s %6s %8s %8s %8s %12s %12s\n", "Symbol", "Clears",
              "Median", "P90", "Slowest", "Median pays", "P90 pays");
  for (const SacredArea& area : areas) {
    double median = ClearPercentile(area.clears, 0.5);
    double p90 = ClearPercentile(area.clears, 0.9);
    double slowest = ClearPercentile(area.clears, 1.0);
    double median_share = SacredShare(median, speed);
    double p90_share = SacredShare(p90, speed);
    std::printf(
        "  %-30s %6zu %7.1fs %7.1fs %7.1fs %4.0f%% %3d/%-3d %4.0f%% %3d/%-3d\n",
        area.symbol->name().c_str(), area.clears.size(), median, p90, slowest,
        100 * median_share, Copies(area, median_share),
        area.symbol->sacred_symbol().daily_copies(), 100 * p90_share,
        Copies(area, p90_share), area.symbol->sacred_symbol().daily_copies());
  }
}

int Run() {
  const Catalogs catalogs = LoadCatalogs();
  if (!absl::GetFlag(FLAGS_save).empty()) {
    GameState state = NewState(catalogs, 1);
    state.bosses = catalogs.bosses;
    LoadResult load = LoadGameFromFile(state, absl::GetFlag(FLAGS_save));
    if (load.status != LoadStatus::kLoaded) {
      std::fprintf(stderr, "%s\n", load.message.c_str());
      return 1;
    }
    Report(state, state.character.username());
  }
  if (!absl::GetFlag(FLAGS_sheets).empty()) {
    for (const auto& file :
         std::filesystem::directory_iterator(absl::GetFlag(FLAGS_sheets))) {
      std::ifstream in(file.path(), std::ios::binary);
      std::string bytes((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
      PlayerInfo player;
      if (!player.ParseFromString(bytes)) {
        std::fprintf(stderr, "can't read %s\n", file.path().c_str());
        continue;
      }
      GameState state = NewState(catalogs, 1);
      state.bosses = catalogs.bosses;
      state.character.RestoreFrom(player.sheet(), state.equips, state.items);
      state.character.set_autoswap_presets(player.autoswap_presets());
      LinkTally tally;
      for (const auto& [line, level] : player.link_lines()) {
        tally.Record(static_cast<Job>(line), level);
      }
      state.character.set_link_tally(std::move(tally));
      state.character.UseEquipSets(state.equip_sets);
      Report(state, player.name());
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
