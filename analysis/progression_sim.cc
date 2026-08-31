/* What a character reaches by the level cap: how long the climb takes, what it
 * paid them, and what they are standing in when they get there.
 *
 * The real engine, but jumped rather than ticked. Nothing that decides what a
 * character earns moves between two steps of the same level on the same map,
 * so the map is played out for a few respawn beats to measure its rate and the
 * whole stretch to the next thing that DOES move -- the level they are about
 * to make, or the moment the player next opens the game -- is handed to
 * AwardCombatRewards in one go. Every kill in it is still rolled for drops
 * exactly as a step would have rolled it; what is given up is the HP drift
 * inside a stretch, on a map PickMap has already watched them survive.
 *
 * The one thing the engine cannot supply is what the player does between
 * fights, so the sweep plays them:
 *
 *   - every AP on the job's primary stat, and every SP into whichever
 *     skill measures best on the map they are farming;
 *   - the honor the pool has collected spent on rerolling both Inner Ability
 *     setups, once the character is high enough to hold one;
 *   - the whole Etc tab sold at each level;
 *   - the best weapon they can hold and pay for, bought the level it comes
 *     within reach, with the type of it measured rather than assumed;
 *   - what is left of the purse spent on scrolls and stars, the weapon first;
 *   - and the map that pays the most EXP a second of the ones they live on.
 *
 * That last one is measured, not guessed: each candidate map is played out for
 * a few respawn beats with the character exactly as they stand, and a map that
 * kills them is not a candidate.
 *
 * None of it happens on levelling up, though, because none of it is the
 * character's doing -- it is the player's, and the player is not always there.
 * They open the game a handful of times a day, in bursts rather than on a
 * clock, and less often as the climb slows; see LooksPerDay. The character
 * farms the whole time either way. `--attention=0` puts the player at every
 * level instead, which is the floor this sim used to report.
 *
 * Two sections, each on its own flag.
 *
 *   --playtime  how long each branch takes to each level, and what it earned
 *               on the way. The climb alone.
 *   --ledger    what the purse went on and what the character has to show for
 *               it: the weapon's slots and stars, and how much of each set is
 *               on their back.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 * The sweep climbs the branches that take a 4th advancement. The rest stop at
 * their 2nd or 3rd job and were never built to reach the cap; --all_branches
 * still climbs them, and --branch takes any one of them on its own.
 *
 * One branch takes about fifteen seconds, and the whole sweep about the same
 * again -- it climbs a branch a core. Anything far past that is a bug to
 * chase rather than a wait to sit through.
 *
 *   bazelisk run //analysis:progression_sim
 *   bazelisk run //analysis:progression_sim -- --detail
 *   bazelisk run //analysis:progression_sim -- --branch=DARK_KNIGHT
 *   bazelisk run //analysis:progression_sim -- --all_branches
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "analysis/ability_plan.h"
#include "analysis/gear_plan.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_format.h"
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "analysis/skill_plan.h"
#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/inner_ability.h"
#include "src/character/job_advancement.h"
#include "src/character/progression.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/loot.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/shop.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
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
ABSL_FLAG(bool, all_branches, false,
          "Climb the branches that stop at their 2nd or 3rd job as well. They "
          "were never meant to reach the cap -- the advancement is there at "
          "60 and at 100 -- so what they measure past that is a build nobody "
          "plays, and they are the slowest rows in the sweep by far.");
ABSL_FLAG(bool, playtime, true, "Print how long the climb takes.");
ABSL_FLAG(bool, ledger, true,
          "Print what the climb spent and what it is wearing at the cap.");
ABSL_FLAG(int, star_ceiling, ms::kMaxStarForce,
          "A star the shopper will not go past whatever its arithmetic says. "
          "Here so one run can be pinned against another: the shopper decides "
          "where to stop by price, and stops well short of this.");
ABSL_FLAG(int, scroll_rate, 100,
          "Success rate of the scrolls bought, as a whole percent. A lower "
          "rate pays more per slot it lands and wastes the rest, which on a "
          "drop-only piece is a slot nothing gets back.");
ABSL_FLAG(bool, endgame, true,
          "Print what the days after the cap add up to: the best money map "
          "farmed and the dailies run, with the purse still spending.");
ABSL_FLAG(double, endgame_days, 7.0,
          "Days played at the cap for the endgame section.");
ABSL_FLAG(bool, dailies, true,
          "Run each unlocked boss once a day, on the climb and after it. A "
          "run that misses the limit pays nothing and still costs the whole "
          "of it, which is what trying too early spends.");
ABSL_FLAG(double, attention, 1.0,
          "Scales how often the player opens the game. 1 is the table in "
          "LooksPerDay; a larger number is a more attentive player, and 0 "
          "puts them there at every level, which is what this sim used to "
          "assume.");
ABSL_FLAG(int, runs, 1,
          "Climbs per branch, each on its own seed. Drops are rolled, so one "
          "climb says almost nothing about whether a set completes.");
ABSL_FLAG(std::string, ability_rank, "unique",
          "The Inner Ability rank a character rolls up to before they hold "
          "any line through a reset. A lock buys nothing while what they are "
          "short of is a rank, and the ladder to Legendary costs more honor "
          "than a climb to the cap is paid.");
ABSL_FLAG(std::string, branch, "",
          "One branch to climb, as its Job enum name without the JOB_ prefix "
          "(DARK_KNIGHT). Empty climbs them all, which waits on the slowest "
          "of them however many cores are free -- so name one when one is the "
          "question.");

namespace ms {
namespace {

// The levels the table reports a running total at. Every tenth to 140, where
// the SP schedule ends and the books are finished, then every twentieth to the
// cap -- past 140 a level buys HP, MP and AP and nothing else, so the rungs
// need not be as close together.
constexpr int kMilestones[] = {10,  20,  30,  40,  50,  60,  70,  80, 90,
                               100, 110, 120, 130, 140, 160, 180, 200};
constexpr int kNumMilestones = sizeof(kMilestones) / sizeof(kMilestones[0]);

// The rank a character rolls the ability up to before holding anything.
AbilityRank AbilityRankWanted() {
  const std::string& named = absl::GetFlag(FLAGS_ability_rank);
  AbilityRank rank = ABILITY_RANK_UNSPECIFIED;
  if (!AbilityRank_Parse("ABILITY_RANK_" + absl::AsciiStrToUpper(named),
                         &rank) ||
      rank == ABILITY_RANK_UNSPECIFIED) {
    LOG(FATAL) << "Unknown --ability_rank '" << named << "'";
  }
  return rank;
}

// Spends everything the last level handed over.
void SpendPoints(CharacterInstance& character) {
  while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
  }
}

// Whatever the ranking would not spend. A point the greedy declines is one it
// could not measure a gain for -- a utility skill, or one whose worth is in
// staying alive rather than in the damage table -- and leaving it in the pool
// is the one thing worse than spending it in the catalog's order.
void LearnTheRest(GameState& state) {
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    while (state.character.LearnSkill(entry.second)) {
    }
  }
}

// How long the book's ranking is played out for. Shorter than the run a weapon
// is measured over: a ranking has only to settle an order, and this one is
// taken again at every level of every climb.
constexpr double kBookSeconds = 10.0;

// Seconds in a day, which is what a daily reset waits out. The game is idle,
// so a day of playtime is a day.
constexpr double kDaySeconds = 24.0 * 60.0 * 60.0;

// The fights open to `level`, by boss key and difficulty, lowest unlock
// first. Only the difficulties the game has actually built: one marked coming
// soon is a shell with nothing in it but HP.
std::vector<std::pair<std::string, int>> UnlockedBosses(const GameState& state,
                                                        int level) {
  std::vector<std::pair<int, std::pair<std::string, int>>> open;
  for (const std::pair<const std::string, Boss>& entry : state.bosses) {
    for (int i = 0; i < entry.second.difficulties_size(); ++i) {
      const BossDifficulty& difficulty = entry.second.difficulties(i);
      if (difficulty.coming_soon() || difficulty.unlock_level() > level) {
        continue;
      }
      open.push_back({difficulty.unlock_level(), {entry.first, i}});
    }
  }
  std::sort(open.begin(), open.end());
  std::vector<std::pair<std::string, int>> fights;
  for (const std::pair<int, std::pair<std::string, int>>& entry : open) {
    fights.push_back(entry.second);
  }
  return fights;
}

// What the character takes off the map they are standing on, a second: their
// swings against the crowd it holds, plus anything of theirs on a clock of its
// own.
double CrowdRate(GameState& state) {
  CombatParams params = ComputeCombatParams(state);
  if (!params.active || params.types.empty()) {
    return 0.0;
  }
  int enemies = 0;
  for (const CombatType& type : params.types) {
    enemies += type.simultaneous;
  }
  enemies = std::max(1, enemies);
  Sequence played = PlaySwings(params, kBookSeconds, enemies);
  double rate = played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
  return rate + OffClockRate(params, played, 1.0, enemies);
}

// The fight the book is aimed at: the stiffest one open to them, or the next
// one to open before any are. A player spends points on the boss they are
// about to meet rather than on the one they beat last month.
bool BookTarget(const GameState& state, std::pair<std::string, int>* fight) {
  int level = state.character.proto().level();
  std::vector<std::pair<std::string, int>> open = UnlockedBosses(state, level);
  if (!open.empty()) {
    *fight = open.back();
    return true;
  }
  int soonest = 0;
  for (const std::pair<const std::string, Boss>& entry : state.bosses) {
    for (int i = 0; i < entry.second.difficulties_size(); ++i) {
      const BossDifficulty& difficulty = entry.second.difficulties(i);
      if (difficulty.coming_soon() || difficulty.unlock_level() <= level) {
        continue;
      }
      if (soonest == 0 || difficulty.unlock_level() < soonest) {
        soonest = difficulty.unlock_level();
        *fight = {entry.first, i};
      }
    }
  }
  return soonest > 0;
}

// What the character takes off that fight a second. One enemy standing behind
// its own defence, which is a different question from the crowd above: the
// swing that clears twelve is rarely the one that kills the one that matters.
double BossRate(GameState& state) {
  std::pair<std::string, int> fight;
  if (!BookTarget(state, &fight)) {
    return 0.0;
  }
  const BossDifficulty& difficulty =
      state.bosses[fight.first].difficulties(fight.second);
  int phase = BossObjectivePhase(state.mobs, difficulty);
  CombatParams params =
      ComputeBossParams(state, fight.first, difficulty, phase);
  if (!params.active) {
    return 0.0;
  }
  Sequence played = PlaySwings(params, kBookSeconds);
  double rate = played.seconds > 0.0 ? played.damage / played.seconds : 0.0;
  return rate + OffClockRate(params, played, 1.0);
}

// The book ranked on both at once, weighted alike. A climb clears crowds and a
// boss is one enemy, and a book that answers only one of them is not a build
// anybody plays: ranked on the map alone the Hero reaches Hilla holding
// nothing that kills her.
//
// The geometric mean because it needs no weight chosen for it, and because it
// scores a build that cannot do one of the two at nothing at all, which is the
// whole point of asking for both. Either leg stands in alone when the other
// has nothing to measure -- before the first fight exists, and on the walk
// home where there is no map.
double BookRate(GameState& state) {
  double crowd = CrowdRate(state);
  double boss = BossRate(state);
  if (crowd <= 0.0) {
    return boss;
  }
  if (boss <= 0.0) {
    return crowd;
  }
  return std::sqrt(crowd * boss);
}

// Follows the purse and adds up each direction on its own. The balance is no
// answer by itself -- a climb that has just bought a weapon looks poor -- so
// the tables report everything the character was ever paid and everything the
// shop ever took, and the difference is what they are holding.
struct Purse {
  int64_t earned = 0;
  int64_t spent = 0;
  int64_t held = 0;

  void Note(const CharacterInstance& character) {
    int64_t now = character.meso();
    if (now > held) {
      earned += now - held;
    } else {
      spent += held - now;
    }
    held = now;
  }
};

// Pieces of the set filed under `key` the character has on, 0 for a set the
// catalog does not hold.
int PiecesWorn(const GameState& state, const std::string& key) {
  std::map<std::string, EquipSet>::const_iterator it =
      state.character.equip_sets().find(key);
  if (it == state.character.equip_sets().end()) {
    return 0;
  }
  return state.character.PiecesWornOf(it->second);
}

// How many pieces that set holds once every one of them is written, which is
// what a count of what is worn is read against. Asked of the set rather than
// written here, so a piece added to it tomorrow is counted rather than
// overflowing a number nobody moved.
int SetSize(const GameState& state, const std::string& key) {
  std::map<std::string, EquipSet>::const_iterator it =
      state.character.equip_sets().find(key);
  return it == state.character.equip_sets().end()
             ? 0
             : it->second.complete_pieces();
}

// The weapon's stars, and the upgrade slots still open in it. -1 apiece for
// bare hands, which reads as a row with nothing in it rather than as zero.
std::pair<int, int> WeaponUpgrades(const GameState& state) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      state.character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it == state.character.equipped().end()) {
    return {-1, -1};
  }
  return {it->second.stars(),
          it->second.equip_state().remaining_upgrade_slots()};
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
  double meso_per_second = 0.0;
  bool died = false;
};

// What one kill of `mob` is worth in meso: what it drops, plus what its Etc
// drops fetch, which the climb sells at every level. The character's own meso
// bonus is left out -- it multiplies every map alike, and this is only ever
// read to rank them.
double MesoPerKill(const GameState& state, const Mob& mob) {
  double meso = ExpectedMesoPerKill(mob, /*item_drop_pct=*/0.0);
  for (const MobDrop& drop : mob.drops()) {
    if (!drop.has_item()) {
      continue;
    }
    std::map<std::string, ItemPrototype>::const_iterator it =
        state.items.find(drop.item());
    if (it != state.items.end()) {
      meso += drop.per_kill() * it->second.sell_price();
    }
  }
  return meso;
}

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
  double meso = 0.0;
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < horizon; elapsed += step) {
    sim.Advance(params, step);
    const std::vector<int64_t>& kills = sim.kills_this_step();
    for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
      exp += kills[i] * params.types[i].mob->exp();
      meso += kills[i] * MesoPerKill(state, *params.types[i].mob);
    }
    if (sim.died_this_step()) {
      probe.died = true;
      return probe;
    }
  }
  probe.exp_per_second = exp / horizon;
  probe.meso_per_second = meso / horizon;
  return probe;
}

// Moves the character to whichever map pays the most of what they are farming
// for, of the ones they survive. Leaves them where they are if every map kills
// them, which the give-up clock then catches.
void PickMapFor(GameState& state, const std::vector<std::string>& candidates,
                int beats, double step, bool for_meso) {
  std::string best;
  double best_rate = 0.0;
  for (const std::string& map : candidates) {
    Probe probe = ProbeMap(state, map, beats, step);
    double rate = for_meso ? probe.meso_per_second : probe.exp_per_second;
    if (probe.died || rate <= best_rate) {
      continue;
    }
    best_rate = rate;
    best = map;
  }
  if (!best.empty()) {
    state.current_map = best;
  }
}

// Climbing: the most EXP a second.
void PickMap(GameState& state, const std::vector<std::string>& candidates,
             int beats, double step) {
  PickMapFor(state, candidates, beats, step, /*for_meso=*/false);
}

// At the cap, where there is no EXP left to earn: the most meso a second.
void PickMoneyMap(GameState& state, const std::vector<std::string>& candidates,
                  int beats, double step) {
  PickMapFor(state, candidates, beats, step, /*for_meso=*/true);
}

// What the last scout settled on, and the level it was asked at.
struct WeaponScout {
  EquipType settled = EQUIP_TYPE_UNSPECIFIED;
  int settled_at = 0;
};

// Everything the player does on levelling up, in the order that makes each
// step pay for the next: the advancement first, then the points it hands over,
// then the drops turned into meso, then the weapon that meso buys, and only
// then the choice of where to take it.
// How many levels a settled weapon type is trusted for. The scout is the
// single most expensive thing a look does -- ten ladders, each swung for a
// simulated minute -- and the answer is a branch's whole identity: a Paladin
// does not stop being a Paladin between Lv61 and Lv65. It is re-asked at every
// advancement regardless, which is where it actually moves.
constexpr int kScoutEveryLevels = 5;

void Retool(GameState& state, const std::vector<Job>& path, int* taken,
            const std::vector<std::string>& maps, int beats, double step,
            Purse& purse, GearShopper& shopper, WeaponScout& scout) {
  if (state.character.CanAdvanceJob() &&
      *taken < static_cast<int>(path.size())) {
    Job job = path[(*taken)++];
    PerformJobAdvancement(state, job);
    scout.settled = EQUIP_TYPE_UNSPECIFIED;
    for (const std::string& key : StarterEquipsFor(job)) {
      std::map<std::string, EquipPrototype>::const_iterator it =
          state.equips.find(key);
      if (it != state.equips.end()) {
        EquipByName(state.character, it->second.name());
      }
    }
  }
  SpendPoints(state.character);
  SellDrops(state.character);
  purse.Note(state.character);
  // What fell goes on before what is bought, so the weapon measurement is
  // taken with the rest of the outfit already in place.
  WearBestFromBag(state.character);
  int level = state.character.proto().level();
  if (scout.settled == EQUIP_TYPE_UNSPECIFIED ||
      level - scout.settled_at >= kScoutEveryLevels) {
    scout.settled = SettledWeaponType(state, /*budget=*/true);
    scout.settled_at = level;
  }
  Outfit(state, /*budget=*/true, scout.settled);
  // The book after the weapon, since a point is worth what the thing in their
  // hands can swing -- and the weapon is settled on what the branch is for,
  // which is the only thing that keeps the two from talking each other into a
  // corner. See SettledWeaponType.
  SpendBookWithToggles(state, BookRate);
  LearnTheRest(state);
  // After the weapon, because a scroll on last tier's weapon is meso that
  // buys nothing: the next one displaces it slots and stars and all.
  shopper.Spend(state);
  purse.Note(state.character);
  PickMap(state, maps, beats, step);
}

// What one boss's dailies came to over a run.
struct BossLog {
  std::string name;
  int unlock_level = 0;
  int attempts = 0;
  int clears = 0;
  // The level the first try was taken at, which is not the unlock level: a
  // reset comes round once a day, and a climb can pass several levels inside
  // one.
  int first_attempt_level = 0;
  int first_clear_level = 0;  // 0 for a boss never beaten
  // The quickest clear: which difficulty it was, what it took, and what the
  // character was hitting for at the time. Zero for a boss never beaten.
  int best_difficulty = 0;
  double best_seconds = 0.0;
  int best_power = 0;
};

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
const char* const kFrozenPieces[] = {"Frozen Top",    "Frozen Bottom",
                                     "Frozen Hat",    "Frozen Cape",
                                     "Frozen Gloves", "Frozen Boots"};
constexpr int kNumFrozenPieces = 6;
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
  // Meso the character had been paid, all told, by each milestone, and what
  // the shop had taken back off them.
  int64_t milestone_meso[kNumMilestones] = {0};
  int64_t milestone_spent[kNumMilestones] = {0};
  // What they were standing in at each milestone: the weapon's stars and the
  // slots still open in it, and how much of each set was on their back.
  int milestone_stars[kNumMilestones] = {0};
  int milestone_slots[kNumMilestones] = {0};
  int milestone_frozen[kNumMilestones] = {0};
  int milestone_boss_set[kNumMilestones] = {0};
  // The honor the pool had been PAID by each milestone -- what it is holding
  // plus whatever has been rerolled away -- and the two parts of it that can
  // be named: what the levels paid and what the daily clears paid. Whatever is
  // left over is what the monsters dropped.
  int64_t milestone_honor[kNumMilestones] = {0};
  int64_t milestone_level_honor[kNumMilestones] = {0};
  int64_t milestone_boss_honor[kNumMilestones] = {0};
  // What the Inner Ability came to: the honor spent rerolling it, and the
  // lines each preset was holding when the run ended.
  int64_t ability_honor_spent = 0;
  AbilityPreset ability_farming;
  AbilityPreset ability_bossing;
  // What each preset rated the line types at, which is what the holding above
  // was decided on.
  AbilityWorth farming_worth;
  AbilityWorth bossing_worth;
  // The level the weapon first held ten stars at, and the level the Frozen Set
  // was first complete at. 0 for a climb that reached the cap without it.
  int ten_star_level = 0;
  int frozen_set_level = 0;
  // What the dailies came to, by boss key, in the order they unlock.
  std::map<std::string, BossLog> bosses;
  // Where the endgame left off: the days played past the cap, what they paid,
  // and what the character was standing in at the end of them.
  double endgame_seconds = 0.0;
  int64_t endgame_earned = 0;
  int64_t endgame_spent = 0;
  int endgame_stars = 0;
  int endgame_frozen = 0;
  // How many the set holds in all, which endgame_frozen is read against.
  int frozen_set_size = 0;
  int endgame_boss_set = 0;
  // Pieces worn that take an upgrade at all, how many of them have every slot
  // spent, and the stars over all of them -- where the shopper stopped, since
  // nothing told it where to.
  int endgame_pieces = 0;
  int endgame_scrolled = 0;
  int endgame_stars_worn = 0;
  int endgame_hammers = 0;
  std::string money_map;
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
void NoteTokenChances(const CombatParams& params,
                      const std::vector<int64_t>& kills, Climb& climb) {
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

// What the encounter in front of them pays a second, and what it kills.
struct Yield {
  std::vector<double> kills_per_second;  // parallel to CombatParams::types
  double exp_per_second = 0.0;
  bool died = false;
};

// Plays the current encounter out for a few respawn beats and banks none of
// it, then reports the rate it settled at.
//
// Nothing that decides that rate moves between two steps of the same level on
// the same map -- not the gear, not the book, not the roster -- so the rate
// holds until one of those does. That is what lets the climb be JUMPED rather
// than stepped: measure once, then hand the kills of a whole stretch to
// AwardCombatRewards in one go, which rolls each of them for drops exactly as
// a step would have.
//
// What it gives up is the drift inside a stretch: the character's HP wanders
// over an evening and this cannot see them die of it. The map they are on is
// one PickMap watched them survive, so it is a small thing to give up for
// sixty times the speed.
Yield MeasureYield(GameState& state, const CombatParams& params, int beats,
                   double step) {
  Yield yield;
  yield.kills_per_second.assign(params.types.size(), 0.0);
  if (!params.active || params.types.empty()) {
    yield.died = true;
    return yield;
  }
  double horizon = std::max(step, beats * params.respawn_seconds);
  std::vector<int64_t> total(params.types.size(), 0);
  double exp = 0.0;
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < horizon; elapsed += step) {
    sim.Advance(params, step);
    const std::vector<int64_t>& kills = sim.kills_this_step();
    for (std::size_t i = 0; i < params.types.size(); ++i) {
      total[i] += kills[i];
      exp += kills[i] * params.types[i].mob->exp();
    }
    if (sim.died_this_step()) {
      yield.died = true;
      return yield;
    }
  }
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    yield.kills_per_second[i] = total[i] / horizon;
  }
  yield.exp_per_second = exp * (1.0 + params.exp_pct) / horizon;
  return yield;
}

// The kills a stretch of `seconds` comes to. `carry` holds the fractions left
// over from the last stretch, so a mob killed once every two minutes is still
// killed thirty times an hour rather than rounded away at every jump.
std::vector<int64_t> KillsOver(const Yield& yield, double seconds,
                               std::vector<double>* carry) {
  carry->resize(yield.kills_per_second.size(), 0.0);
  std::vector<int64_t> kills(yield.kills_per_second.size(), 0);
  for (std::size_t i = 0; i < kills.size(); ++i) {
    double want = yield.kills_per_second[i] * seconds + (*carry)[i];
    kills[i] = static_cast<int64_t>(want);
    (*carry)[i] = want - kills[i];
  }
  return kills;
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

// Every daily clear the climb has taken so far, over every boss.
int64_t ClearsSoFar(const Climb& climb) {
  int64_t clears = 0;
  for (const std::pair<const std::string, BossLog>& entry : climb.bosses) {
    clears += entry.second.clears;
  }
  return clears;
}

// Writes down the playtime, the purse and what the character is wearing, at
// every milestone this level has just reached. Read after the level's
// shopping, so a milestone says what they walked away from it in rather than
// what they arrived in.
void NoteMilestones(const GameState& state, int level, double seconds,
                    const Purse& purse, Climb& climb) {
  std::pair<int, int> weapon = WeaponUpgrades(state);
  int frozen = PiecesWorn(state, "frozen");
  if (climb.ten_star_level == 0 && weapon.first >= 10) {
    climb.ten_star_level = level;
  }
  if (climb.frozen_set_level == 0 && frozen >= SetSize(state, "frozen")) {
    climb.frozen_set_level = level;
  }
  for (int i = 0; i < kNumMilestones; ++i) {
    if (level < kMilestones[i] || climb.milestone_seconds[i] >= 0.0) {
      continue;
    }
    climb.milestone_seconds[i] = seconds;
    climb.milestone_meso[i] = purse.earned;
    climb.milestone_spent[i] = purse.spent;
    climb.milestone_stars[i] = weapon.first;
    climb.milestone_slots[i] = weapon.second;
    climb.milestone_frozen[i] = frozen;
    climb.milestone_boss_set[i] = PiecesWorn(state, "boss_accessory");
    // Paid, not held: the pool has one sink and this is the only thing that
    // spends it, so what went out of it is known exactly.
    climb.milestone_honor[i] =
        state.character.honor() + climb.ability_honor_spent;
    climb.milestone_level_honor[i] = HonorForLevels(1, level);
    climb.milestone_boss_honor[i] = kBossClearHonor * ClearsSoFar(climb);
  }
}

// Fights one boss once and reports what it took off the clock. A run that
// misses pays nothing and still costs whatever the player sat through.
double FightOnce(GameState& state, const std::pair<std::string, int>& fight,
                 int level, int power, Climb& climb, BossOutcome* result) {
  const BossDifficulty& difficulty =
      state.bosses[fight.first].difficulties(fight.second);
  BossLog& log = climb.bosses[fight.first];
  if (log.attempts == 0) {
    log.name = state.bosses[fight.first].name();
    log.unlock_level = difficulty.unlock_level();
    log.first_attempt_level = level;
  }
  ++log.attempts;
  BossOutcome outcome = FightBoss(state, fight.first, fight.second);
  *result = outcome;
  if (!outcome.won) {
    return outcome.seconds;
  }
  ++log.clears;
  if (log.first_clear_level == 0) {
    log.first_clear_level = level;
  }
  if (log.best_seconds == 0.0 || outcome.seconds < log.best_seconds) {
    log.best_difficulty = fight.second;
    log.best_seconds = outcome.seconds;
    log.best_power = power;
  }
  return outcome.seconds;
}

// How often the player opens the game, and when.
//
// Not a metronome. A day is a handful of SESSIONS -- a look before work, a
// long gap, an evening spent checking every half hour -- and both the number
// of looks and where they fall thin out as the climb slows. An hour between
// looks at Lv30 is a level and a half; at Lv150 it is nothing.
//
// The character farms throughout either way. This decides only when the PLAYER
// is there to sell what dropped, spend the purse, move map, take the
// advancement and walk up to a boss.
constexpr double kLookGap = 30.0 * 60.0;  // between looks inside one session

// Looks a day, by level, interpolated between. A new character is watched; one
// grinding out the last forty levels is checked morning and evening.
struct LookAnchor {
  int level;
  double per_day;
};
constexpr LookAnchor kAttention[] = {{1, 48.0},  {30, 24.0}, {60, 12.0},
                                     {100, 5.0}, {150, 2.0}, {200, 2.0}};
constexpr int kNumAttention = sizeof(kAttention) / sizeof(kAttention[0]);

double LooksPerDay(int level) {
  double scale = absl::GetFlag(FLAGS_attention);
  if (kAttention[0].level >= level) {
    return kAttention[0].per_day * scale;
  }
  for (int i = 1; i < kNumAttention; ++i) {
    if (level > kAttention[i].level) {
      continue;
    }
    const LookAnchor& lo = kAttention[i - 1];
    const LookAnchor& hi = kAttention[i];
    double t = static_cast<double>(level - lo.level) / (hi.level - lo.level);
    return (lo.per_day + t * (hi.per_day - lo.per_day)) * scale;
  }
  return kAttention[kNumAttention - 1].per_day * scale;
}

// When they next open it. One look starts the day and the rest come together
// in an evening, half an hour apart -- twelve looks a day is not one every two
// hours, it is one at breakfast and eleven after work.
//
// `left` counts what the evening still owes, so a run of looks is a burst
// followed by a gap rather than an even spread.
double NextLook(double now, int level, int* left, std::mt19937& rng) {
  if (*left > 0) {
    --*left;
    return now + kLookGap;
  }
  double looks = std::max(1.0, LooksPerDay(level));
  *left = static_cast<int>(looks) - 1;
  double burst = *left * kLookGap;
  double gap = std::max(kLookGap, kDaySeconds / looks * (looks - *left));
  if (burst + gap > kDaySeconds) {
    gap = std::max(kLookGap, kDaySeconds - burst);
  }
  // Jittered so two runs of a branch do not open the game at the same moment
  // all climb.
  std::uniform_real_distribution<double> jitter(0.75, 1.25);
  return now + gap * jitter(rng);
}

// Where one fight stands with the player: whether today's clear is banked,
// and what they are waiting on before the next attempt.
struct FightState {
  bool attempted = false;
  bool cleared_today = false;
  // Whether the last loss was close. Only a near miss is walked back into on
  // the clock alone; see WorthATry.
  bool near_miss = false;
  double retry_at = 0.0;
  int power_at_last_try = 0;
};

// Everything one run of one branch carries while it is played. Held together
// because the two halves -- the climb and the days after it -- differ only in
// what they are farming for and when they stop.
struct Session {
  GameState& state;
  const std::vector<std::string>& maps;
  std::vector<Job> path;
  int taken = 0;
  Purse purse;
  GearShopper shopper;
  WeaponScout scout;
  Climb& climb;
  double step = 0.5;
  int beats = 4;
  // Playtime so far, and when the next reset falls due.
  double seconds = 0.0;
  double next_daily = kDaySeconds;
  // When the player next opens the game, and how many looks the evening still
  // owes. Seeded off the run so two climbs of a branch differ.
  double next_look = 0.0;
  int looks_left = 0;
  std::mt19937 rng;
  // Where each fight stands with them, by boss key.
  std::map<std::string, FightState> fights;
  // What a line of each type and rank is worth to them, measured the first
  // time they stand where the Ability opens.
  AbilityWorth farming_worth;
  AbilityWorth bossing_worth;
  bool ability_measured = false;
};

// What the upgradeable half of what is worn came to: how many pieces take an
// upgrade at all, how many have every slot spent, and the stars over all of
// them. Reported rather than held against a target, because the shopper has
// none -- where it stopped is the answer, not the question.
struct GearReached {
  int pieces = 0;
  int scrolled = 0;
  int stars = 0;
  int hammers = 0;
};

GearReached ReachedOnGear(const GameState& state) {
  GearReached reached;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    const EquipInstance& item = entry.second;
    bool takes_star =
        Supports(item.prototype(), UPGRADE_STAR_FORCE) && item.max_stars() > 0;
    bool takes_scroll = Supports(item.prototype(), UPGRADE_SCROLL) &&
                        item.prototype().upgrade_slots() > 0;
    if (!takes_scroll && !takes_star) {
      continue;  // an off-hand or a pocket, which takes neither
    }
    ++reached.pieces;
    reached.stars += item.stars();
    reached.hammers += item.equip_state().hammers();
    if (item.equip_state().remaining_upgrade_slots() == 0) {
      ++reached.scrolled;
    }
  }
  return reached;
}

// Runs the dailies if one is due, and puts what they dropped on. Returns
// whether anything happened, since the fight parameters have to be rebuilt
// when it did.
// The honor the pool has collected, spent on rerolling both Inner Ability
// setups. Nothing happens before level 160, where the panel opens.
//
// What a line is worth is measured once, the first time the character stands
// there: it is the ORDER of the types that the holding reads, and a branch's
// order does not move under it over the forty levels it has left.
void SpendHonor(Session& run) {
  if (!run.state.character.inner_ability_unlocked()) {
    return;
  }
  if (!run.ability_measured) {
    run.farming_worth =
        MeasureAbilityWorth(run.state, StatPreset::kFarming, CrowdRate);
    run.bossing_worth =
        MeasureAbilityWorth(run.state, StatPreset::kBossing, BossRate);
    run.ability_measured = true;
    run.climb.farming_worth = run.farming_worth;
    run.climb.bossing_worth = run.bossing_worth;
  }
  run.climb.ability_honor_spent += SpendHonorOnAbility(
      run.state, AbilityRankWanted(), run.farming_worth, run.bossing_worth);
}

// What a fight leaves behind: the drops worn, and the purse spent on them.
void AfterFighting(Session& run) {
  WearBestFromBag(run.state.character);
  run.purse.Note(run.state.character);
  run.shopper.Spend(run.state);
  run.purse.Note(run.state.character);
}

// How much of a fight has to be left standing before its loser gives up on
// the day. A near miss is another twenty seconds of damage away, so they stay
// and go again; a rout waits for something to change.
constexpr double kNearMiss = 0.25;
// How long they wait before that second go, and how much stronger they have to
// have got for a loss to be worth revisiting without one.
constexpr double kRetrySeconds = 30.0 * 60.0;
constexpr double kRetryPowerGain = 1.05;

int PowerNow(const GameState& state) {
  const Character& proto = state.character.proto();
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  return CombatPower(
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      TotalEquipStats(state.character, derived),
                      state.character.weapon_type(), /*attack_skill=*/nullptr,
                      /*attack_level=*/0, PassiveOffenseFor(derived)),
      /*vs_boss=*/true);
}

// Whether this fight is worth walking up to now. It is on the day it opens,
// and after that only once the loss has something new behind it: a level, a
// fifth again the damage, or the twenty minutes it takes to believe the last
// one was bad luck.
bool WorthATry(const Session& run, const FightState& fight, bool levelled,
               int power) {
  if (fight.cleared_today) {
    return false;
  }
  if (!fight.attempted) {
    return true;
  }
  if (levelled || power >= fight.power_at_last_try * kRetryPowerGain) {
    return true;
  }
  // The clock alone is a reason only after a near miss. A rout walked back
  // into every half hour is a fight nobody plays and, since a loss costs the
  // whole of the limit, most of what this sim used to spend its time on.
  return fight.near_miss && run.seconds >= fight.retry_at;
}

// Takes on every fight that is open and worth a try, and puts what they
// dropped on. Returns whether any of them was fought.
//
// A loss costs the clock and nothing else: the day is spent by BEATING a
// boss, not by walking into it, so a player who misses goes again rather than
// waiting for the reset. That is what a player does, and waiting for the reset
// is what used to put every first clear far above the level the fight opened
// at.
bool TakeOnBosses(Session& run, int level, bool levelled) {
  if (!absl::GetFlag(FLAGS_dailies)) {
    return false;
  }
  if (run.seconds >= run.next_daily) {
    run.next_daily += kDaySeconds;
    for (std::pair<const std::string, FightState>& entry : run.fights) {
      entry.second.cleared_today = false;
    }
  }
  int power = PowerNow(run.state);
  double spent = 0.0;
  for (const std::pair<std::string, int>& open :
       UnlockedBosses(run.state, level)) {
    FightState& fight = run.fights[open.first];
    if (!WorthATry(run, fight, levelled, power)) {
      continue;
    }
    fight.attempted = true;
    fight.power_at_last_try = power;
    BossOutcome outcome;
    spent += FightOnce(run.state, open, level, power, run.climb, &outcome);
    if (outcome.won) {
      fight.cleared_today = true;
      continue;
    }
    fight.retry_at = run.seconds + spent + kRetrySeconds;
    fight.near_miss = outcome.left <= kNearMiss;
    // A near miss keeps them at the keyboard, so the next look comes forward
    // to meet it rather than waiting for the evening.
    if (fight.near_miss) {
      run.next_look = std::min(run.next_look, fight.retry_at);
    }
  }
  if (spent <= 0.0) {
    return false;
  }
  run.seconds += spent;
  AfterFighting(run);
  return true;
}

// Plays the character forward to the level cap, or until the give-up clock
// runs out.
void ClimbToCap(Session& run) {
  double give_up = absl::GetFlag(FLAGS_give_up_hours) * 3600.0;
  Retool(run.state, run.path, &run.taken, run.maps, run.beats, run.step,
         run.purse, run.shopper, run.scout);
  SpendHonor(run);
  run.next_look = NextLook(run.seconds, run.state.character.proto().level(),
                           &run.looks_left, run.rng);

  int level = run.state.character.proto().level();
  double level_began = 0.0;
  Stint stint = {level, 0.0, run.state.current_map,
                 HeldWeaponName(run.state.character)};
  CombatParams params = ComputeCombatParams(run.state);
  std::vector<double> carry;
  while (level < kTrialLevelCap && run.seconds < give_up) {
    Yield yield = MeasureYield(run.state, params, run.beats, run.step);
    if (yield.died || yield.exp_per_second <= 0.0) {
      // Nowhere they can stand, or nothing to be earned standing there. Ask
      // for a map again and, if there is still none, let the give-up clock
      // have it rather than spinning.
      PickMap(run.state, run.maps, run.beats, run.step);
      CombatParams again = ComputeCombatParams(run.state);
      if (again.encounter == params.encounter) {
        run.seconds = give_up;
        break;
      }
      params = again;
      continue;
    }
    // The next thing that changes anything: the level they are about to make,
    // or the moment the player next opens it. Nothing else moves in between.
    const Character& proto = run.state.character.proto();
    double to_level =
        (ExpToNextLevel(level) - proto.exp()) / yield.exp_per_second;
    double horizon = std::min(std::max(to_level, run.step),
                              std::max(run.next_look - run.seconds, run.step));
    horizon = std::min(horizon, give_up - run.seconds);
    std::vector<int64_t> kills = KillsOver(yield, horizon, &carry);
    AwardCombatRewards(run.state, params, kills);
    NoteTokenChances(params, kills, run.climb);
    run.purse.Note(run.state.character);
    run.seconds += horizon;

    int reached = run.state.character.proto().level();
    bool levelled = reached != level;
    bool watching = absl::GetFlag(FLAGS_attention) <= 0.0;
    bool looked = false;
    if (watching ? levelled : run.seconds >= run.next_look) {
      if (!watching) {
        run.next_look =
            NextLook(run.seconds, reached, &run.looks_left, run.rng);
      }
      Retool(run.state, run.path, &run.taken, run.maps, run.beats, run.step,
             run.purse, run.shopper, run.scout);
      SpendHonor(run);
      looked = true;
    }
    if (levelled) {
      stint.seconds = run.seconds - level_began;
      run.climb.stints.push_back(stint);
      level_began = run.seconds;
      level = reached;
      NoteFrozenDrops(run.state, level, run.climb);
      NoteMilestones(run.state, level, run.seconds, run.purse, run.climb);
      stint = {level, 0.0, run.state.current_map,
               HeldWeaponName(run.state.character)};
      // The character got stronger whether or not anybody was watching, and
      // the fight is built off what they are.
      looked = true;
    }
    // After retooling, so a fight is taken on in the gear this look bought
    // rather than in what the last one left.
    if (looked && TakeOnBosses(run, reached, levelled)) {
      PickMap(run.state, run.maps, run.beats, run.step);
    }
    if (looked) {
      params = ComputeCombatParams(run.state);
      stint.map = run.state.current_map;
      stint.weapon = HeldWeaponName(run.state.character);
    }
  }
}

// Plays the days after the cap: the map that pays the most meso a second
// rather than the most EXP, the dailies as they fall due, and the purse still
// spending on whatever it can now afford.
void FarmAtCap(Session& run) {
  double began = run.seconds;
  double horizon = began + absl::GetFlag(FLAGS_endgame_days) * kDaySeconds;
  int level = run.state.character.proto().level();
  int64_t earned_at_cap = run.purse.earned;
  int64_t spent_at_cap = run.purse.spent;

  PickMoneyMap(run.state, run.maps, run.beats, run.step);
  run.climb.money_map = run.state.current_map;
  CombatParams params = ComputeCombatParams(run.state);
  // Retooled on the clock rather than on levelling up, since nothing levels
  // any more: often enough that a star bought is felt, rarely enough that the
  // measurement behind it is not the whole cost of the section.
  double next_retool = run.seconds + kDaySeconds / 24.0;
  std::vector<double> carry;
  while (run.seconds < horizon) {
    Yield yield = MeasureYield(run.state, params, run.beats, run.step);
    if (yield.died) {
      PickMoneyMap(run.state, run.maps, run.beats, run.step);
      CombatParams again = ComputeCombatParams(run.state);
      if (again.encounter == params.encounter) {
        break;  // nowhere left they can stand
      }
      params = again;
      continue;
    }
    // Jumped the same way the climb is, but to the retool rather than to a
    // level: nothing levels here, so the clock is the only thing that moves.
    double jump = std::min(next_retool, horizon) - run.seconds;
    jump = std::max(jump, run.step);
    std::vector<int64_t> kills = KillsOver(yield, jump, &carry);
    AwardCombatRewards(run.state, params, kills);
    run.purse.Note(run.state.character);
    run.seconds += jump;
    bool fought = TakeOnBosses(run, level, /*levelled=*/false);
    if (run.seconds >= next_retool) {
      next_retool += kDaySeconds / 24.0;
      WearBestFromBag(run.state.character);
      Outfit(run.state, /*budget=*/true);
      run.shopper.Spend(run.state);
      run.purse.Note(run.state.character);
      SpendHonor(run);
      PickMoneyMap(run.state, run.maps, run.beats, run.step);
      run.climb.money_map = run.state.current_map;
      fought = true;
    }
    if (fought) {
      params = ComputeCombatParams(run.state);
    }
  }
  std::pair<int, int> weapon = WeaponUpgrades(run.state);
  run.climb.endgame_seconds = run.seconds - began;
  run.climb.endgame_earned = run.purse.earned - earned_at_cap;
  run.climb.endgame_spent = run.purse.spent - spent_at_cap;
  run.climb.endgame_stars = weapon.first;
  run.climb.endgame_frozen = PiecesWorn(run.state, "frozen");
  run.climb.frozen_set_size = SetSize(run.state, "frozen");
  run.climb.endgame_boss_set = PiecesWorn(run.state, "boss_accessory");
  GearReached reached = ReachedOnGear(run.state);
  run.climb.endgame_pieces = reached.pieces;
  run.climb.endgame_scrolled = reached.scrolled;
  run.climb.endgame_stars_worn = reached.stars;
  run.climb.endgame_hammers = reached.hammers;
}

Climb Play(const Catalogs& catalogs, Job branch,
           const std::vector<std::string>& maps, unsigned int seed) {
  GameState state = NewState(catalogs, seed);
  // A plain field rather than something the constructor takes, so a sim that
  // fights one has to say so. This one runs the dailies.
  state.bosses = catalogs.bosses;
  Climb climb;
  for (int i = 0; i < kNumMilestones; ++i) {
    climb.milestone_seconds[i] = -1.0;
  }
  GearPlan plan;
  plan.star_ceiling = absl::GetFlag(FLAGS_star_ceiling);
  plan.scroll_rate = absl::GetFlag(FLAGS_scroll_rate);

  Session run = {
      state,         maps, PathTo(branch), 0, Purse(), GearShopper(plan),
      WeaponScout(), climb};
  run.step = absl::GetFlag(FLAGS_step);
  run.beats = absl::GetFlag(FLAGS_probe_beats);
  run.rng.seed(seed);
  ClimbToCap(run);
  if (absl::GetFlag(FLAGS_endgame) &&
      state.character.proto().level() >= kTrialLevelCap) {
    FarmAtCap(run);
  }
  climb.ability_farming = state.character.ability(StatPreset::kFarming);
  climb.ability_bossing = state.character.ability(StatPreset::kBossing);
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

// A fight's own length, which is minutes and seconds rather than the hours a
// playtime is read in.
std::string FightClock(double seconds) {
  int whole = static_cast<int>(seconds + 0.5);
  char text[32];
  std::snprintf(text, sizeof(text), "%d:%02d", whole / 60, whole % 60);
  return text;
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

// Playtime to each milestone, one row a branch.
void PrintPlaytime(const Catalogs& catalogs, const std::vector<Job>& branches,
                   const std::vector<Climb>& climbs) {
  std::printf(
      "Playtime to each level, played at the best map the character survives "
      "and the best weapon the shop will sell them.\n\n");
  std::printf("%-13s", "branch");
  for (int level : kMilestones) {
    std::printf("  %8s", ("Lv" + std::to_string(level)).c_str());
  }
  std::printf("\n%s\n", std::string(13 + 10 * kNumMilestones, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      std::printf("  %8s", Clock(climbs[i].milestone_seconds[m]).c_str());
    }
    std::printf("\n");
    if (absl::GetFlag(FLAGS_detail)) {
      PrintDetail(catalogs, climbs[i]);
    }
  }
}

// Everything the climb was paid by each milestone. Income, not the balance:
// what the shop took is spent on the weapon that made the rest of it faster.
void PrintMeso(const std::vector<Job>& branches,
               const std::vector<Climb>& climbs) {
  std::printf(
      "\nMeso earned by each level -- kills and the Etc tab together, before "
      "the shop takes any of it.\n\n");
  std::printf("%-13s", "branch");
  for (int level : kMilestones) {
    std::printf("  %8s", ("Lv" + std::to_string(level)).c_str());
  }
  std::printf("\n%s\n", std::string(13 + 10 * kNumMilestones, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      char money[16];
      FormatShort(static_cast<double>(climbs[i].milestone_meso[m]), money,
                  sizeof(money));
      std::printf("  %8s", climbs[i].milestone_seconds[m] < 0.0 ? "-" : money);
    }
    std::printf("\n");
  }
}

// The pool at each milestone, and where it came from at the two levels the
// question is asked at: 160, where Inner Ability opens, and the cap.
void PrintHonor(const std::vector<Job>& branches,
                const std::vector<Climb>& climbs) {
  std::printf(
      "\nHonor paid by each level, whether or not it is still in the pool. "
      "Nothing earns it faster\nand nothing but a reset spends it, so this is "
      "the whole of what the Ability is bought with.\n\n");
  std::printf("%-13s", "branch");
  for (int level : kMilestones) {
    std::printf("  %8s", ("Lv" + std::to_string(level)).c_str());
  }
  std::printf("\n%s\n", std::string(13 + 10 * kNumMilestones, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      char honor[16];
      FormatShort(static_cast<double>(climbs[i].milestone_honor[m]), honor,
                  sizeof(honor));
      std::printf("  %8s", climbs[i].milestone_seconds[m] < 0.0 ? "-" : honor);
    }
    std::printf("\n");
  }

  std::printf(
      "\nWhere it came from. The levels pay the same to everybody, so the "
      "spread is the dailies a\nclimb was strong enough to take and the kills "
      "it took to get there.\n\n");
  std::printf("%-13s", "branch");
  for (int m = 0; m < kNumMilestones; ++m) {
    if (kMilestones[m] != 160 && kMilestones[m] != 200) {
      continue;
    }
    std::printf("  %9s %9s %9s %9s",
                ("at Lv" + std::to_string(kMilestones[m])).c_str(), "levels",
                "bosses", "mobs");
  }
  std::printf("\n%s\n", std::string(13 + 2 * 4 * 10, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      if (kMilestones[m] != 160 && kMilestones[m] != 200) {
        continue;
      }
      const Climb& climb = climbs[i];
      if (climb.milestone_seconds[m] < 0.0) {
        std::printf("  %9s %9s %9s %9s", "-", "-", "-", "-");
        continue;
      }
      int64_t mobs = climb.milestone_honor[m] - climb.milestone_level_honor[m] -
                     climb.milestone_boss_honor[m];
      std::printf("  %9lld %9lld %9lld %9lld",
                  static_cast<long long>(climb.milestone_honor[m]),
                  static_cast<long long>(climb.milestone_level_honor[m]),
                  static_cast<long long>(climb.milestone_boss_honor[m]),
                  static_cast<long long>(mobs));
    }
    std::printf("\n");
  }
}

// How a line reads on a table: what it is, its rank as an initial, and what it
// pays -- flat for the stats and the attacks, whole percents for the rest.
std::string AbilityLineText(const AbilityLine& line) {
  std::string type =
      absl::AsciiStrToLower(AbilityLineType_Name(line.type())
                                .substr(std::strlen("ABILITY_LINE_TYPE_")));
  char text[64];
  std::snprintf(text, sizeof(text), "%s(%c) %d", type.c_str(),
                AbilityRank_Name(line.rank())[std::strlen("ABILITY_RANK_")],
                AbilityLineValue(line.type(), line.rank()));
  return text;
}

// The line types this preset rated highest, best first. Read at Unique, where
// every type but Attack Speed rolls, so the order is one list rather than four.
std::vector<std::string> RatedTypes(const AbilityWorth& worth, int most) {
  std::vector<std::pair<double, std::string>> ranked;
  for (int t = ABILITY_LINE_TYPE_STR; t < AbilityLineType_ARRAYSIZE; ++t) {
    double rate = worth.rate[t][ABILITY_RANK_UNIQUE];
    if (rate <= 0.0) {
      continue;
    }
    ranked.push_back(
        {-rate, absl::AsciiStrToLower(
                    AbilityLineType_Name(static_cast<AbilityLineType>(t))
                        .substr(std::strlen("ABILITY_LINE_TYPE_")))});
  }
  std::sort(ranked.begin(), ranked.end());
  std::vector<std::string> names;
  for (int i = 0; i < static_cast<int>(ranked.size()) && i < most; ++i) {
    names.push_back(ranked[i].second);
  }
  return names;
}

// One preset: the rank the ability climbed to and the lines it ended up
// holding, over what it was aiming at.
void PrintAbilityRow(const char* label, const AbilityPreset& preset,
                     const AbilityWorth& worth) {
  std::string rank = absl::AsciiStrToLower(
      AbilityRank_Name(preset.rank()).substr(std::strlen("ABILITY_RANK_")));
  std::printf("  %-9s %-10s", label, rank.c_str());
  for (const AbilityLine& line : preset.lines()) {
    std::printf("  %-22s", AbilityLineText(line).c_str());
  }
  std::printf("\n  %-9s %-10s", "", "rated");
  for (const std::string& name : RatedTypes(worth, 3)) {
    std::printf("  %-22s", name.c_str());
  }
  std::printf("\n");
}

// What the honor was spent on. The pool pays for nothing else, so this is the
// whole of what a climb has to show for every level, boss and monster that
// ever paid it.
void PrintAbility(const std::vector<Job>& branches,
                  const std::vector<Climb>& climbs) {
  std::printf(
      "\nThe Inner Ability each climb rolled itself into. What a line is "
      "worth is measured on the\ncharacter who rolls for it -- the crowd for "
      "farming, the fight for bossing -- and the pool is\nspent holding the "
      "best two of them once the ability has climbed to %s.\n",
      absl::GetFlag(FLAGS_ability_rank).c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    const Climb& climb = climbs[i];
    char honor[16];
    FormatShort(static_cast<double>(climb.ability_honor_spent), honor,
                sizeof(honor));
    std::printf("\n%s, %s honor spent\n", BranchName(branches[i]).c_str(),
                honor);
    PrintAbilityRow("farming", climb.ability_farming, climb.farming_worth);
    PrintAbilityRow("bossing", climb.ability_bossing, climb.bossing_worth);
  }
}

// Where the Frozen Set came from, for the branches that climb far enough to
// see it.
void PrintFrozenDrops(const std::vector<Job>& branches,
                      const std::vector<Climb>& climbs) {
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
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
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
}

// One row of the ledger: what the branch had earned and spent by `level`, and
// what it was standing in when it left.
void PrintLedgerRow(const Climb& climb, int m) {
  char earned[16];
  char spent[16];
  FormatShort(static_cast<double>(climb.milestone_meso[m]), earned,
              sizeof(earned));
  FormatShort(static_cast<double>(climb.milestone_spent[m]), spent,
              sizeof(spent));
  char weapon[16];
  if (climb.milestone_stars[m] < 0) {
    std::snprintf(weapon, sizeof(weapon), "%s", "-");
  } else {
    std::snprintf(weapon, sizeof(weapon), "%d*/%d open",
                  climb.milestone_stars[m], climb.milestone_slots[m]);
  }
  std::printf("  %5d  %9s  %9s  %13s  %8d  %10d\n", kMilestones[m], earned,
              spent, weapon, climb.milestone_frozen[m],
              climb.milestone_boss_set[m]);
}

// What the purse went on and what the branch has to show for it.
void PrintLedger(const std::vector<Job>& branches,
                 const std::vector<Climb>& climbs) {
  std::printf(
      "\nWhat each climb earned, what the shop took back, and what they were "
      "standing in when they\nleft the level. The weapon column is its stars "
      "and the upgrade slots still open in it.\n");
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    std::printf("\n%s\n", BranchName(branches[i]).c_str());
    std::printf("  %5s  %9s  %9s  %13s  %8s  %10s\n", "level", "earned",
                "spent", "weapon", "Frozen", "Boss acc.");
    std::printf("  %s\n", std::string(62, '-').c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      if (climbs[i].milestone_seconds[m] >= 0.0) {
        PrintLedgerRow(climbs[i], m);
      }
    }
    std::printf(
        "  weapon reached 10 stars: %s      Frozen Set complete: %s\n",
        climbs[i].ten_star_level == 0
            ? "never"
            : ("Lv" + std::to_string(climbs[i].ten_star_level)).c_str(),
        climbs[i].frozen_set_level == 0
            ? "never"
            : ("Lv" + std::to_string(climbs[i].frozen_set_level)).c_str());
  }
}

// One boss's row: what the quickest clear took, against what the fight allows.
void PrintReadinessRow(const Catalogs& catalogs, const BossLog& log,
                       const std::string& key) {
  std::map<std::string, Boss>::const_iterator boss = catalogs.bosses.find(key);
  if (boss == catalogs.bosses.end()) {
    return;
  }
  const BossDifficulty& difficulty =
      boss->second.difficulties(log.best_difficulty);
  int clock = difficulty.time_limit_seconds();
  char rate[16];
  FormatShort(BossTotalHp(catalogs.mobs, difficulty) / log.best_seconds, rate,
              sizeof(rate));
  std::printf("  %-18s %8s %9s %7.0f%% %10s %9d\n",
              (difficulty.name() + " " + boss->second.name()).c_str(),
              FightClock(log.best_seconds).c_str(), FightClock(clock).c_str(),
              100.0 * log.best_seconds / clock, rate, log.best_power);
}

// How much of each fight's clock the branch needed once it could take it at
// all. A boss is beaten inside its limit or not beaten, so the margin is the
// reading -- a clear at nine tenths of the clock is a fight the next patch
// takes away.
//
// Read off the climb rather than off a character built for the question: what
// walks up to a boss here has the AP, the book and the gear a player actually
// had when the fight opened.
void PrintBossReadiness(const Catalogs& catalogs,
                        const std::vector<Job>& branches,
                        const std::vector<Climb>& climbs) {
  std::printf(
      "\nWhat the quickest clear of each fight took, against the clock it "
      "allows. A boss never beaten\nis left out; the table above says which "
      "those were.\n");
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    std::printf("\n%s\n", BranchName(branches[i]).c_str());
    std::printf("  %-18s %8s %9s %8s %10s %9s\n", "fight", "best", "clock",
                "of it", "HP/s", "CP");
    std::printf("  %s\n", std::string(67, '-').c_str());
    for (const std::pair<const std::string, BossLog>& entry :
         climbs[i].bosses) {
      if (entry.second.best_seconds > 0.0) {
        PrintReadinessRow(catalogs, entry.second, entry.first);
      }
    }
  }
}

// The branches to climb: the ones that take a 4th advancement, or under
// --all_branches every branch from the 2nd job up, or the one --branch names.
// A 1st job is left out even then -- climbing to 140 without ever advancing
// takes forty simulated days and answers nothing.
std::vector<Job> BranchesToClimb() {
  const std::string& name = absl::GetFlag(FLAGS_branch);
  if (!name.empty()) {
    return {ParseBranch(name)};
  }
  int deepest = absl::GetFlag(FLAGS_all_branches) ? 2 : 4;
  std::vector<Job> wanted;
  for (Job branch : EveryBranch()) {
    if (StageOf(branch) >= deepest) {
      wanted.push_back(branch);
    }
  }
  return wanted;
}

// The run of a branch the tables read: the one whose climb to the cap took
// the middling time. An average of several climbs is a climb nobody played,
// and the tables are meant to read as one character's.
int TypicalRun(const std::vector<Climb>& runs) {
  std::vector<std::pair<double, int>> by_time;
  for (int i = 0; i < static_cast<int>(runs.size()); ++i) {
    by_time.push_back({runs[i].milestone_seconds[kNumMilestones - 1], i});
  }
  std::sort(by_time.begin(), by_time.end());
  return by_time[by_time.size() / 2].second;
}

// One line: how many of `runs` met a target, and where the typical one met it.
void PrintTarget(const std::string& label, int met, int total,
                 const std::string& typical) {
  std::printf("  %-46s %2d/%-2d runs   %s\n", label.c_str(), met, total,
              typical.c_str());
}

// Whether the character can be paid for the gear the cap asks of them, stated
// against the targets rather than left in the tables to be read off.
void PrintTargets(const std::vector<Job>& branches,
                  const std::vector<std::vector<Climb>>& runs) {
  std::printf(
      "\nWhat each branch reached, over every run of it. The last column is "
      "the typical run --\nthe one whose climb to the cap took the middling "
      "time.\n");
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    const std::vector<Climb>& all = runs[i];
    const Climb& typical = all[TypicalRun(all)];
    int total = static_cast<int>(all.size());
    std::printf("\n%s\n", BranchName(branches[i]).c_str());

    int ten_star = 0;
    int frozen = 0;
    for (const Climb& climb : all) {
      ten_star += climb.ten_star_level > 0 ? 1 : 0;
      frozen += climb.frozen_set_level > 0 ? 1 : 0;
    }
    PrintTarget("weapon at 10 stars by the cap", ten_star, total,
                typical.ten_star_level == 0
                    ? "never"
                    : "Lv" + std::to_string(typical.ten_star_level));
    PrintTarget(
        "Frozen Set complete by the cap", frozen, total,
        typical.frozen_set_level == 0
            ? std::to_string(typical.milestone_frozen[kNumMilestones - 1]) +
                  " of " + std::to_string(typical.frozen_set_size)
            : "Lv" + std::to_string(typical.frozen_set_level));

    // A boss beaten on the day it opened is the question; one beaten later is
    // a player who came back, which every branch manages eventually.
    for (const std::pair<const std::string, BossLog>& entry : typical.bosses) {
      int beaten = 0;
      for (const Climb& climb : all) {
        std::map<std::string, BossLog>::const_iterator log =
            climb.bosses.find(entry.first);
        beaten += log != climb.bosses.end() && log->second.clears > 0 ? 1 : 0;
      }
      std::string when =
          entry.second.first_clear_level == 0
              ? "never, " + std::to_string(entry.second.attempts) + " tries"
              : "Lv" + std::to_string(entry.second.first_clear_level) + ", " +
                    std::to_string(entry.second.clears) + " of " +
                    std::to_string(entry.second.attempts);
      PrintTarget(entry.second.name + " (opens Lv" +
                      std::to_string(entry.second.unlock_level) +
                      ", first try Lv" +
                      std::to_string(entry.second.first_attempt_level) + ")",
                  beaten, total, when);
    }
    if (typical.endgame_seconds > 0.0) {
      char earned[16];
      FormatShort(static_cast<double>(typical.endgame_earned), earned,
                  sizeof(earned));
      std::printf(
          "  %-46s %-12s %d* weapon, %d of %d Frozen, %d Boss acc.\n",
          ("after " + Clock(typical.endgame_seconds) + " at the cap").c_str(),
          (std::string(earned) + " earned").c_str(), typical.endgame_stars,
          typical.endgame_frozen, typical.frozen_set_size,
          typical.endgame_boss_set);
      char spent[16];
      FormatShort(static_cast<double>(typical.endgame_spent), spent,
                  sizeof(spent));
      std::printf(
          "  %-46s %-12s %d of %d pieces scrolled out, %.1f* mean, %d "
          "hammers\n",
          "  spent on gear", spent, typical.endgame_scrolled,
          typical.endgame_pieces,
          typical.endgame_pieces == 0
              ? 0.0
              : static_cast<double>(typical.endgame_stars_worn) /
                    typical.endgame_pieces,
          typical.endgame_hammers);
      std::printf("  %-46s %s\n", "  farmed", typical.money_map.c_str());
    }
  }
}

void Run() {
  Catalogs catalogs = LoadCatalogs();
  std::vector<std::string> maps = HuntingGrounds(catalogs);
  std::vector<Job> branches = BranchesToClimb();
  int per_branch = std::max(1, absl::GetFlag(FLAGS_runs));

  // Every run climbs on its own character, so they all go at once. The rows
  // are printed afterwards, in the table's own order rather than the order the
  // threads happened to finish.
  int count = static_cast<int>(branches.size());
  std::vector<std::vector<Climb>> runs(count, std::vector<Climb>(per_branch));
  ParallelFor(count * per_branch, [&](int i) {
    unsigned int seed =
        static_cast<unsigned int>(absl::GetFlag(FLAGS_seed)) + i / count;
    runs[i % count][i / count] =
        Play(catalogs, branches[i % count], maps, seed);
  });

  std::vector<Climb> typical;
  for (int i = 0; i < count; ++i) {
    typical.push_back(runs[i][TypicalRun(runs[i])]);
  }

  if (absl::GetFlag(FLAGS_playtime)) {
    PrintPlaytime(catalogs, branches, typical);
    PrintMeso(branches, typical);
    PrintHonor(branches, typical);
    PrintAbility(branches, typical);
  }
  if (absl::GetFlag(FLAGS_ledger)) {
    PrintLedger(branches, typical);
  }

  // Third jobs only. A branch that stops at its 2nd job is scaffolding for the
  // playtime table above, not a player: the advancement is there at 60 and
  // nobody climbs to 100 without it. It matters here because the two answer
  // differently -- a 2nd job finds the top two maps a coin flip against Sand
  // Dwarf and stays put, where a 3rd job is paid a third more for moving up,
  // and the cape only falls off the mobs up there.
  PrintFrozenDrops(branches, typical);
  PrintTokenOdds(branches.data(), typical, count);
  PrintTargets(branches, runs);
  PrintBossReadiness(catalogs, branches, typical);
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
