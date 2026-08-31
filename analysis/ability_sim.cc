/* What an Inner Ability goal costs in honor.
 *
 * Two questions, and the sim answers both. The first is exact arithmetic: a
 * rank-up is a coin with a known face, so the honor it takes to climb the
 * ladder is the price of a reset over the chance one pays off. The second is
 * not, because the lines are rolled without replacement and what a reset costs
 * depends on what is being held through it -- so the goal is played out.
 *
 * Which lines to hold is the whole of the strategy, and the order matters more
 * than it looks: a reset holding two lines costs twice one holding none, so
 * the line left to the expensive end should be the likeliest of the three.
 * Both orders are played and printed side by side.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:ability_sim
 *   bazelisk run //analysis:ability_sim -- --goal=boss_damage:unique
 *   bazelisk run //analysis:ability_sim -- --trials=100000
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/character/inner_ability.h"
#include "src/protos/character.pb.h"

ABSL_FLAG(std::string, goal,
          "attack_speed:legendary,boss_damage:unique,buff_duration:unique",
          "Lines to roll for, as type:rank, comma separated. A line counts as "
          "met at the named rank or above.");
ABSL_FLAG(int, trials, 2000, "Runs to average over.");
ABSL_FLAG(int64_t, reset_cap, 200000,
          "Resets one run may take before it is abandoned.");

namespace ms {
namespace {

// One line the goal asks for.
struct Want {
  AbilityLineType type;
  AbilityRank rank;
};

// The enum name with its prefix taken off and lowered, which is how the flag
// spells a type: ABILITY_LINE_TYPE_ATTACK_SPEED reads as attack_speed.
std::string ShortName(AbilityLineType type) {
  return absl::AsciiStrToLower(AbilityLineType_Name(type).substr(
      std::string("ABILITY_LINE_TYPE_").size()));
}

std::string ShortName(AbilityRank rank) {
  return absl::AsciiStrToLower(
      AbilityRank_Name(rank).substr(std::string("ABILITY_RANK_").size()));
}

std::vector<Want> ParseGoal(const std::string& spec) {
  std::vector<Want> goal;
  for (absl::string_view part : absl::StrSplit(spec, ',')) {
    const std::pair<std::string, std::string> named =
        absl::StrSplit(part, absl::MaxSplits(':', 1));
    Want want = {ABILITY_LINE_TYPE_UNSPECIFIED, ABILITY_RANK_UNSPECIFIED};
    for (int i = ABILITY_LINE_TYPE_STR; i < AbilityLineType_ARRAYSIZE; ++i) {
      const auto type = static_cast<AbilityLineType>(i);
      if (ShortName(type) == named.first) {
        want.type = type;
      }
    }
    for (int i = ABILITY_RANK_RARE; i <= ABILITY_RANK_LEGENDARY; ++i) {
      const auto rank = static_cast<AbilityRank>(i);
      if (ShortName(rank) == named.second) {
        want.rank = rank;
      }
    }
    if (want.type == ABILITY_LINE_TYPE_UNSPECIFIED ||
        want.rank == ABILITY_RANK_UNSPECIFIED) {
      LOG(FATAL) << "cannot read goal line '" << std::string(part) << "'";
    }
    goal.push_back(want);
  }
  return goal;
}

bool Meets(const AbilityLine& line, const Want& want) {
  return line.type() == want.type && line.rank() >= want.rank;
}

bool Met(const AbilityPreset& preset, const Want& want) {
  for (const AbilityLine& line : preset.lines()) {
    if (Meets(line, want)) {
      return true;
    }
  }
  return false;
}

bool GoalMet(const AbilityPreset& preset, const std::vector<Want>& goal) {
  for (const Want& want : goal) {
    if (!Met(preset, want)) {
      return false;
    }
  }
  return true;
}

// Which of the two ways of playing the goal a run follows. Both hold every
// line they are allowed to, up to the two a reset takes; they disagree about
// whether the top line is one of them.
enum class Style {
  // Hold whatever the goal asked for, the top line included. The last line
  // still being chased is then one of the two below it, which are the
  // unlikeliest to land.
  kHoldAny,
  // Never hold the top line, so it is the one still being rolled once both
  // lines below it are held. Costlier per reset at the end, but the top line
  // is the likeliest of the three to come up.
  kHoldLower,
};

// Whether holding the top line would shut the goal out. Only the top line ever
// carries the ability's own rank, so a line the goal wants at that rank has
// nowhere else to go -- and a held top line at that rank is never rolled
// again. Holding anything else up there would strand it.
bool TopSlotIsSpokenFor(const AbilityPreset& preset,
                        const std::vector<Want>& goal) {
  for (const Want& want : goal) {
    if (want.rank == preset.rank() && !Met(preset, want)) {
      return true;
    }
  }
  return false;
}

// Whether the line in `slot` is worth holding through the next reset.
bool WorthHolding(const AbilityPreset& preset, int slot,
                  const std::vector<Want>& goal, Style style) {
  const AbilityLine& line = preset.lines(slot);
  if (slot == 0) {
    if (style == Style::kHoldLower) {
      return false;
    }
    // The one line the goal wants at the ability's rank may hold the top slot.
    // Nothing else may, or it would sit there unrolled forever.
    if (TopSlotIsSpokenFor(preset, goal)) {
      for (const Want& want : goal) {
        if (want.rank == preset.rank() && Meets(line, want)) {
          return true;
        }
      }
      return false;
    }
  }
  for (const Want& want : goal) {
    if (Meets(line, want)) {
      return true;
    }
  }
  return false;
}

// Holds every line worth holding, up to the two a reset allows.
void HoldWantedLines(AbilityPreset& preset, const std::vector<Want>& goal,
                     Style style) {
  for (int i = 0; i < preset.lines_size(); ++i) {
    SetAbilityLineLocked(preset, i, false);
  }
  for (int i = 0; i < preset.lines_size(); ++i) {
    if (WorthHolding(preset, i, goal, style)) {
      SetAbilityLineLocked(preset, i, true);
    }
  }
}

// Honor one run spends reaching `goal`, or -1 for a run that gave up.
int64_t RunToGoal(const std::vector<Want>& goal, Style style, int64_t cap,
                  std::mt19937& rng, int64_t& resets) {
  AbilityPreset preset = DefaultAbilityPreset();
  int64_t spent = 0;
  for (resets = 0; resets < cap; ++resets) {
    HoldWantedLines(preset, goal, style);
    spent += AbilityResetCost(preset.rank(), LockedAbilityLines(preset));
    RerollAbility(preset, rng);
    if (GoalMet(preset, goal)) {
      ++resets;
      return spent;
    }
  }
  return -1;
}

struct Summary {
  double mean_honor = 0.0;
  int64_t median = 0;
  int64_t p90 = 0;
  double mean_resets = 0.0;
  int abandoned = 0;
};

Summary Play(const std::vector<Want>& goal, Style style, int trials,
             int64_t cap, std::mt19937& rng) {
  std::vector<int64_t> spent;
  spent.reserve(trials);
  Summary summary;
  double total = 0.0;
  double total_resets = 0.0;
  for (int i = 0; i < trials; ++i) {
    int64_t resets = 0;
    const int64_t honor = RunToGoal(goal, style, cap, rng, resets);
    if (honor < 0) {
      ++summary.abandoned;
      continue;
    }
    spent.push_back(honor);
    total += static_cast<double>(honor);
    total_resets += static_cast<double>(resets);
  }
  if (spent.empty()) {
    return summary;
  }
  std::sort(spent.begin(), spent.end());
  summary.mean_honor = total / spent.size();
  summary.mean_resets = total_resets / spent.size();
  summary.median = spent[spent.size() / 2];
  summary.p90 = spent[spent.size() * 9 / 10];
  return summary;
}

// The ladder is a coin per reset, so what it takes to climb is arithmetic
// rather than something to play out.
void PrintRankLadder() {
  printf("Climbing the ranks, with nothing held\n\n");
  printf("  %-10s %8s %8s %10s %14s %14s\n", "From", "Cost", "Chance", "Resets",
         "Honor", "Cumulative");
  int64_t cumulative = 0;
  for (int i = ABILITY_RANK_RARE; i < ABILITY_RANK_LEGENDARY; ++i) {
    const auto rank = static_cast<AbilityRank>(i);
    const double chance = AbilityRankUpChance(rank);
    const int64_t cost = AbilityResetCost(rank, 0);
    const double resets = 1.0 / chance;
    const auto honor = static_cast<int64_t>(cost * resets);
    cumulative += honor;
    printf("  %-10s %8lld %7.0f%% %10.0f %14lld %14lld\n",
           ShortName(rank).c_str(), static_cast<long long>(cost),
           chance * 100.0, resets, static_cast<long long>(honor),
           static_cast<long long>(cumulative));
  }
  printf("\n");
}

void PrintGoal(const std::vector<Want>& goal, int trials, int64_t cap,
               std::mt19937& rng) {
  printf("Rolling for");
  for (const Want& want : goal) {
    printf(" %s(%s)", ShortName(want.type).c_str(),
           ShortName(want.rank).c_str());
  }
  printf(", from Rare, over %d runs\n\n", trials);
  printf("  %-14s %14s %14s %14s %10s\n", "Holding", "Mean honor", "Median",
         "9 in 10 by", "Resets");
  for (const std::pair<Style, const char*> style :
       {std::make_pair(Style::kHoldAny, "any line"),
        std::make_pair(Style::kHoldLower, "lower only")}) {
    const Summary summary = Play(goal, style.first, trials, cap, rng);
    printf("  %-14s %14.0f %14lld %14lld %10.0f", style.second,
           summary.mean_honor, static_cast<long long>(summary.median),
           static_cast<long long>(summary.p90), summary.mean_resets);
    if (summary.abandoned > 0) {
      printf("  (%d runs gave up)", summary.abandoned);
    }
    printf("\n");
  }
  printf("\n");
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  std::mt19937 rng(12345);
  ms::PrintRankLadder();
  ms::PrintGoal(ms::ParseGoal(absl::GetFlag(FLAGS_goal)),
                absl::GetFlag(FLAGS_trials), absl::GetFlag(FLAGS_reset_cap),
                rng);
  return 0;
}
