/* Whether a boss can be killed inside its clock, and by whom.
 *
 * Every branch that could stand at the level is grown the way a player gets
 * there -- every AP on its stat, every SP on its books -- geared in the best
 * of every slot the game holds, and then put in front of the real fight: the
 * shipped boss data, stepped by the same BossRun the screen steps, at the same
 * sixty frames a second. What comes back is the time the kill took, against
 * the time the fight allows.
 *
 * The clock is raised while measuring, so a build that cannot make the limit
 * still says by how much it missed rather than only that it did.
 *
 * A boss is not fought in its own drops: whatever the difficulty pays is left
 * off the character, since nobody wearing it has yet to kill the thing.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:boss_sim
 *   bazelisk run //analysis:boss_sim -- --boss=zakum --level=110
 *   bazelisk run //analysis:boss_sim -- --job=dark_knight --detail
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "analysis/skill_plan.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/combat/encounter.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"
#include "src/spawn.h"

ABSL_FLAG(std::string, boss, "hilla", "Which fight, by data file stem.");
ABSL_FLAG(std::string, difficulty, "",
          "Which difficulty of it, by name. Unset takes the first.");
ABSL_FLAG(int, level, 0,
          "The level to fight it at. Unset takes the level the difficulty "
          "opens at, which is the hardest a player can meet it.");
ABSL_FLAG(std::string, job, "",
          "One branch, as the enum spells it ('dark_knight'). Unset sweeps "
          "every branch that could stand at the level.");
ABSL_FLAG(bool, detail, false,
          "Also print what the character is holding and the book they spent "
          "their points on.");
ABSL_FLAG(std::string, weapon, "",
          "Hold this weapon type, as the enum spells it ('two_handed_axe'), "
          "rather than the one the job measures best with.");

namespace ms {
namespace {

// Fixes the random stream. The clear pays rolled drops, and an unseeded run
// would print a table that moved a little each time.
constexpr unsigned int kSimSeed = 20260821;

// The clock the measurement runs against, in place of the fight's own. Long
// enough that even a build with no business here finishes and reports a time.
constexpr double kMeasureSeconds = 3600.0;

// How long a candidate point is swung for while the book is being spent. Long
// enough for a cooldown to land a dozen times, which is all the settling a
// ranking needs.
constexpr double kTrySeconds = 30.0;

// What the character takes off this phase a second, swings and own-clock
// pulses together. The figure every choice below is ranked by.
double Rate(const GameState& state, const std::string& boss_key,
            const BossDifficulty& difficulty, int phase) {
  CombatParams params = ComputeBossParams(state, boss_key, difficulty, phase);
  if (!params.active) {
    return 0.0;
  }
  int enemies = 0;
  for (const Spawn& spawn : difficulty.phases(phase).spawns()) {
    enemies += SpawnCount(spawn);
  }
  Sequence played = PlaySwings(params, kTrySeconds, std::max(1, enemies));
  double rate = played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
  return rate + OffClockRate(params, played, 1.0, std::max(1, enemies));
}

// What one branch came to against the fight.
struct Result {
  std::string weapon;
  int combat_power = 0;
  std::string swing;  // what the fight chose to spend most of its time on
  double kill_seconds = 0.0;  // 0 for a build that never finished
  std::vector<std::string> gear;
  std::vector<std::pair<std::string, int>> book;
};

// Which swing the fight leaned on, read off the run rather than assumed: a
// boss is one enemy holding PDR, and what that is worth differs from what the
// farming loop picks.
std::string MainSwing(const GameState& state, const std::string& boss_key,
                      const BossDifficulty& difficulty) {
  CombatParams params = ComputeBossParams(state, boss_key, difficulty,
                                          difficulty.phases_size() - 1);
  Sequence played = PlaySwings(params, 60.0);
  if (played.main_attack < 0) {
    return "-";
  }
  return params.attacks[played.main_attack].name;
}

// The weapon the branch takes into the fight: what --weapon names, or what a
// character with a book spent the usual way measures best with.
//
// Measured on a scout rather than on the character who fights: the choice
// wants a book behind it, and the book below is spent against a weapon. One
// of the two has to go first, and the weapons inside a job's own tier are
// close enough that the scout settles it.
EquipType WeaponFor(const Catalogs& catalogs, int level, Job branch) {
  std::string named = absl::GetFlag(FLAGS_weapon);
  if (!named.empty()) {
    EquipType type = EQUIP_TYPE_UNSPECIFIED;
    if (!EquipType_Parse("EQUIP_TYPE_" + absl::AsciiStrToUpper(named), &type)) {
      LOG(FATAL) << "Unknown --weapon '" << named << "'";
    }
    return type;
  }
  GameState state = NewState(catalogs, kSimSeed);
  GrowTo(state, level, PathTo(branch));
  Outfit(state, /*budget=*/false);
  return state.character.weapon_type();
}

// Grows the branch, gears it, spends its book on this fight, and fights. The
// clock is raised on this state's own copy of the boss, so a build that misses
// the limit still comes back with a time.
Result Fight(const Catalogs& catalogs, int level, Job branch,
             const std::string& boss_key, int difficulty_index) {
  GameState state = NewState(catalogs, kSimSeed);
  state.bosses = catalogs.bosses;
  GrowTo(state, level, PathTo(branch), /*spend_sp=*/false);

  Boss& boss = state.bosses[boss_key];
  BossDifficulty* difficulty = boss.mutable_difficulties(difficulty_index);
  OutfitWeapon(state, WeaponFor(catalogs, level, branch));
  OutfitDrops(state, BossOwnDrops(*difficulty));
  FullyUpgrade(state);
  int phase = BossObjectivePhase(catalogs.mobs, *difficulty);
  SpendBookWithToggles(state, [&](GameState& s) {
    return Rate(s, boss_key, *difficulty, phase);
  });
  // Again, now that the book is spent: which trace an item wants is measured
  // on a swing, and the swing has changed under it.
  FullyUpgrade(state);

  Result result;
  result.weapon = HeldWeaponName(state.character);
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    result.gear.push_back(entry.second.name());
  }
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    int learned = state.character.skill_level(entry.second);
    if (learned > 0 &&
        state.character.HasAdvancement(entry.second.job_advancement())) {
      result.book.push_back({entry.second.name(), learned});
    }
  }
  const Character& proto = state.character.proto();
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  result.combat_power = CombatPower(OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(),
      TotalEquipStats(state.character, derived), state.character.weapon_type(),
      /*attack_skill=*/nullptr, /*attack_level=*/0,
      PassiveOffenseFor(derived)));
  result.swing = MainSwing(state, boss_key, *difficulty);

  result.kill_seconds =
      FightBoss(state, boss_key, difficulty_index, kMeasureSeconds).seconds;
  return result;
}

// mm:ss, the way the fight's own clock reads.
std::string Clock(double seconds) {
  int total = static_cast<int>(seconds + 0.5);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
  return buf;
}

// The branches to sweep: the one --job names, or every branch that could
// stand at the level.
std::vector<Job> Branches(int level) {
  std::string name = absl::GetFlag(FLAGS_job);
  if (name.empty()) {
    return BranchesAt(level);
  }
  return {ParseBranch(name)};
}

void Run() {
  Catalogs catalogs = LoadCatalogs();
  std::string boss_key = absl::GetFlag(FLAGS_boss);
  std::map<std::string, Boss>::const_iterator found =
      catalogs.bosses.find(boss_key);
  if (found == catalogs.bosses.end()) {
    LOG(FATAL) << "Unknown --boss '" << boss_key << "'";
  }
  const Boss& boss = found->second;
  int index = BossDifficultyIndex(boss, absl::GetFlag(FLAGS_difficulty));
  if (index < 0) {
    LOG(FATAL) << "Unknown --difficulty '" << absl::GetFlag(FLAGS_difficulty)
               << "'";
  }
  const BossDifficulty& difficulty = boss.difficulties(index);
  int limit = difficulty.time_limit_seconds();
  int level = absl::GetFlag(FLAGS_level);
  if (level <= 0) {
    level = std::max(difficulty.unlock_level(), 1);
  }

  std::vector<Job> branches = Branches(level);
  std::vector<Result> results(branches.size());
  ParallelFor(static_cast<int>(branches.size()), [&](int i) {
    results[i] = Fight(catalogs, level, branches[i], boss_key, index);
  });

  std::printf(
      "%s %s at level %d: %lld HP over %d phase%s, %d%% PDR, %s on the "
      "clock.\nEvery AP in the job's stat, every skill maxed, every slot in "
      "the best the game holds -- less what this fight is the only source "
      "of.\n\n",
      difficulty.name().c_str(), boss.name().c_str(), level,
      static_cast<long long>(BossTotalHp(catalogs.mobs, difficulty)),
      difficulty.phases_size(), difficulty.phases_size() == 1 ? "" : "s",
      BossPdr(catalogs.mobs, difficulty), Clock(limit).c_str());
  std::printf("%-15s  %-24s  %8s  %-20s  %8s  %9s  %s\n", "job", "weapon", "CP",
              "swing", "kill", "HP/s", "");
  std::printf("%s\n", std::string(104, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    const Result& result = results[i];
    std::string kill =
        result.kill_seconds > 0.0 ? Clock(result.kill_seconds) : "never";
    double rate =
        result.kill_seconds > 0.0
            ? BossTotalHp(catalogs.mobs, difficulty) / result.kill_seconds
            : 0.0;
    bool cleared = result.kill_seconds > 0.0 && result.kill_seconds <= limit;
    std::printf("%-15s  %-24s  %8d  %-20s  %8s  %9.0f  %s\n",
                BranchName(branches[i]).c_str(), result.weapon.c_str(),
                result.combat_power, result.swing.c_str(), kill.c_str(), rate,
                cleared ? "clears" : "MISSES");
    if (absl::GetFlag(FLAGS_detail)) {
      std::printf("                 ");
      for (const std::string& piece : result.gear) {
        std::printf("%s  ", piece.c_str());
      }
      std::printf("\n                 ");
      for (const std::pair<std::string, int>& skill : result.book) {
        std::printf("%s %d  ", skill.first.c_str(), skill.second);
      }
      std::printf("\n\n");
    }
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
