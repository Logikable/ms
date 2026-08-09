/* Projects lifetime meso income across the whole 1-300 level range.
 *
 * meso_sim drives the real engine and so can only reach the level cap, which
 * is where the game's maps stop. This one is a closed form: it takes the same
 * EXP table and the same loot formulas the game uses, pairs them with GMS's
 * real mob EXP per level (analysis/gms_mob_exp.h), and reports the meso a
 * player holds on reaching each milestone.
 *
 * The 1-60 block it prints is the check -- it should sit within a few percent
 * of what //analysis:meso_sim measures by playing.
 *
 * Scratch analysis tool, not part of the game.
 */
#include <cstdio>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "analysis/gms_mob_exp.h"
#include "src/character/exp_table.h"
#include "src/combat/loot.h"
#include "src/protos/mob.pb.h"

ABSL_FLAG(int, etc_stops_at, 200,
          "First level at which mobs stop dropping Etc items. GMS's Arcane "
          "River drops nothing from 200. Past the table to disable.");
ABSL_FLAG(int, meso_stops_at, 100000,
          "First level at which mobs stop dropping meso.");
ABSL_FLAG(std::string, milestones, "40,110,200,230,260",
          "Comma-separated levels to report.");
ABSL_FLAG(double, etc_per_kill, 0.4,
          "Expected Etc drops per kill. Every mob in data/mobs drops one at "
          "0.4.");
ABSL_FLAG(double, etc_price_per_level, 2.0,
          "Etc sell price as a multiple of the mob's level. Every item in "
          "data/items/etc is 2x, give or take four rounded entries.");

namespace {

// What one kill of a level-appropriate mob is worth. The player is assumed to
// train at their own level, which the map ladder keeps them near and which
// leaves them inside MesoLevelPenalty's free +/-10 band the whole way.
struct Kill {
  double exp = 0.0;
  double meso = 0.0;
  double etc = 0.0;
};

Kill KillAt(int level) {
  ms::Mob mob;
  mob.set_level(level);
  Kill kill;
  kill.exp = ms::GmsMobExpPerKill(level);
  kill.meso = ms::ExpectedMesoPerKill(mob, level);
  kill.etc = absl::GetFlag(FLAGS_etc_per_kill) *
             absl::GetFlag(FLAGS_etc_price_per_level) * level;
  return kill;
}

// Cumulative meso on reaching each level: index i holds the total at level i.
// Two running totals, because the question is always what the Etc sales are
// worth on top of the meso drops rather than instead of them.
struct Totals {
  std::vector<double> meso;
  std::vector<double> etc;
};

Totals Accumulate() {
  Totals totals;
  totals.meso.assign(ms::kMaxLevel + 1, 0.0);
  totals.etc.assign(ms::kMaxLevel + 1, 0.0);
  double meso = 0.0;
  double etc = 0.0;
  for (int level = 1; level < ms::kMaxLevel; ++level) {
    Kill kill = KillAt(level);
    if (kill.exp > 0.0) {
      double kills = static_cast<double>(ms::ExpToNextLevel(level)) / kill.exp;
      if (level < absl::GetFlag(FLAGS_meso_stops_at)) {
        meso += kills * kill.meso;
      }
      if (level < absl::GetFlag(FLAGS_etc_stops_at)) {
        etc += kills * kill.etc;
      }
    }
    totals.meso[level + 1] = meso;
    totals.etc[level + 1] = etc;
  }
  return totals;
}

std::vector<int> ParseMilestones() {
  std::vector<int> levels;
  std::string spec = absl::GetFlag(FLAGS_milestones);
  std::string digits;
  for (int i = 0; i <= static_cast<int>(spec.size()); ++i) {
    if (i == static_cast<int>(spec.size()) || spec[i] == ',') {
      if (!digits.empty()) {
        levels.push_back(std::stoi(digits));
        digits.clear();
      }
    } else if (spec[i] != ' ') {
      digits.push_back(spec[i]);
    }
  }
  if (levels.empty()) {
    LOG(FATAL) << "--milestones named no levels";
  }
  for (int level : levels) {
    if (level < 2 || level > ms::kMaxLevel) {
      LOG(FATAL) << "--milestones level " << level << " is outside 2.."
                 << ms::kMaxLevel;
    }
  }
  return levels;
}

// Meso reads in the hundreds of millions by the end, which no column width
// survives; k/M/B/T keeps the table comparable at a glance.
void FormatShort(double value, char* out, int size) {
  const char* suffix[] = {"T", "B", "M", "k"};
  const double scale[] = {1e12, 1e9, 1e6, 1e3};
  for (int i = 0; i < 4; ++i) {
    if (value >= scale[i]) {
      snprintf(out, size, "%.3g%s", value / scale[i], suffix[i]);
      return;
    }
  }
  snprintf(out, size, "%.0f", value);
}

void PrintMilestones(const Totals& totals) {
  printf("\ncumulative meso earned, nothing spent");
  if (absl::GetFlag(FLAGS_etc_stops_at) <= ms::kMaxLevel) {
    printf(" (Etc drops stop at %d)", absl::GetFlag(FLAGS_etc_stops_at));
  }
  printf("\n%6s %18s %18s %10s\n", "level", "meso only", "+ Etc sold",
         "Etc share");
  std::vector<int> levels = ParseMilestones();
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    int level = levels[i];
    double meso = totals.meso[level];
    double both = meso + totals.etc[level];
    printf("%6d %18.0f %18.0f %9.1f%%\n", level, meso, both,
           both > 0.0 ? 100.0 * totals.etc[level] / both : 0.0);
  }
}

// The only levels the game can be played across, so the only place the model
// can be held against something measured. Compare with //analysis:meso_sim.
void PrintEngineCheck(const Totals& totals) {
  printf("\nthe range //analysis:meso_sim can check by playing\n");
  const int kChecks[] = {5, 10, 15, 20, 25, 30, 40, 50, 60};
  for (int i = 0; i < 9; ++i) {
    int level = kChecks[i];
    printf("%6d %18.0f\n", level, totals.meso[level] + totals.etc[level]);
  }
}

// Why the curve flattens: meso per kill is linear in mob level and capped by
// MeanMesoMultiplier above 90, while mob EXP keeps climbing.
void PrintPerKill() {
  printf("\nwhat one kill pays, by level\n");
  printf("%6s %12s %12s %12s %12s\n", "level", "EXP", "meso", "Etc",
         "meso per EXP");
  const int kRows[] = {10, 30, 60, 90, 110, 140, 170, 199, 230, 260};
  for (int i = 0; i < 10; ++i) {
    Kill kill = KillAt(kRows[i]);
    char exp[32];
    FormatShort(kill.exp, exp, sizeof(exp));
    printf("%6d %12s %12.0f %12.0f %12.3f\n", kRows[i], exp, kill.meso,
           kill.etc, kill.exp > 0.0 ? (kill.meso + kill.etc) / kill.exp : 0.0);
  }
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  Totals totals = Accumulate();
  PrintMilestones(totals);
  PrintEngineCheck(totals);
  PrintPerKill();
  return 0;
}
