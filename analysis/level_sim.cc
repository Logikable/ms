/* How long a character takes to climb from level 1 to the trial cap, branch by
 * branch. The real engine, run forward: the same AdvanceCombat the frontend
 * ticks, paid out at the same rate, so what it reports is playtime rather than
 * an estimate of it.
 *
 * The one thing the engine cannot supply is what the player does between
 * fights, so the sweep plays them:
 *
 *   - every AP on the job's primary stat, every SP on whatever it will buy;
 *   - the whole Etc tab sold at each level;
 *   - the best weapon they can hold and pay for, bought the level it comes
 *     within reach, with the type of it measured rather than assumed;
 *   - and the map that pays the most EXP a second of the ones they live on,
 *     re-chosen at every level.
 *
 * That last one is measured, not guessed: each candidate map is played out for
 * a few respawn beats with the character exactly as they stand, and a map that
 * kills them is not a candidate. It makes this an attentive player's clock --
 * a floor on how long the climb takes rather than an average of it.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:level_sim
 *   bazelisk run //analysis:level_sim -- --detail
 *   bazelisk run //analysis:level_sim -- --branch=DARK_KNIGHT
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "analysis/parallel.h"
#include "analysis/sim_gear.h"
#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/character/job_advancement.h"
#include "src/character/progression.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/shop.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

ABSL_FLAG(double, step, 0.5,
          "Seconds the fight is advanced by per tick. Comfortably finer than a "
          "swing (0.66s) or a mob hit (1.5s), which is all the resolution the "
          "climb needs: against 0.1s the playtimes move by under 2% and the "
          "sweep takes a third as long.");
ABSL_FLAG(int, probe_beats, 4,
          "Respawn beats each candidate map is played out for when the "
          "character picks where to farm.");
ABSL_FLAG(double, give_up_hours, 2000.0,
          "Playtime after which a branch is written off as stuck.");
ABSL_FLAG(bool, detail, false,
          "Also print the map and the weapon each level was spent on.");
// Fixes the random stream every run of this sim draws from. Rewards are
// rolled, so an unseeded run would print a table that moved a little each
// time and hide a real change under the noise. Move it to see how much of a
// number is the seed and how much is the game.
ABSL_FLAG(int, seed, 20260813, "The random stream every climb draws from.");
ABSL_FLAG(std::string, branch, "",
          "One branch to climb, as its Job enum name without the JOB_ prefix "
          "(DARK_KNIGHT). Empty climbs them all, which waits on the slowest "
          "of them however many cores are free -- so name one when one is the "
          "question.");

namespace ms {
namespace {

// The levels the table reports a running total at.
constexpr int kMilestones[] = {10, 20, 30,  40,  50,  60,  70,
                               80, 90, 100, 110, 120, 130, 140};
constexpr int kNumMilestones = sizeof(kMilestones) / sizeof(kMilestones[0]);

struct Catalogs {
  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
};

Catalogs LoadCatalogs() {
  Catalogs c;
  c.equips = LoadTextProtoMap<EquipPrototype>(EmbeddedEquips());
  c.scrolls = LoadTextProtoMap<Scroll>(EmbeddedScrolls());
  c.items = LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  c.mobs = LoadTextProtoMap<Mob>(EmbeddedMobs());
  c.maps = LoadTextProtoMap<MapData>(EmbeddedMaps());
  c.skills = LoadTextProtoMap<Skill>(EmbeddedSkills());
  return c;
}

// The 2nd-job branches by name. Spelled out here rather than borrowed from the
// frontend's JobName, which a combat tool has no business reaching into.
std::string BranchName(Job job) {
  switch (job) {
    case JOB_FIGHTER:
      return "Fighter";
    case JOB_PAGE:
      return "Page";
    case JOB_SPEARMAN:
      return "Spearman";
    case JOB_HUNTER:
      return "Hunter";
    case JOB_CROSSBOWMAN:
      return "Crossbowman";
    case JOB_ICE_LIGHTNING_WIZARD:
      return "I/L Wizard";
    case JOB_FIRE_POISON_WIZARD:
      return "F/P Wizard";
    case JOB_CLERIC:
      return "Cleric";
    case JOB_ASSASSIN:
      return "Assassin";
    case JOB_BANDIT:
      return "Bandit";
    case JOB_BERSERKER:
      return "Berserker";
    case JOB_CRUSADER:
      return "Crusader";
    case JOB_WHITE_KNIGHT:
      return "White Knight";
    case JOB_RANGER:
      return "Ranger";
    case JOB_SNIPER:
      return "Sniper";
    case JOB_ICE_LIGHTNING_MAGE:
      return "I/L Mage";
    case JOB_FIRE_POISON_MAGE:
      return "F/P Mage";
    case JOB_PRIEST:
      return "Priest";
    case JOB_HERMIT:
      return "Hermit";
    case JOB_CHIEF_BANDIT:
      return "Chief Bandit";
    case JOB_DARK_KNIGHT:
      return "Dark Knight";
    case JOB_PALADIN:
      return "Paladin";
    case JOB_HERO:
      return "Hero";
    case JOB_BOW_MASTER:
      return "Bow Master";
    default:
      return "?";
  }
}

// The advancements a branch is reached through, in order, so the climb takes
// the same path a player does and collects each book's skills on the way. Read
// off the game's own stage table, so a third job joins by existing.
std::vector<Job> PathTo(Job branch) {
  std::vector<Job> path;
  for (int stage = 1;; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(branch, stage);
    if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
      return path;
    }
    path.push_back(JobForAdvancement(advancement));
  }
}

// Spends everything the last level handed over.
void SpendPoints(CharacterInstance& character) {
  while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
  }
}

void LearnEverything(GameState& state) {
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    while (state.character.LearnSkill(entry.second)) {
    }
  }
}

// Sells the Etc tab, skipping what will not sell. A token is the one Etc item
// worth keeping: it sells for nothing and buys the Frozen tier.
void SellDrops(CharacterInstance& character) {
  int i = 0;
  while (i < static_cast<int>(character.stackables(ITEM_CATEGORY_ETC).size())) {
    int count = character.stackables(ITEM_CATEGORY_ETC)[i].count();
    if (character.SellStackable(ITEM_CATEGORY_ETC, i, count) <= 0) {
      ++i;  // a token sells for nothing; it is kept and spent on the shelf
    }
  }
}

// What a map came to over the probe.
struct Probe {
  double exp_per_second = 0.0;
  bool died = false;
};

// Plays `map` out for a few respawn beats with the character exactly as they
// stand, and reports what it pays. Nothing is banked -- the fight is run
// straight off CombatParams rather than through AdvanceCombat, so the probe
// costs the character neither EXP nor meso nor HP.
Probe ProbeMap(GameState& state, const std::string& map, int beats,
               double step) {
  std::string held = state.current_map;
  state.current_map = map;
  CombatParams params = ComputeCombatParams(state);
  state.current_map = held;

  Probe probe;
  if (!params.active || params.types.empty()) {
    probe.died = true;  // nothing to fight, which is as good as unusable
    return probe;
  }
  double horizon = beats * params.respawn_seconds;
  double exp = 0.0;
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < horizon; elapsed += step) {
    sim.Advance(params, step);
    const std::vector<int64_t>& kills = sim.kills_this_step();
    for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
      exp += kills[i] * params.types[i].mob->exp();
    }
    if (sim.died_this_step()) {
      probe.died = true;
      return probe;
    }
  }
  probe.exp_per_second = exp / horizon;
  return probe;
}

// Moves the character to whichever map pays the most EXP a second of the ones
// they survive. Leaves them where they are if every map kills them, which the
// give-up clock then catches.
void PickMap(GameState& state, const std::vector<std::string>& candidates,
             int beats, double step) {
  std::string best;
  double best_rate = 0.0;
  for (const std::string& map : candidates) {
    Probe probe = ProbeMap(state, map, beats, step);
    if (probe.died || probe.exp_per_second <= best_rate) {
      continue;
    }
    best_rate = probe.exp_per_second;
    best = map;
  }
  if (!best.empty()) {
    state.current_map = best;
  }
}

// Everything the player does on levelling up, in the order that makes each
// step pay for the next: the advancement first, then the points it hands over,
// then the drops turned into meso, then the weapon that meso buys, and only
// then the choice of where to take it.
void Retool(GameState& state, const std::vector<Job>& path, int* taken,
            const std::vector<std::string>& maps, int beats, double step) {
  if (state.character.CanAdvanceJob() &&
      *taken < static_cast<int>(path.size())) {
    Job job = path[(*taken)++];
    PerformJobAdvancement(state, job);
    for (const std::string& key : StarterEquipsFor(job)) {
      std::map<std::string, EquipPrototype>::const_iterator it =
          state.equips.find(key);
      if (it != state.equips.end()) {
        EquipByName(state.character, it->second.name());
      }
    }
  }
  SpendPoints(state.character);
  LearnEverything(state);
  SellDrops(state.character);
  Outfit(state, /*budget=*/true);
  PickMap(state, maps, beats, step);
}

// One level of one branch's climb, for --detail.
struct Stint {
  int level = 0;
  double seconds = 0.0;
  std::string map;
  std::string weapon;
};

// The Frozen Set's pieces, in the order they become wearable, and the two
// tokens the last two pieces are bought with. Named rather than derived so the
// table reads the same however the catalog is walked.
const char* const kFrozenPieces[] = {"Frozen Top", "Frozen Bottom",
                                     "Frozen Hat", "Frozen Cape"};
constexpr int kNumFrozenPieces = 4;
const char* const kFrozenTokens[] = {"Frozen Weapon Token",
                                     "Frozen Secondary Token"};
// The same two by catalog key, which is how a mob's drop list names them.
const char* const kFrozenTokenKeys[] = {"frozen_weapon_token",
                                        "frozen_secondary_token"};
constexpr int kNumFrozenTokens = 2;

// What one branch's climb came to.
struct Climb {
  // Playtime in seconds at each milestone, or -1 for one never reached.
  double milestone_seconds[kNumMilestones];
  // The level each Frozen piece first dropped at, or 0 for one that never
  // did. The rates are set so that all four arrive before the level cap --
  // this is the check on that, farmed rather than argued.
  int frozen_level[kNumFrozenPieces] = {0, 0, 0, 0};
  // The same for the two tokens, which is the only reading there is on
  // whether a climb ever gets to hold the tier they buy.
  int token_level[kNumFrozenTokens] = {0, 0};
  // Kills off mobs that carry each token, and the log of the chance every one
  // of them came up empty. One climb either drops a token or does not, which
  // says almost nothing at a rate this long; the kills behind it say how
  // likely that was, which is the number worth reading.
  int64_t token_kills[kNumFrozenTokens] = {0, 0};
  double token_log_miss[kNumFrozenTokens] = {0.0, 0.0};
  std::vector<Stint> stints;
};

// Counts this step's kills against the tokens they were a chance at.
void NoteTokenChances(const CombatParams& params, const CombatSim& sim,
                      Climb& climb) {
  const std::vector<int64_t>& kills = sim.kills_this_step();
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    if (kills[i] <= 0) {
      continue;
    }
    for (const MobDrop& drop : params.types[i].mob->drops()) {
      for (int token = 0; token < kNumFrozenTokens; ++token) {
        if (drop.item() != kFrozenTokenKeys[token] || drop.per_kill() <= 0.0) {
          continue;
        }
        climb.token_kills[token] += kills[i];
        climb.token_log_miss[token] += kills[i] * std::log1p(-drop.per_kill());
      }
    }
  }
}

// Notes any Frozen piece or token picked up since the last look. Called before
// the level's shopping, so a token counted here is one the shelf has not
// spent yet.
void NoteFrozenDrops(const GameState& state, int level, Climb& climb) {
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    for (int piece = 0; piece < kNumFrozenPieces; ++piece) {
      if (climb.frozen_level[piece] == 0 &&
          bag[i].prototype().name() == kFrozenPieces[piece]) {
        climb.frozen_level[piece] = level;
      }
    }
  }
  for (const StackableItem& stack :
       state.character.stackables(ITEM_CATEGORY_ETC)) {
    for (int token = 0; token < kNumFrozenTokens; ++token) {
      if (climb.token_level[token] == 0 &&
          stack.name() == kFrozenTokens[token]) {
        climb.token_level[token] = level;
      }
    }
  }
}

Climb Play(const Catalogs& catalogs, Job branch,
           const std::vector<std::string>& maps) {
  double step = absl::GetFlag(FLAGS_step);
  int beats = absl::GetFlag(FLAGS_probe_beats);
  double give_up = absl::GetFlag(FLAGS_give_up_hours) * 3600.0;

  GameState state(catalogs.equips, catalogs.scrolls, catalogs.items,
                  catalogs.mobs, catalogs.maps, catalogs.skills,
                  GameMode::kPlay, JOB_ADVANCEMENT_UNSPECIFIED,
                  static_cast<unsigned int>(absl::GetFlag(FLAGS_seed)));
  std::vector<Job> path = PathTo(branch);
  int taken = 0;

  Climb climb;
  for (int i = 0; i < kNumMilestones; ++i) {
    climb.milestone_seconds[i] = -1.0;
  }
  Retool(state, path, &taken, maps, beats, step);

  int level = state.character.proto().level();
  double seconds = 0.0;
  double level_began = 0.0;
  Stint stint = {level, 0.0, state.current_map,
                 HeldWeaponName(state.character)};
  CombatSim sim;
  // Built once and reused until something changes it. Nothing in a fight moves
  // between two steps of the same level on the same map, and rebuilding it
  // every step is where this sim used to spend almost all of its time.
  CombatParams params = ComputeCombatParams(state);
  while (level < kTrialLevelCap && seconds < give_up) {
    AdvanceCombat(state, sim, params, step);
    NoteTokenChances(params, sim, climb);
    seconds += step;
    if (sim.died_this_step()) {
      // Dying sends them home, where there is nothing to fight.
      PickMap(state, maps, beats, step);
      params = ComputeCombatParams(state);
    }
    if (state.character.proto().level() == level) {
      continue;
    }
    stint.seconds = seconds - level_began;
    climb.stints.push_back(stint);
    level_began = seconds;
    level = state.character.proto().level();
    NoteFrozenDrops(state, level, climb);
    for (int i = 0; i < kNumMilestones; ++i) {
      if (level >= kMilestones[i] && climb.milestone_seconds[i] < 0.0) {
        climb.milestone_seconds[i] = seconds;
      }
    }
    Retool(state, path, &taken, maps, beats, step);
    params = ComputeCombatParams(state);
    stint = {level, 0.0, state.current_map, HeldWeaponName(state.character)};
  }
  return climb;
}

// Seconds as a playtime a person can read: hours and minutes up to a day,
// then days and hours.
std::string Clock(double seconds) {
  if (seconds < 0.0) {
    return "-";
  }
  int minutes = static_cast<int>(seconds / 60.0 + 0.5);
  char text[32];
  if (minutes < 24 * 60) {
    std::snprintf(text, sizeof(text), "%dh%02dm", minutes / 60, minutes % 60);
  } else {
    std::snprintf(text, sizeof(text), "%dd%02dh", minutes / (24 * 60),
                  (minutes / 60) % 24);
  }
  return text;
}

// The maps worth offering the player: the ones with something on them.
std::vector<std::string> HuntingGrounds(const Catalogs& catalogs) {
  std::vector<std::string> maps;
  for (const std::pair<const std::string, MapData>& entry : catalogs.maps) {
    if (!entry.second.spawns().empty()) {
      maps.push_back(entry.first);
    }
  }
  return maps;
}

void PrintDetail(const Catalogs& catalogs, const Climb& climb) {
  std::printf("    %5s  %8s  %-26s  %s\n", "level", "took", "map", "weapon");
  for (const Stint& stint : climb.stints) {
    std::string map = catalogs.maps.count(stint.map) > 0
                          ? catalogs.maps.at(stint.map).name()
                          : stint.map;
    std::printf("    %5d  %8s  %-26s  %s\n", stint.level,
                Clock(stint.seconds).c_str(), map.c_str(),
                stint.weapon.c_str());
  }
  std::printf("\n");
}

// What the kills say about the tokens, which is a steadier reading than
// whether this one seed dropped them: the band is where a token can fall at
// all, so a climb that hurries through it buys fewer chances at one.
void PrintTokenOdds(const Job* branches, const std::vector<Climb>& climbs,
                    int count) {
  std::printf(
      "\nThe chance a climb makes those kills and comes away with nothing, "
      "since one seed\ndropping a token says little at a rate this long. "
      "Kills are "
      "off the mobs that carry one.\n\n");
  std::printf("%-13s  %11s", "branch", "band kills");
  for (const char* token : kFrozenTokens) {
    std::printf(
        "  %18s",
        (std::string("no ") + (token + std::strlen("Frozen "))).c_str());
  }
  std::printf("\n%s\n", std::string(26 + 20 * kNumFrozenTokens, '-').c_str());
  for (int i = 0; i < count; ++i) {
    if (PathTo(branches[i]).size() < 3) {
      continue;
    }
    std::printf("%-13s  %11lld", BranchName(branches[i]).c_str(),
                static_cast<long long>(climbs[i].token_kills[0]));
    for (int token = 0; token < kNumFrozenTokens; ++token) {
      std::printf("  %17.1f%%",
                  100.0 * std::exp(climbs[i].token_log_miss[token]));
    }
    std::printf("\n");
  }
}

// The branches to climb: all of them, or the one --branch names. Dies on a
// name no branch answers to rather than printing an empty table.
std::vector<Job> BranchesToClimb(const Job* all, int count) {
  std::string name = absl::AsciiStrToUpper(absl::GetFlag(FLAGS_branch));
  if (name.empty()) {
    return std::vector<Job>(all, all + count);
  }
  Job wanted = JOB_UNSPECIFIED;
  if (Job_Parse("JOB_" + name, &wanted)) {
    for (int i = 0; i < count; ++i) {
      if (all[i] == wanted) {
        return {wanted};
      }
    }
  }
  LOG(FATAL) << "Unknown --branch '" << absl::GetFlag(FLAGS_branch) << "'";
  return {};
}

void Run() {
  Catalogs catalogs = LoadCatalogs();
  std::vector<std::string> maps = HuntingGrounds(catalogs);
  const Job kEveryBranch[] = {
      JOB_FIGHTER,
      JOB_PAGE,
      JOB_SPEARMAN,
      JOB_HUNTER,
      JOB_CROSSBOWMAN,
      JOB_ICE_LIGHTNING_WIZARD,
      JOB_FIRE_POISON_WIZARD,
      JOB_CLERIC,
      JOB_ASSASSIN,
      JOB_BANDIT,
      JOB_BERSERKER,
      JOB_CRUSADER,
      JOB_WHITE_KNIGHT,
      JOB_RANGER,
      JOB_SNIPER,
      JOB_ICE_LIGHTNING_MAGE,
      JOB_FIRE_POISON_MAGE,
      JOB_PRIEST,
      JOB_HERMIT,
      JOB_CHIEF_BANDIT,
      JOB_DARK_KNIGHT,
      JOB_PALADIN,
      JOB_HERO,
      JOB_BOW_MASTER,
  };
  std::vector<Job> branches = BranchesToClimb(
      kEveryBranch, static_cast<int>(sizeof(kEveryBranch) / sizeof(Job)));

  std::printf(
      "Playtime to each level, played at the best map the character survives "
      "and the best weapon the shop will sell them.\n\n");
  std::printf("%-13s", "branch");
  for (int level : kMilestones) {
    std::printf("  %8s", ("Lv" + std::to_string(level)).c_str());
  }
  std::printf("\n%s\n", std::string(13 + 10 * kNumMilestones, '-').c_str());

  // Every branch climbs on its own character and its own copy of the
  // catalogs, so they all run at once. The rows are printed afterwards, in the
  // table's own order rather than the order the threads happened to finish.
  int count = static_cast<int>(branches.size());
  std::vector<Climb> climbs(count);
  ParallelFor(count,
              [&](int i) { climbs[i] = Play(catalogs, branches[i], maps); });

  for (int i = 0; i < count; ++i) {
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      std::printf("  %8s", Clock(climbs[i].milestone_seconds[m]).c_str());
    }
    std::printf("\n");
    if (absl::GetFlag(FLAGS_detail)) {
      PrintDetail(catalogs, climbs[i]);
    }
  }

  // Third jobs only. A branch that stops at its 2nd job is scaffolding for the
  // playtime table above, not a player: the advancement is there at 60 and
  // nobody climbs to 100 without it. It matters here because the two answer
  // differently -- a 2nd job finds the top two maps a coin flip against Sand
  // Dwarf and stays put, where a 3rd job is paid a third more for moving up,
  // and the cape only falls off the mobs up there.
  std::printf(
      "\nThe level each Frozen piece first dropped at, and each token first "
      "fell, for the branches\nthat take their 3rd advancement. A dash is a "
      "climb that reached the cap without it.\n\n");
  std::printf("%-13s", "branch");
  for (const char* piece : kFrozenPieces) {
    std::printf("  %8s", piece + std::strlen("Frozen "));
  }
  for (const char* token : kFrozenTokens) {
    std::printf("  %15s", token + std::strlen("Frozen "));
  }
  std::printf(
      "\n%s\n",
      std::string(13 + 10 * kNumFrozenPieces + 17 * kNumFrozenTokens, '-')
          .c_str());
  for (int i = 0; i < count; ++i) {
    if (PathTo(branches[i]).size() < 3) {
      continue;
    }
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int piece = 0; piece < kNumFrozenPieces; ++piece) {
      int level = climbs[i].frozen_level[piece];
      std::printf("  %8s",
                  level == 0 ? "-" : ("Lv" + std::to_string(level)).c_str());
    }
    for (int token = 0; token < kNumFrozenTokens; ++token) {
      int level = climbs[i].token_level[token];
      std::printf("  %15s",
                  level == 0 ? "-" : ("Lv" + std::to_string(level)).c_str());
    }
    std::printf("\n");
  }
  PrintTokenOdds(branches.data(), climbs, count);
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
