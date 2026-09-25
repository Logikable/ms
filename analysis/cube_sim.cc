/* cube_sim: what a potential goal costs in cubes.
 *
 * Each goal cubes an item from no potential until it has what's wanted, printed
 * next to the cost of reaching the rank it needs. The distributions are
 * long-tailed: when a mean is twice its median, plan on the median and budget
 * for the tail.
 *
 *   bazelisk run //analysis:cube_sim
 *   bazelisk run //analysis:cube_sim -- --item_level=200 --trials=50000
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

ABSL_FLAG(int, item_level, 140, "Equipment level of the item being cubed.");
ABSL_FLAG(int, trials, 200000, "Runs to average each goal over.");
ABSL_FLAG(int, cap, 2000000, "Cubes one run may take before it is abandoned.");

namespace ms {
namespace {

using Predicate = std::function<bool(const Potential&, int)>;

struct Goal {
  std::string name;
  PotentialGroup group;
  Predicate met;
};

// Cost of one goal, over all its trials.
struct Cost {
  double mean = 0.0;
  int median = 0;
  int p90 = 0;
  int p99 = 0;
};

int LineValue(const PotentialLine& line, int item_level) {
  return PotentialLineValue(line.type(), line.rank(), item_level);
}

// Percent of the wearer's main stat a potential grants. All Stats counts, since
// it gives the main stat like any other line, one rank lower.
int MainStatPct(const Potential& potential, int item_level) {
  int total = 0;
  for (const PotentialLine& line : potential.lines()) {
    if (line.type() == POTENTIAL_LINE_TYPE_STR_PCT ||
        line.type() == POTENTIAL_LINE_TYPE_ALL_STATS_PCT) {
      total += LineValue(line, item_level);
    }
  }
  return total;
}

int CountLines(const Potential& potential,
               const std::vector<PotentialLineType>& wanted) {
  int found = 0;
  for (const PotentialLine& line : potential.lines()) {
    if (std::find(wanted.begin(), wanted.end(), line.type()) != wanted.end()) {
      ++found;
    }
  }
  return found;
}

// Lines worth having on a weapon: attack, boss damage and ignored defence, at
// any size. Magic attack is the magician equivalent and is left out so the
// count is for one job.
const std::vector<PotentialLineType>& UsefulWeaponLines() {
  static const std::vector<PotentialLineType>* kUseful =
      new std::vector<PotentialLineType>{
          POTENTIAL_LINE_TYPE_ATTACK_PCT,
          POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15,
          POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30,
          POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35,
          POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40,
          POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30,
          POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35,
          POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40,
      };
  return *kUseful;
}

Predicate AtLeastMainStat(int pct) {
  return [pct](const Potential& potential, int item_level) {
    return MainStatPct(potential, item_level) >= pct;
  };
}

Predicate HoldsLines(std::vector<PotentialLineType> wanted, int count) {
  return [wanted, count](const Potential& potential, int) {
    return CountLines(potential, wanted) >= count;
  };
}

// Both lines of a pair on one potential. This differs from two lines drawn from
// the pair: duplicates are allowed, so two -1s lines are two cooldown lines but
// still only -1s.
Predicate HoldsBoth(PotentialLineType first, PotentialLineType second) {
  return [first, second](const Potential& potential, int) {
    return CountLines(potential, {first}) >= 1 &&
           CountLines(potential, {second}) >= 1;
  };
}

Predicate LegendaryHolding(std::vector<PotentialLineType> wanted, int count) {
  return [wanted, count](const Potential& potential, int) {
    return potential.rank() == POTENTIAL_RANK_LEGENDARY &&
           CountLines(potential, wanted) >= count;
  };
}

Predicate ReachesRank(PotentialRank rank) {
  return [rank](const Potential& potential, int) {
    return potential.rank() >= rank;
  };
}

Cost Play(const Goal& goal, int item_level, int trials, int cap,
          std::mt19937& rng) {
  std::vector<int> spent;
  spent.reserve(trials);
  for (int trial = 0; trial < trials; ++trial) {
    Potential held;
    int cubes = 0;
    while (!goal.met(held, item_level) && cubes < cap) {
      held = CubePotential(held, CubeType::kRed, goal.group, rng);
      ++cubes;
    }
    spent.push_back(cubes);
  }
  std::sort(spent.begin(), spent.end());
  Cost cost;
  double total = 0.0;
  for (int one : spent) {
    total += one;
  }
  cost.mean = total / spent.size();
  cost.median = spent[spent.size() / 2];
  cost.p90 = spent[static_cast<size_t>(spent.size() * 0.90)];
  cost.p99 = spent[static_cast<size_t>(spent.size() * 0.99)];
  return cost;
}

void PrintHeader(const char* section) {
  std::printf("\n%s\n", section);
  std::printf("  %-44s %9s %8s %8s %8s\n", "goal", "mean", "median", "p90",
              "p99");
}

void PrintCost(const std::string& name, const Cost& cost) {
  std::printf("  %-44s %9.1f %8d %8d %8d\n", name.c_str(), cost.mean,
              cost.median, cost.p90, cost.p99);
}

void PrintRankLadder(int item_level, int trials, int cap, std::mt19937& rng) {
  const std::vector<Goal> ladder = {
      {"reach Epic", PotentialGroup::kArmor, ReachesRank(POTENTIAL_RANK_EPIC)},
      {"reach Unique", PotentialGroup::kArmor,
       ReachesRank(POTENTIAL_RANK_UNIQUE)},
      {"reach Legendary", PotentialGroup::kArmor,
       ReachesRank(POTENTIAL_RANK_LEGENDARY)},
  };
  PrintHeader("The rank ladder, which every goal below pays for first");
  for (const Goal& goal : ladder) {
    PrintCost(goal.name, Play(goal, item_level, trials, cap, rng));
  }
}

void PrintMainStat(int item_level, int trials, int cap, std::mt19937& rng) {
  const std::vector<Goal> stats = {
      {"21% main stat (armour)", PotentialGroup::kArmor, AtLeastMainStat(21)},
      {"24% main stat (armour)", PotentialGroup::kArmor, AtLeastMainStat(24)},
      {"27% main stat (armour)", PotentialGroup::kArmor, AtLeastMainStat(27)},
      {"30% main stat (armour)", PotentialGroup::kArmor, AtLeastMainStat(30)},
      {"21% main stat (accessory)", PotentialGroup::kAccessory,
       AtLeastMainStat(21)},
      {"24% main stat (accessory)", PotentialGroup::kAccessory,
       AtLeastMainStat(24)},
  };
  PrintHeader("Main stat, counting %STR and %All Stat together");
  for (const Goal& goal : stats) {
    PrintCost(goal.name, Play(goal, item_level, trials, cap, rng));
  }
}

// The -3s hat needs both cooldown lines on one potential, which is why it's
// printed next to each line alone.
void PrintSingleLines(int item_level, int trials, int cap, std::mt19937& rng) {
  const std::vector<Goal> singles = {
      {"8% critical damage (gloves)", PotentialGroup::kGloves,
       HoldsLines({POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT}, 1)},
      {"-1s cooldown (hat)", PotentialGroup::kHat,
       HoldsLines({POTENTIAL_LINE_TYPE_COOLDOWN_1}, 1)},
      {"-2s cooldown (hat)", PotentialGroup::kHat,
       HoldsLines({POTENTIAL_LINE_TYPE_COOLDOWN_2}, 1)},
      {"-3s cooldown, both lines at once (hat)", PotentialGroup::kHat,
       HoldsBoth(POTENTIAL_LINE_TYPE_COOLDOWN_1,
                 POTENTIAL_LINE_TYPE_COOLDOWN_2)},
  };
  PrintHeader("The one-slot lines");
  for (const Goal& goal : singles) {
    PrintCost(goal.name, Play(goal, item_level, trials, cap, rng));
  }
}

void PrintAccessoryLines(int item_level, int trials, int cap,
                         std::mt19937& rng) {
  const Goal meso = {"20% meso", PotentialGroup::kAccessory,
                     HoldsLines({POTENTIAL_LINE_TYPE_MESO_RATE}, 1)};
  const Goal drop = {"20% item drop", PotentialGroup::kAccessory,
                     HoldsLines({POTENTIAL_LINE_TYPE_ITEM_DROP_RATE}, 1)};
  const Goal both = {"20% meso and 20% drop on one piece",
                     PotentialGroup::kAccessory,
                     HoldsBoth(POTENTIAL_LINE_TYPE_MESO_RATE,
                               POTENTIAL_LINE_TYPE_ITEM_DROP_RATE)};
  PrintHeader("The accessory lines");
  const Cost meso_cost = Play(meso, item_level, trials, cap, rng);
  const Cost drop_cost = Play(drop, item_level, trials, cap, rng);
  const Cost both_cost = Play(both, item_level, trials, cap, rng);
  PrintCost(meso.name, meso_cost);
  PrintCost(drop.name, drop_cost);
  PrintCost(both.name, both_cost);
  // The farming set: eight accessories, one wanted line each. Five meso lines
  // reach the 100% cap and three drop lines give 60%. A piece needing two named
  // lines needs the second to roll as a prime line, so doubling up costs twice
  // as much.
  std::printf(
      "\n  8 pieces, one wanted line each -- 5 meso and 3 drop:"
      "\n    %.0f cubes on average, against %.0f for the same lines"
      " doubled up on 5 pieces.\n",
      8 * meso_cost.mean, 2 * meso_cost.mean + 3 * both_cost.mean);
}

void PrintWeaponLines(int item_level, int trials, int cap, std::mt19937& rng) {
  const std::vector<Goal> weapon = {
      {"2 useful lines (%ATT, boss, IED), any rank", PotentialGroup::kWeaponry,
       HoldsLines(UsefulWeaponLines(), 2)},
      {"3 useful lines, any rank", PotentialGroup::kWeaponry,
       HoldsLines(UsefulWeaponLines(), 3)},
      {"2 useful lines on a Legendary potential", PotentialGroup::kWeaponry,
       LegendaryHolding(UsefulWeaponLines(), 2)},
      {"3 useful lines on a Legendary potential", PotentialGroup::kWeaponry,
       LegendaryHolding(UsefulWeaponLines(), 3)},
  };
  PrintHeader("The weapon, secondary and emblem");
  for (const Goal& goal : weapon) {
    PrintCost(goal.name, Play(goal, item_level, trials, cap, rng));
  }
}

void Run() {
  const int item_level = absl::GetFlag(FLAGS_item_level);
  const int trials = absl::GetFlag(FLAGS_trials);
  const int cap = absl::GetFlag(FLAGS_cap);
  std::mt19937 rng(20260901);

  std::printf("Red cubes on a level %d item, %d trials a goal.\n", item_level,
              trials);
  std::printf("Every run starts from an item with no potential at all.\n");

  PrintRankLadder(item_level, trials, cap, rng);
  PrintMainStat(item_level, trials, cap, rng);
  PrintSingleLines(item_level, trials, cap, rng);
  PrintAccessoryLines(item_level, trials, cap, rng);
  PrintWeaponLines(item_level, trials, cap, rng);
  std::printf("\n");
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
