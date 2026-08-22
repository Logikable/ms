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
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/combat/boss_run.h"
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

// The step the fight is walked in: the frame the game draws at, so a swing
// lands on the same tick it would land on in front of a player.
constexpr double kStepSeconds = 1.0 / 60.0;

// The clock the measurement runs against, in place of the fight's own. Long
// enough that even a build with no business here finishes and reports a time.
constexpr double kMeasureSeconds = 3600.0;

// How long a candidate point is swung for while the book is being spent. Long
// enough for a cooldown to land a dozen times, which is all the settling a
// ranking needs.
constexpr double kTrySeconds = 30.0;

// The difficulty --difficulty names, or the first. Dies rather than measuring
// a fight nobody asked for.
int DifficultyIndex(const Boss& boss, const std::string& name) {
  if (name.empty()) {
    return 0;
  }
  for (int i = 0; i < boss.difficulties_size(); ++i) {
    if (boss.difficulties(i).name() == name) {
      return i;
    }
  }
  LOG(FATAL) << "Unknown --difficulty '" << name << "'";
  return 0;
}

// Everything the fight is holding, over all its phases.
int64_t TotalHp(const Catalogs& catalogs, const BossDifficulty& difficulty) {
  int64_t hp = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    for (const Spawn& spawn : phase.spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          catalogs.mobs.find(spawn.mob());
      if (it != catalogs.mobs.end()) {
        hp += static_cast<int64_t>(SpawnCount(spawn)) * it->second.max_hp();
      }
    }
  }
  return hp;
}

// The stiffest defence anything in the fight stands behind, for the header:
// it is what every Ignore DEF lever in the books is worth here.
int Pdr(const Catalogs& catalogs, const BossDifficulty& difficulty) {
  int pdr = 0;
  for (const BossPhase& phase : difficulty.phases()) {
    for (const Spawn& spawn : phase.spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          catalogs.mobs.find(spawn.mob());
      if (it != catalogs.mobs.end()) {
        pdr = std::max(pdr, it->second.pdr());
      }
    }
  }
  return pdr;
}

// What the difficulty pays, as catalog keys. Left off the character: a fight
// cannot be beaten in gear only that fight hands out.
std::set<std::string> OwnDrops(const BossDifficulty& difficulty) {
  std::set<std::string> keys;
  for (const MobDrop& drop : difficulty.drops()) {
    if (drop.has_equip()) {
      keys.insert(drop.equip());
    }
  }
  return keys;
}

// The phase the book is spent to beat: the one holding the most HP. A fight
// is decided against its heaviest phase, and Zakum's arms are not it.
int ObjectivePhase(const Catalogs& catalogs, const BossDifficulty& difficulty) {
  int best = 0;
  int64_t most = -1;
  for (int i = 0; i < difficulty.phases_size(); ++i) {
    int64_t hp = 0;
    for (const Spawn& spawn : difficulty.phases(i).spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          catalogs.mobs.find(spawn.mob());
      if (it != catalogs.mobs.end()) {
        hp += static_cast<int64_t>(SpawnCount(spawn)) * it->second.max_hp();
      }
    }
    if (hp > most) {
      most = hp;
      best = i;
    }
  }
  return best;
}

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

// The catalog by the name a skill requirement calls it: the display name,
// which is not the key the catalog is filed under.
std::map<std::string, const Skill*> ByName(const GameState& state) {
  std::map<std::string, const Skill*> named;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    named[entry.second.name()] = &entry.second;
  }
  return named;
}

// Raises `skill` by `levels`, buying whatever it demands first. Returns the
// points that went in, which is more than the levels asked for when the skill
// stands behind a requirement nothing has paid for yet, and fewer when the SP
// runs out. The caller restores the character afterwards, so a plan that
// cannot be finished costs nothing.
int Buy(GameState& state, const Skill& skill,
        const std::map<std::string, const Skill*>& named, int levels,
        int depth = 0) {
  int spent = 0;
  if (depth < 4 && skill.has_required_skill() &&
      !state.character.MeetsSkillRequirement(skill)) {
    std::map<std::string, const Skill*>::const_iterator req =
        named.find(skill.required_skill().skill_name());
    if (req == named.end()) {
      return 0;
    }
    while (!state.character.MeetsSkillRequirement(skill)) {
      int step = Buy(state, *req->second, named, 1, depth + 1);
      if (step == 0) {
        return spent;
      }
      spent += step;
    }
  }
  for (int i = 0; i < levels && state.character.LearnSkill(skill); ++i) {
    ++spent;
  }
  return spent;
}

// Spends the pool where it measures best against the fight: a point at a time,
// into whichever skill lifts the rate most per point it costs.
//
// The catalog's own order is no allocation at all. A 4th job at 130 holds 150
// points of a 200-point book, so which of them get spent is most of what the
// character hits for -- and a player choosing them reads the fight in front of
// them, which is what this does.
void SpendSp(GameState& state, const std::string& boss_key,
             const BossDifficulty& difficulty, int phase) {
  std::map<std::string, const Skill*> named = ByName(state);
  double held = Rate(state, boss_key, difficulty, phase);
  while (true) {
    Character before = state.character.ToProto();
    const Skill* best = nullptr;
    int best_levels = 0;
    double best_score = 0.0;
    double best_rate = held;
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      // One level, and the whole skill. A skill meant to replace the one
      // being swung is worth nothing at its first level and everything at its
      // last, and a chooser offered only the first would never buy it.
      for (int levels : {1, entry.second.max_level()}) {
        int points = Buy(state, entry.second, named, levels);
        if (points > 0) {
          double rate = Rate(state, boss_key, difficulty, phase);
          double score = (rate - held) / points;
          if (score > best_score) {
            best_score = score;
            best_rate = rate;
            best_levels = levels;
            best = &entry.second;
          }
        }
        state.character.RestoreFrom(before, state.equips, state.items);
      }
    }
    if (best == nullptr) {
      return;
    }
    Buy(state, *best, named, best_levels);
    held = best_rate;
  }
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
  OutfitDrops(state, OwnDrops(*difficulty));
  FullyUpgrade(state);
  int phase = ObjectivePhase(catalogs, *difficulty);
  SpendSp(state, boss_key, *difficulty, phase);
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

  difficulty->set_time_limit_seconds(static_cast<int>(kMeasureSeconds));
  BossRun run(boss_key, boss, difficulty_index);
  while (!run.done()) {
    run.Advance(state, kStepSeconds);
  }
  if (run.won()) {
    result.kill_seconds = kMeasureSeconds - run.seconds_left();
  }
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
  int index = DifficultyIndex(boss, absl::GetFlag(FLAGS_difficulty));
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
      static_cast<long long>(TotalHp(catalogs, difficulty)),
      difficulty.phases_size(), difficulty.phases_size() == 1 ? "" : "s",
      Pdr(catalogs, difficulty), Clock(limit).c_str());
  std::printf("%-15s  %-24s  %8s  %-20s  %8s  %9s  %s\n", "job", "weapon", "CP",
              "swing", "kill", "HP/s", "");
  std::printf("%s\n", std::string(104, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    const Result& result = results[i];
    std::string kill =
        result.kill_seconds > 0.0 ? Clock(result.kill_seconds) : "never";
    double rate = result.kill_seconds > 0.0
                      ? TotalHp(catalogs, difficulty) / result.kill_seconds
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
