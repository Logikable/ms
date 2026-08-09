/* Reports the meso a player holds on reaching each milestone level, across the
 * whole 1-300 range.
 *
 * meso_sim drives the real engine and so can only reach the level cap, which
 * is where the game's maps stop. This prints the closed form that carries it
 * further -- see analysis/meso_curve.h for the model.
 *
 * The 1-60 block it prints is the check: it should sit within a few percent of
 * what //analysis:meso_sim measures by playing.
 *
 * Scratch analysis tool, not part of the game.
 */
#include <cstdio>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/meso_curve.h"
#include "analysis/sim_format.h"
#include "src/character/exp_table.h"

ABSL_FLAG(int, etc_stops_at, 200,
          "First level at which mobs stop dropping Etc items. GMS's Arcane "
          "River drops nothing from 200. Past the table to disable.");
ABSL_FLAG(int, meso_stops_at, 100000,
          "First level at which mobs stop dropping meso.");
ABSL_FLAG(std::string, milestones, "40,110,200,230,260",
          "Comma-separated levels to report.");

namespace {

void PrintMilestones(const ms::MesoCurve& curve) {
  printf("\ncumulative meso earned, nothing spent");
  if (absl::GetFlag(FLAGS_etc_stops_at) <= ms::kMaxLevel) {
    printf(" (Etc drops stop at %d)", absl::GetFlag(FLAGS_etc_stops_at));
  }
  printf("\n%6s %18s %18s %10s\n", "level", "meso only", "+ Etc sold",
         "Etc share");
  std::vector<int> levels =
      ms::ParseLevels(absl::GetFlag(FLAGS_milestones), "--milestones");
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    int level = levels[i];
    double both = curve.Total(level);
    printf("%6d %18.0f %18.0f %9.1f%%\n", level, curve.meso[level], both,
           both > 0.0 ? 100.0 * curve.etc[level] / both : 0.0);
  }
}

// The only levels the game can be played across, so the only place the model
// can be held against something measured. Compare with //analysis:meso_sim.
void PrintEngineCheck(const ms::MesoCurve& curve) {
  printf("\nthe range //analysis:meso_sim can check by playing\n");
  const int kChecks[] = {5, 10, 15, 20, 25, 30, 40, 50, 60};
  for (int i = 0; i < 9; ++i) {
    printf("%6d %18.0f\n", kChecks[i], curve.Total(kChecks[i]));
  }
}

// Why the curve flattens: meso per kill is linear in mob level and capped by
// MeanMesoMultiplier above 90, while mob EXP keeps climbing.
void PrintPerKill(const ms::MesoCurveParams& params) {
  printf("\nwhat one kill pays, by level\n");
  printf("%6s %12s %12s %12s %12s\n", "level", "EXP", "meso", "Etc",
         "meso per EXP");
  const int kRows[] = {10, 30, 60, 90, 110, 140, 170, 199, 230, 260};
  for (int i = 0; i < 10; ++i) {
    ms::KillValue kill = ms::KillValueAt(kRows[i], params);
    char exp[32];
    ms::FormatShort(kill.exp, exp, sizeof(exp));
    printf("%6d %12s %12.0f %12.0f %12.3f\n", kRows[i], exp, kill.meso,
           kill.etc, kill.exp > 0.0 ? (kill.meso + kill.etc) / kill.exp : 0.0);
  }
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::MesoCurveParams params;
  params.etc_stops_at = absl::GetFlag(FLAGS_etc_stops_at);
  params.meso_stops_at = absl::GetFlag(FLAGS_meso_stops_at);
  ms::MesoCurve curve = ms::BuildMesoCurve(params);
  PrintMilestones(curve);
  PrintEngineCheck(curve);
  PrintPerKill(params);
  return 0;
}
