/* endgame_sim: GMS's ceiling for every branch, side by side. Each row is the
 * --mode=endgame character (level 300, Eternal and Destiny, both boss
 * accessory sets, every account system full) with its Combat Power both ways,
 * its clear time on the hardest fight built, and its damage a second against
 * an unkillable copy of that fight's toughest mob.
 *
 *   bazelisk run -c opt //analysis:endgame_sim
 *   bazelisk run -c opt //analysis:endgame_sim -- --job=bow_master --gear
 *
 * GMS's Combat Power counts no skill, so across jobs it mostly says which
 * stat and weapon a job uses. The dummy's damage a second is the comparison
 * between jobs.
 */
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/hexa_stat.h"
#include "src/character/stat_preset.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

ABSL_FLAG(std::string, job, "", "One branch (\"bow_master\"); empty for all.");
ABSL_FLAG(int, trials, 3, "Fights per branch, each on its own seed.");
ABSL_FLAG(bool, gear, false, "Also list each branch's gear and allocations.");
ABSL_FLAG(double, seconds, 600.0,
          "How long the dummy is struck for the damage a second.");

namespace ms {
namespace {

struct Row {
  Job job = JOB_UNSPECIFIED;
  int64_t gms_cp = 0;
  int64_t our_cp = 0;
  std::string fight;
  int wins = 0;
  double seconds = 0.0;
  double clear_seconds = 0.0;
  double dps = 0.0;
  std::string gear;
};

constexpr char kDummyMob[] = "__endgame_dummy";

// Damage a second against an unkillable copy of the fight's toughest mob, at
// its level and defence, for --seconds: the fight's own clock is mostly phase
// changes once it dies inside a minute.
double DummyRate(GameState& state, const BossDifficulty& difficulty) {
  Mob dummy;
  for (const BossPhase& phase : difficulty.phases()) {
    for (const Spawn& spawn : phase.spawns()) {
      const Mob& mob = state.mobs.at(spawn.mob());
      if (mob.pdr() >= dummy.pdr()) {
        dummy = mob;
      }
    }
  }
  dummy.set_name("Dummy");
  dummy.set_max_hp(kMeasuredMobHp);
  dummy.set_boss(true);
  state.mobs[kDummyMob] = dummy;
  BossDifficulty fight;
  fight.set_name("Dummy");
  Spawn* spawn = fight.add_phases()->add_spawns();
  spawn->set_mob(kDummyMob);
  spawn->set_count(1);
  const CombatParams params = ComputeBossParams(state, "__dummy", fight, 0);
  if (!params.active) {
    return 0.0;
  }
  const Sequence played = MeasureFight(params, absl::GetFlag(FLAGS_seconds));
  return played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
}

GameState NewEndgameState(const Catalogs& catalogs, Job branch,
                          unsigned int seed) {
  TestOptions options;
  options.job = AdvancementForJobStage(branch, StageOf(branch));
  options.autoswap_presets = true;
  options.playable_roster = false;
  return GameState(catalogs.equips, catalogs.scrolls, catalogs.items,
                   catalogs.mobs, catalogs.maps, catalogs.skills,
                   GameMode::kEndgame, options, seed, catalogs.sets,
                   catalogs.bosses);
}

std::string HexaName(HexaStat stat) {
  switch (stat) {
    case HEXA_STAT_CRIT_DAMAGE:
      return "CritDmg";
    case HEXA_STAT_BOSS_DAMAGE:
      return "Boss";
    case HEXA_STAT_IGNORE_DEFENSE:
      return "IED";
    case HEXA_STAT_DAMAGE:
      return "Dmg";
    case HEXA_STAT_ATTACK:
      return "ATT";
    case HEXA_STAT_MAIN_STAT:
      return "Stat";
    default:
      return "-";
  }
}

// What the character wears and chose, for --gear.
std::string Describe(const GameState& state) {
  const CharacterInstance& c = state.character;
  const StatPreset boss = AutoswapSlotFor(Activity::kBossing);
  std::string out;
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       c.equipped(boss)) {
    const EquipInstance& item = *worn.second;
    out += "    " + FormatSlot(worn.first) + ": " + item.prototype().name() +
           " " + std::to_string(item.equip_state().stars()) + "*";
    for (const PotentialLine& line : item.potential().lines()) {
      out += " [" + PotentialLineName(line.type()) + "]";
    }
    out += "\n";
  }
  for (const HexaStatNode& node : c.proto().hexa_stats().nodes()) {
    out += "    HEXA " + HexaName(node.primary()) + " " +
           std::to_string(node.primary_level()) + " / " +
           HexaName(node.first()) + " " + std::to_string(node.first_level()) +
           " / " + HexaName(node.second()) + " " +
           std::to_string(node.second_level()) + "\n";
  }
  out += "    Artifact:";
  for (const ArtifactCrystal& crystal : c.legion().artifact().crystals()) {
    out += " (";
    for (int stat : crystal.stats()) {
      out +=
          ArtifactStat_Name(static_cast<ArtifactStat>(stat)).substr(14) + " ";
    }
    out += ")";
  }
  out += "\n    Inner Ability:";
  for (const AbilityLine& line : c.ability(boss).lines()) {
    out += " " + AbilityLineType_Name(line.type()).substr(18);
  }
  out += "\n";
  return out;
}

Row Measure(const Catalogs& catalogs, Job branch, int trials, bool gear) {
  Row row;
  row.job = branch;
  GameState state = NewEndgameState(catalogs, branch, 1);
  row.gms_cp = GmsCharacterCombatPower(state.character, state.skills,
                                       Activity::kBossing);
  row.our_cp =
      CharacterCombatPower(state.character, state.skills, Activity::kBossing);
  if (gear) {
    row.gear = Describe(state);
  }
  std::pair<std::string, int> fight;
  if (!AimedFight(state, &fight)) {
    return row;
  }
  const BossDifficulty& difficulty =
      state.bosses.at(fight.first).difficulties(fight.second);
  row.fight = difficulty.name() + " " + state.bosses.at(fight.first).name();
  row.dps = DummyRate(state, difficulty);
  for (int trial = 0; trial < trials; ++trial) {
    GameState run = NewEndgameState(catalogs, branch, 1 + trial);
    const BossOutcome outcome = FightBoss(run, fight.first, fight.second);
    row.wins += outcome.won ? 1 : 0;
    row.clear_seconds += outcome.seconds / trials;
  }
  return row;
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  using namespace ms;
  const Catalogs catalogs = LoadCatalogs();
  std::vector<Job> branches;
  const std::string only = absl::GetFlag(FLAGS_job);
  if (!only.empty()) {
    branches.push_back(ParseBranch(only, 4));
  } else {
    for (Job branch : EveryBranch()) {
      if (StageOf(branch) == 4) {
        branches.push_back(branch);
      }
    }
  }
  std::vector<Row> rows(branches.size());
  ParallelFor(static_cast<int>(branches.size()), [&](int i) {
    rows[i] = Measure(catalogs, branches[i], absl::GetFlag(FLAGS_trials),
                      absl::GetFlag(FLAGS_gear));
  });
  double best = 0.0;
  for (const Row& row : rows) {
    best = std::max(best, row.dps);
  }
  std::printf("%-26s %14s %14s  %-22s %5s %8s %16s %6s\n", "Branch", "GMS CP",
              "Our CP", "Fight", "Wins", "Clear s", "Dummy dmg/s", "vs top");
  for (const Row& row : rows) {
    std::printf("%-26s %14s %14s  %-22s %2d/%-2d %8.1f %16s %6.3f\n",
                BranchName(row.job).c_str(),
                FormatWithCommas(row.gms_cp).c_str(),
                FormatWithCommas(row.our_cp).c_str(), row.fight.c_str(),
                row.wins, absl::GetFlag(FLAGS_trials), row.clear_seconds,
                FormatWithCommas(static_cast<int64_t>(row.dps)).c_str(),
                best > 0 ? row.dps / best : 0.0);
    if (!row.gear.empty()) {
      std::printf("%s", row.gear.c_str());
    }
  }
  return 0;
}
