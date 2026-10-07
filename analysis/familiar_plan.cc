#include "analysis/familiar_plan.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "src/character/familiar.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/familiar.pb.h"

namespace ms {
namespace {

typedef std::map<FamiliarLineType, double> Worths;

// A main's line worths at its rank and the rank below, which its two lines
// roll from.
struct RankWorths {
  Worths at;
  Worths below;
};

// Rolls sampled to value a Legendary familiar, as cube_plan's kCubeSamples.
constexpr int kRollSamples = 64;

// Cubes one look may buy on one familiar, so a rule that never stops can't
// hang a run.
constexpr int kMaxCubesPerLook = 2000;

std::vector<std::string> Mains() {
  std::vector<std::string> mains;
  for (int i = 0; i < kMaxSummonedFamiliars; ++i) {
    mains.push_back(FamiliarRoster()[i].name);
  }
  return mains;
}

bool IsMain(const std::string& name) {
  for (const std::string& main : Mains()) {
    if (main == name) {
      return true;
    }
  }
  return false;
}

Familiar& EntryFor(FamiliarBook& book, const std::string& name) {
  for (Familiar& familiar : *book.mutable_familiars()) {
    if (familiar.name() == name) {
      return familiar;
    }
  }
  Familiar& added = *book.add_familiars();
  added.set_name(name);
  return added;
}

// The character's damage with `book` in place of the account's.
double PowerWith(GameState& state, const FamiliarBook& book,
                 const std::function<double(GameState&)>& power) {
  state.character.set_familiars(book);
  return power(state);
}

bool HoldsDrop(const Familiar& familiar) {
  for (const FamiliarLine& line : familiar.lines()) {
    if (line.type() == FAMILIAR_LINE_TYPE_BOSS_DROP_RATE) {
      return true;
    }
  }
  return false;
}

// Whether a main other than `name` holds the boss drop line, which would make
// a second worth nothing.
bool OtherHoldsDrop(const FamiliarBook& book, const std::string& name) {
  for (const std::string& main : Mains()) {
    const Familiar* familiar = FindFamiliar(book, main);
    if (main != name && familiar != nullptr && HoldsDrop(*familiar)) {
      return true;
    }
  }
  return false;
}

// What each line of `rank` adds on `name` alone, over the same familiar with
// no lines, with the other mains as they are.
Worths LineWorths(GameState& state, const FamiliarBook& book,
                  const std::string& name, PotentialRank rank,
                  const std::function<double(GameState&)>& power,
                  const FamiliarPrices& prices) {
  Worths worths;
  if (rank == POTENTIAL_RANK_UNSPECIFIED) {
    return worths;
  }
  FamiliarBook probe = book;
  Familiar& familiar = EntryFor(probe, name);
  familiar.set_level(std::max(familiar.level(), 1));
  familiar.clear_lines();
  const double bare = PowerWith(state, probe, power);
  const bool other_drop = OtherHoldsDrop(book, name);
  for (FamiliarLineType type : FamiliarPool(rank)) {
    if (type == FAMILIAR_LINE_TYPE_BOSS_DROP_RATE) {
      worths[type] = other_drop ? 0.0 : prices.drop_line_power;
      continue;
    }
    familiar.clear_lines();
    FamiliarLine& line = *familiar.add_lines();
    line.set_type(type);
    line.set_rank(rank);
    worths[type] = PowerWith(state, probe, power) - bare;
  }
  return worths;
}

double MeanOf(const Worths& worths) {
  if (worths.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (const std::pair<const FamiliarLineType, double>& entry : worths) {
    sum += entry.second;
  }
  return sum / static_cast<double>(worths.size());
}

RankWorths WorthsAt(GameState& state, const FamiliarBook& book,
                    const std::string& name, int level,
                    const std::function<double(GameState&)>& power,
                    const FamiliarPrices& prices) {
  RankWorths worths;
  worths.at = LineWorths(state, book, name, FamiliarRank(level), power, prices);
  if (level > 1) {
    worths.below =
        LineWorths(state, book, name, FamiliarRank(level - 1), power, prices);
  } else {
    worths.below = worths.at;
  }
  return worths;
}

// A fresh roll's mean worth, under RollFamiliarLines's odds.
double MeanRoll(const RankWorths& worths, int level) {
  double second = MeanOf(worths.at);
  if (level > 1) {
    second = kFamiliarPrimeChance * MeanOf(worths.at) +
             (1.0 - kFamiliarPrimeChance) * MeanOf(worths.below);
  }
  return MeanOf(worths.at) + second;
}

double CubePrice(const FamiliarPrices& prices) {
  return prices.power_per_meso * static_cast<double>(kFamiliarCubeMeso);
}

// Fresh Legendary rolls on `name`, kRollSamples of them, each measured with
// both lines in place over the familiar with none. Measured together, as the
// held lines are, so two boss lines past the cap count once on both sides of
// the rule.
std::vector<double> SampledRolls(GameState& state, const FamiliarBook& book,
                                 const std::string& name,
                                 const std::function<double(GameState&)>& power,
                                 const FamiliarPrices& prices) {
  FamiliarBook probe = book;
  Familiar& familiar = EntryFor(probe, name);
  familiar.set_level(kFamiliarMaxLevel);
  familiar.clear_lines();
  const double without = PowerWith(state, probe, power);
  const bool other_drop = OtherHoldsDrop(book, name);
  std::vector<double> worths;
  for (int i = 0; i < kRollSamples; ++i) {
    familiar.clear_lines();
    for (FamiliarLine& line :
         RollFamiliarLines(POTENTIAL_RANK_LEGENDARY, state.rng)) {
      *familiar.add_lines() = std::move(line);
    }
    double worth = PowerWith(state, probe, power) - without;
    if (!other_drop && HoldsDrop(familiar)) {
      worth += prices.drop_line_power;
    }
    worths.push_back(worth);
  }
  return worths;
}

double CubeReserve(const std::vector<double>& rolls,
                   const FamiliarPrices& prices) {
  const std::vector<double> odds(rolls.size(),
                                 1.0 / static_cast<double>(rolls.size()));
  return FamiliarReserve(rolls, odds, CubePrice(prices));
}

// What reaching `level` on `name` is worth. Below Legendary the next level
// rerolls, so it is the mean roll; at Legendary the cubes follow, so it is a
// roll kept or cubed up to the reservation value, E[max(X, r)].
double ReachedWorth(GameState& state, const FamiliarBook& book,
                    const std::string& name, int level,
                    const std::function<double(GameState&)>& power,
                    const FamiliarPrices& prices) {
  if (level < kFamiliarMaxLevel) {
    return MeanRoll(WorthsAt(state, book, name, level, power, prices), level);
  }
  const std::vector<double> rolls =
      SampledRolls(state, book, name, power, prices);
  const double reserve = CubeReserve(rolls, prices);
  double expected = 0.0;
  for (double roll : rolls) {
    expected += std::max(roll, reserve);
  }
  return expected / static_cast<double>(rolls.size());
}

double WorthOf(const RankWorths& worths, const Familiar& familiar) {
  const PotentialRank top = FamiliarRank(familiar.level());
  double sum = 0.0;
  for (const FamiliarLine& line : familiar.lines()) {
    const Worths& table = line.rank() == top ? worths.at : worths.below;
    std::map<FamiliarLineType, double>::const_iterator it =
        table.find(line.type());
    if (it != table.end()) {
      sum += it->second;
    }
  }
  return sum;
}

// The cheapest steps on familiars other than the mains that lift Familiar
// Bond one level, in order, and their EXP. Empty when no level is left.
std::vector<std::string> BondSteps(const FamiliarBook& book, int64_t* cost) {
  *cost = 0;
  std::vector<std::string> steps;
  std::map<std::string, int> levels;
  for (const FamiliarSpecies& species : FamiliarRoster()) {
    levels[species.name] = FamiliarLevel(book, species.name);
  }
  int total = TotalFamiliarLevels(book);
  const int now = FamiliarSkillLevel(total);
  while (FamiliarSkillLevel(total) == now) {
    std::string cheapest;
    int64_t best = std::numeric_limits<int64_t>::max();
    for (const FamiliarSpecies& species : FamiliarRoster()) {
      const int level = levels[species.name];
      if (IsMain(species.name) || level >= kFamiliarMaxLevel) {
        continue;
      }
      if (FamiliarLevelCost(level + 1) < best) {
        best = FamiliarLevelCost(level + 1);
        cheapest = species.name;
      }
    }
    if (cheapest.empty()) {
      return {};
    }
    steps.push_back(cheapest);
    *cost += best;
    ++levels[cheapest];
    ++total;
  }
  return steps;
}

// Summons the mains in every preset. A main never levelled can't be summoned
// for real, so a probe book lends each a level while they are put out.
void SummonMains(GameState& state) {
  FamiliarBook probe = state.account.familiars();
  for (const std::string& main : Mains()) {
    Familiar& familiar = EntryFor(probe, main);
    familiar.set_level(std::max(familiar.level(), 1));
  }
  state.character.set_familiars(probe);
  for (int slot = 0; slot < kNumStatPresets; ++slot) {
    for (const std::string& main : Mains()) {
      state.character.SummonFamiliar(main, StatPresetAt(slot));
    }
  }
}

// Spends the pool a step at a time on the better of a main's next level and
// Familiar Bond's, per EXP.
int SpendExp(GameState& state, const std::function<double(GameState&)>& power,
             const FamiliarPrices& prices) {
  int levels = 0;
  for (;;) {
    const FamiliarBook& book = state.account.familiars();
    std::string main;
    int main_level = kFamiliarMaxLevel;
    for (const std::string& name : Mains()) {
      if (FamiliarLevel(book, name) < main_level) {
        main_level = FamiliarLevel(book, name);
        main = name;
      }
    }
    double main_rate = 0.0;
    int64_t main_cost = 0;
    if (!main.empty()) {
      main_cost = FamiliarLevelCost(main_level + 1);
      double held = 0.0;
      const Familiar* familiar = FindFamiliar(book, main);
      if (familiar != nullptr && main_level > 0) {
        held = WorthOf(WorthsAt(state, book, main, main_level, power, prices),
                       *familiar);
      }
      // A step is priced as the first of the best run it starts, the way a
      // star is: Rare and Epic lines are worth little, but they lead to
      // Unique and Legendary.
      int64_t run_cost = 0;
      for (int level = main_level + 1; level <= kFamiliarMaxLevel; ++level) {
        run_cost += FamiliarLevelCost(level);
        main_rate = std::max(
            main_rate,
            (ReachedWorth(state, book, main, level, power, prices) - held) /
                static_cast<double>(run_cost));
      }
    }
    int64_t bond_cost = 0;
    const std::vector<std::string> steps = BondSteps(book, &bond_cost);
    double bond_rate = 0.0;
    if (!steps.empty()) {
      FamiliarBook lifted = book;
      for (const std::string& name : steps) {
        Familiar& familiar = EntryFor(lifted, name);
        familiar.set_level(familiar.level() + 1);
      }
      bond_rate =
          (PowerWith(state, lifted, power) - PowerWith(state, book, power)) /
          static_cast<double>(bond_cost);
    }
    if (main_rate <= 0.0 && bond_rate <= 0.0) {
      break;
    }
    if (main_rate >= bond_rate) {
      if (book.exp() < main_cost || !LevelUpFamiliar(state, main)) {
        break;
      }
      ++levels;
      continue;
    }
    if (book.exp() < bond_cost) {
      break;
    }
    for (const std::string& name : steps) {
      LevelUpFamiliar(state, name);
      ++levels;
    }
  }
  state.MirrorAccount();
  return levels;
}

// Cubes each Legendary main under the stopping rule.
int SpendCubes(GameState& state, const std::function<double(GameState&)>& power,
               const FamiliarPrices& prices) {
  int cubes = 0;
  for (const std::string& main : Mains()) {
    if (FamiliarLevel(state.account.familiars(), main) != kFamiliarMaxLevel) {
      continue;
    }
    const double reserve = CubeReserve(
        SampledRolls(state, state.account.familiars(), main, power, prices),
        prices);
    // The held lines are measured together rather than summed, so two boss
    // lines past the cap count once.
    FamiliarBook bare = state.account.familiars();
    EntryFor(bare, main).clear_lines();
    const double without = PowerWith(state, bare, power);
    for (int rolled = 0; rolled < kMaxCubesPerLook; ++rolled) {
      const FamiliarBook& book = state.account.familiars();
      double held = PowerWith(state, book, power) - without;
      if (!OtherHoldsDrop(book, main) && HoldsDrop(*FindFamiliar(book, main))) {
        held += prices.drop_line_power;
      }
      if (held >= reserve || !CubeFamiliar(state, main)) {
        break;
      }
      ++cubes;
    }
  }
  state.MirrorAccount();
  return cubes;
}

}  // namespace

double FamiliarReserve(const std::vector<double>& values,
                       const std::vector<double>& odds, double price) {
  if (values.empty()) {
    return 0.0;
  }
  double low = *std::min_element(values.begin(), values.end());
  double high = *std::max_element(values.begin(), values.end());
  if (price <= 0.0) {
    return high;
  }
  // E[max(X, r)] - price - r falls as r rises, so bisect for its root.
  for (int i = 0; i < 100; ++i) {
    const double r = (low + high) / 2.0;
    double expected = 0.0;
    for (std::size_t j = 0; j < values.size(); ++j) {
      expected += odds[j] * std::max(values[j], r);
    }
    if (expected - price - r > 0.0) {
      low = r;
    } else {
      high = r;
    }
  }
  return low;
}

FamiliarSpend SpendFamiliars(GameState& state,
                             const std::function<double(GameState&)>& power,
                             const FamiliarPrices& prices) {
  FamiliarSpend spend;
  if (state.character.account_max_level() < kFamiliarsLevel) {
    return spend;
  }
  SummonMains(state);
  state.MirrorAccount();
  spend.levels = SpendExp(state, power, prices);
  const int64_t before = state.character.meso();
  spend.cubes = SpendCubes(state, power, prices);
  spend.meso = before - state.character.meso();
  return spend;
}

}  // namespace ms
