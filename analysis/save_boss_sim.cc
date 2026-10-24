/* save_boss_sim: every fight open to a real save's played character, fought
 * solo, with what each first solo clear would pay in Noblesse SP.
 *
 *   bazelisk run //analysis:save_boss_sim -- --save=/path/to/copy/ms.save
 *
 * The save is only read. Each fight starts from a fresh load, so one fight's
 * rewards never carry into the next.
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/gear_plan.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_world.h"
#include "src/character/guild.h"
#include "src/character/noblesse_plan.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/account.pb.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/save.h"

ABSL_FLAG(std::string, save, "", "The save to read. Use a copy.");
ABSL_FLAG(int, trials, 3, "Attempts at each fight, each on its own seed.");
ABSL_FLAG(bool, gear, false, "Also list what the boss preset wears.");
ABSL_FLAG(std::string, sweep_to, "",
          "If set, first records a solo clear of every fight the character "
          "wins, spends the Noblesse SP they pay, writes the result here and "
          "fights from it. Runs before --spend_to.");
ABSL_FLAG(std::string, spend_to, "",
          "If set, first spends the character's meso as progression_sim's "
          "shopper would, writes the result here and fights from it.");

namespace ms {
namespace {

// Reads `path` into `state`, a fresh NewState.
bool Load(const Catalogs& catalogs, const std::string& path, GameState& state) {
  state.bosses = catalogs.bosses;
  const LoadResult load = LoadGameFromFile(state, path);
  if (load.status != LoadStatus::kLoaded) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), load.message.c_str());
    return false;
  }
  return true;
}

bool SoloCleared(const GameState& state, const std::string& boss,
                 const std::string& difficulty) {
  for (const SoloClear& clear : state.account.solo_clears()) {
    if (clear.boss() == boss && clear.difficulty() == difficulty) {
      return true;
    }
  }
  return false;
}

// The Noblesse SP a first solo clear of `difficulty` would add.
int SpFor(const GameState& state, const std::string& boss,
          const std::string& difficulty) {
  google::protobuf::RepeatedPtrField<SoloClear> clears =
      state.account.solo_clears();
  const int before = NoblesseSpEarned(clears, state.bosses);
  SoloClear* clear = clears.Add();
  clear->set_boss(boss);
  clear->set_difficulty(difficulty);
  return NoblesseSpEarned(clears, state.bosses) - before;
}

// Solos every fight `in`'s character wins, banks the Noblesse SP and spends it
// on what raises the boss damage the shopper aims at, then writes `out`.
bool Sweep(const Catalogs& catalogs, const std::string& in,
           const std::string& out) {
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, in, state)) {
    return false;
  }
  for (const std::pair<std::string, int>& fight :
       UnlockedBosses(state, state.character.proto().level())) {
    GameState trial = NewState(catalogs, 1);
    if (!Load(catalogs, in, trial)) {
      return false;
    }
    if (FightBoss(trial, fight.first, fight.second).won) {
      state.account.RecordSoloClear(
          fight.first,
          state.bosses.at(fight.first).difficulties(fight.second).name());
    }
  }
  state.character.set_noblesse_sp_earned(
      NoblesseSpEarned(state.account.solo_clears(), state.bosses));
  GearShopper shopper{GearPlan()};
  const double before = shopper.Power(state);
  const int levels =
      SpendNoblesseSp(state.character, state.skills,
                      [&](CharacterInstance&) { return shopper.Power(state); });
  std::printf(
      "Swept: %d Noblesse SP earned, %d levels bought; boss power "
      "x%.3f.\n",
      state.character.noblesse_sp_earned(), levels,
      shopper.Power(state) / before);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.guild() == GUILD_SKILL_NOBLESSE) {
      std::printf("  %s %d\n", entry.second.name().c_str(),
                  state.character.ListedSkillLevel(entry.second));
    }
  }
  return SaveGameToFile(state, out);
}

// Spends the save's meso on gear and writes the result to `out`.
bool Spend(const Catalogs& catalogs, const std::string& in,
           const std::string& out) {
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, in, state)) {
    return false;
  }
  const int64_t before = state.character.meso();
  GearShopper shopper{GearPlan()};
  const GearSpend spend = shopper.Spend(state);
  std::printf(
      "Spent %s of %s: stars %s (%d stars, %d booms), cubes %s (%d kept of "
      "%d), flames %s, scrolls %s, symbols %s (%d levels), copies %s.\n",
      FormatWithCommas(spend.meso()).c_str(), FormatWithCommas(before).c_str(),
      FormatWithCommas(spend.stars).c_str(), spend.stars_gained, spend.booms,
      FormatWithCommas(spend.cubes).c_str(), spend.cubes_kept,
      spend.cubes_bought, FormatWithCommas(spend.flames).c_str(),
      FormatWithCommas(spend.scrolls).c_str(),
      FormatWithCommas(spend.symbols).c_str(), spend.symbol_levels,
      FormatWithCommas(spend.copies + spend.replacements).c_str());
  return SaveGameToFile(state, out);
}

// What the boss preset wears: stars, unscrolled slots and potential ranks.
void PrintBossGear(const GameState& state) {
  std::printf("%-12s %-32s %5s %5s %-10s %-10s\n", "Slot", "Item", "Stars",
              "Slots", "Potential", "Bonus");
  for (int i = EquipSlot_MIN; i <= EquipSlot_MAX; ++i) {
    if (!EquipSlot_IsValid(i)) {
      continue;
    }
    const EquipSlot slot = static_cast<EquipSlot>(i);
    const EquipInstance* item =
        state.character.WornAt(StatPreset::kSecond, slot);
    if (item == nullptr) {
      continue;
    }
    const Equip& e = item->equip_state();
    std::printf("%-12s %-32s %5d %5d %-10s %-10s\n", FormatSlot(slot).c_str(),
                item->name().c_str(), e.stars(), e.remaining_upgrade_slots(),
                PotentialRank_Name(e.main_potential().rank()).c_str() + 15,
                PotentialRank_Name(e.bonus_potential().rank()).c_str() + 15);
  }
  std::printf("\n");
}

int Run() {
  std::string path = absl::GetFlag(FLAGS_save);
  const int trials = absl::GetFlag(FLAGS_trials);
  const Catalogs catalogs = LoadCatalogs();
  const std::string swept = absl::GetFlag(FLAGS_sweep_to);
  if (!swept.empty()) {
    if (!Sweep(catalogs, path, swept)) {
      return 1;
    }
    path = swept;
  }
  const std::string spent = absl::GetFlag(FLAGS_spend_to);
  if (!spent.empty()) {
    if (!Spend(catalogs, path, spent)) {
      return 1;
    }
    path = spent;
  }
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, path, state)) {
    return 1;
  }
  if (absl::GetFlag(FLAGS_gear)) {
    PrintBossGear(state);
  }
  const int level = state.character.proto().level();
  std::printf("%s, level %d. Noblesse SP earned %d.\n\n",
              state.character.proto().name().c_str(), level,
              NoblesseSpEarned(state.account.solo_clears(), state.bosses));
  std::printf("%-24s %-8s %5s %5s %4s %6s %8s %9s\n", "Boss", "Diff", "Unlck",
              "Solo", "+SP", "Wins", "Avg s", "Left@clk");
  for (const std::pair<std::string, int>& fight :
       UnlockedBosses(state, level)) {
    const Boss& boss = state.bosses.at(fight.first);
    const BossDifficulty& difficulty = boss.difficulties(fight.second);
    int wins = 0;
    double seconds = 0.0;
    double worst_left = 0.0;
    for (int t = 0; t < trials; ++t) {
      GameState trial = NewState(catalogs, 1000 + t);
      if (!Load(catalogs, path, trial)) {
        return 1;
      }
      const BossOutcome outcome = FightBoss(trial, fight.first, fight.second);
      wins += outcome.won ? 1 : 0;
      seconds += outcome.seconds;
      worst_left = std::max(worst_left, outcome.left_at_clock);
    }
    std::printf(
        "%-24s %-8s %5d %5s %4d %3d/%-2d %8.0f %8.1f%%\n", fight.first.c_str(),
        difficulty.name().c_str(), difficulty.unlock_level(),
        SoloCleared(state, fight.first, difficulty.name()) ? "yes" : "-",
        SpFor(state, fight.first, difficulty.name()), wins, trials,
        seconds / trials, 100.0 * worst_left);
  }
  return 0;
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  return ms::Run();
}
