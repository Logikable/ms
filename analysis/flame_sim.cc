/* flame_sim: whether the two Rebirth Flames are priced against each other.
 *
 * Each branch's --mode=max character is rolled on every piece that takes a
 * flame. For a target gain, a flame's cost is its price over the share of its
 * rolls that reach the target; keeping the better roll, as Black does, doesn't
 * change that. Targets are Black's own quantiles, so the row reads as "a
 * Black roll this good costs Burning this many times as much". Above 1 Black
 * is the cheaper way there.
 *
 *   bazelisk run //analysis:flame_sim
 *   bazelisk run //analysis:flame_sim -- --level=250 --branch=HERO
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/cube_plan.h"
#include "analysis/flame_plan.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "analysis/yardstick.h"
#include "src/character/job_advancement.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/protos/equip.pb.h"

ABSL_FLAG(int, level, 260, "Level of the max characters rolled on.");
ABSL_FLAG(std::string, branch, "",
          "One branch, as its Job enum name without the JOB_ prefix. Empty "
          "rolls every branch with a 4th job.");
ABSL_FLAG(int, samples, 20000, "Rolls of each flame on each piece.");

namespace ms {
namespace {

constexpr double kQuantiles[] = {0.5, 0.75, 0.9, 0.95, 0.99};
constexpr int kQuantileCount = sizeof(kQuantiles) / sizeof(kQuantiles[0]);

// The share of `sorted` at or above `target`.
double ShareReaching(const std::vector<double>& sorted, double target) {
  auto at = std::lower_bound(sorted.begin(), sorted.end(), target);
  return static_cast<double>(sorted.end() - at) / sorted.size();
}

// One slot's results, summed over branches as logs so the mean is geometric.
struct SlotRow {
  int branches = 0;
  double black_median_pct = 0.0;
  double burning_median_pct = 0.0;
  double log_ratio[kQuantileCount] = {};
  // Branches where no Burning roll reached the target at all.
  int unreachable[kQuantileCount] = {};
};

void Roll(const Catalogs& catalogs, Job branch, int level, int samples,
          std::map<EquipSlot, SlotRow>& rows) {
  GameState state = NewMaxState(
      catalogs, AdvancementForJobStage(branch, StageOf(branch)), level, 1);
  state.bosses = catalogs.bosses;
  const Yardstick yard = YardstickFor(state);
  const CubeBasis basis = CubeBasisFor(state, yard);
  std::vector<EquipSlot> slots;
  for (const auto& [slot, item] : state.character.equipped(kBossGear)) {
    if (item->CanFlame()) {
      slots.push_back(slot);
    }
  }
  const double burning_price = FlameOf(FlameType::kBurning).cost;
  const double black_price = FlameOf(FlameType::kBlack).cost;
  for (EquipSlot slot : slots) {
    std::mt19937 rng(static_cast<unsigned>(slot) * 7919u + 1u);
    double standing = 0.0;
    const std::vector<double> burning = FlameGains(
        state, basis, slot, FlameType::kBurning, samples, rng, &standing);
    const std::vector<double> black =
        FlameGains(state, basis, slot, FlameType::kBlack, samples, rng);
    if (black.empty() || standing <= 0.0) {
      continue;
    }
    SlotRow& row = rows[slot];
    ++row.branches;
    row.black_median_pct += 100.0 * black[black.size() / 2] / standing;
    row.burning_median_pct += 100.0 * burning[burning.size() / 2] / standing;
    for (int q = 0; q < kQuantileCount; ++q) {
      const double target =
          black[static_cast<size_t>(kQuantiles[q] * (black.size() - 1))];
      const double black_share = ShareReaching(black, target);
      const double burning_share = ShareReaching(burning, target);
      if (burning_share <= 0.0) {
        ++row.unreachable[q];
        continue;
      }
      const double ratio =
          (burning_price / burning_share) / (black_price / black_share);
      row.log_ratio[q] += std::log(ratio);
    }
  }
}

void Run() {
  const int level = absl::GetFlag(FLAGS_level);
  const int samples = absl::GetFlag(FLAGS_samples);
  const Catalogs catalogs = LoadCatalogs();
  std::vector<Job> branches;
  if (absl::GetFlag(FLAGS_branch).empty()) {
    for (Job job : EveryBranch()) {
      if (StageOf(job) == 4) {
        branches.push_back(job);
      }
    }
  } else {
    branches.push_back(ParseBranch(absl::GetFlag(FLAGS_branch)));
  }
  std::map<EquipSlot, SlotRow> rows;
  for (Job branch : branches) {
    Roll(catalogs, branch, level, samples, rows);
  }
  std::printf(
      "Burning's meso over Black's to reach a roll as good as Black's own "
      "quantile, over %zu\nbranches at level %d (geometric mean). Above 1, "
      "Black is cheaper; * counts branches\nno Burning roll reached. The "
      "medians are one roll's damage gain on a bare piece.\n\n",
      branches.size(), level);
  std::printf("  %-18s %8s %8s", "slot", "burn med", "blk med");
  for (double q : kQuantiles) {
    std::printf("  %7s",
                ("q" + std::to_string(static_cast<int>(q * 100))).c_str());
  }
  std::printf("\n");
  for (const auto& [slot, row] : rows) {
    std::string name = EquipSlot_Name(slot);
    name = name.substr(std::string("EQUIP_SLOT_").size());
    std::printf("  %-18s %7.2f%% %7.2f%%", name.c_str(),
                row.burning_median_pct / row.branches,
                row.black_median_pct / row.branches);
    for (int q = 0; q < kQuantileCount; ++q) {
      const int reached = row.branches - row.unreachable[q];
      char cell[32];
      if (reached == 0) {
        std::snprintf(cell, sizeof(cell), "never");
      } else {
        std::snprintf(cell, sizeof(cell), "%.2f%s",
                      std::exp(row.log_ratio[q] / reached),
                      row.unreachable[q] > 0 ? "*" : "");
      }
      std::printf("  %7s", cell);
    }
    std::printf("\n");
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
