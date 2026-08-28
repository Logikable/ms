/* What a character reaches by the level cap: how long the climb takes, what it
 * paid them, and what they are standing in when they get there.
 *
 * The real engine, run forward: the same AdvanceCombat the frontend ticks,
 * paid out at the same rate, so what it reports is playtime rather than an
 * estimate of it.
 *
 * The one thing the engine cannot supply is what the player does between
 * fights, so the sweep plays them:
 *
 *   - every AP on the job's primary stat, every SP on whatever it will buy;
 *   - the whole Etc tab sold at each level;
 *   - the best weapon they can hold and pay for, bought the level it comes
 *     within reach, with the type of it measured rather than assumed;
 *   - what is left of the purse spent on scrolls and stars, the weapon first;
 *   - and the map that pays the most EXP a second of the ones they live on,
 *     re-chosen at every level.
 *
 * That last one is measured, not guessed: each candidate map is played out for
 * a few respawn beats with the character exactly as they stand, and a map that
 * kills them is not a candidate. It makes this an attentive player's clock --
 * a floor on how long the climb takes rather than an average of it.
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
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "analysis/gear_plan.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_format.h"
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/exp_table.h"
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
ABSL_FLAG(int, star_target, 15,
          "Stars each piece is taken to, held down to its own maximum. 15 is "
          "the last star an attempt cannot destroy the item at, which is "
          "where a player wearing pieces one boss drops stops.");
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
ABSL_FLAG(int, runs, 1,
          "Climbs per branch, each on its own seed. Drops are rolled, so one "
          "climb says almost nothing about whether a set completes.");
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

// Everything the player does on levelling up, in the order that makes each
// step pay for the next: the advancement first, then the points it hands over,
// then the drops turned into meso, then the weapon that meso buys, and only
// then the choice of where to take it.
void Retool(GameState& state, const std::vector<Job>& path, int* taken,
            const std::vector<std::string>& maps, int beats, double step,
            Purse& purse, GearShopper& shopper) {
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
  purse.Note(state.character);
  // What fell goes on before what is bought, so the weapon measurement is
  // taken with the rest of the outfit already in place.
  WearBestFromBag(state.character);
  Outfit(state, /*budget=*/true);
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
  int endgame_boss_set = 0;
  // Pieces worn that take upgrades at all, and how many of them are finished:
  // every slot spent and the stars up to the plan's target or the item's own
  // ceiling, whichever is lower.
  int endgame_pieces = 0;
  int endgame_finished = 0;
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
  if (climb.frozen_set_level == 0 && frozen >= 6) {
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
  }
}

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

// Runs every open fight once and reports what they took off the clock. A run
// that misses the limit pays nothing and still costs the whole of it.
double RunDailies(GameState& state, int level, Climb& climb) {
  double spent = 0.0;
  for (const std::pair<std::string, int>& fight :
       UnlockedBosses(state, level)) {
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
    if (outcome.won) {
      ++log.clears;
      spent += outcome.seconds;
      if (log.first_clear_level == 0) {
        log.first_clear_level = level;
      }
    } else {
      spent += state.bosses[fight.first]
                   .difficulties(fight.second)
                   .time_limit_seconds();
    }
  }
  return spent;
}

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
  Climb& climb;
  double step = 0.5;
  int beats = 4;
  // Playtime so far, and when the next reset falls due.
  double seconds = 0.0;
  double next_daily = kDaySeconds;
};

// How many worn pieces take upgrades, and how many of them are finished at
// `target`: every slot spent, and the stars up to that or the item's own
// ceiling, whichever is lower.
std::pair<int, int> PiecesFinished(const GameState& state, int target) {
  int pieces = 0;
  int finished = 0;
  for (const std::pair<const EquipSlot, EquipInstance>& entry :
       state.character.equipped()) {
    const EquipInstance& item = entry.second;
    // max_stars() is the level's ceiling and says nothing about whether the
    // item takes stars at all, so ask before believing it.
    int stars = Supports(item.prototype(), UPGRADE_STAR_FORCE)
                    ? std::min(target, item.max_stars())
                    : 0;
    bool takes_scroll = Supports(item.prototype(), UPGRADE_SCROLL) &&
                        item.prototype().upgrade_slots() > 0;
    if (!takes_scroll && stars == 0) {
      continue;  // an off-hand or a pocket, which takes neither
    }
    ++pieces;
    if (item.equip_state().remaining_upgrade_slots() == 0 &&
        item.stars() >= stars) {
      ++finished;
    }
  }
  return {pieces, finished};
}

// Runs the dailies if one is due, and puts what they dropped on. Returns
// whether anything happened, since the fight parameters have to be rebuilt
// when it did.
bool MaybeDailies(Session& run, int level) {
  if (!absl::GetFlag(FLAGS_dailies) || run.seconds < run.next_daily) {
    return false;
  }
  run.next_daily += kDaySeconds;
  run.seconds += RunDailies(run.state, level, run.climb);
  WearBestFromBag(run.state.character);
  run.purse.Note(run.state.character);
  run.shopper.Spend(run.state);
  run.purse.Note(run.state.character);
  return true;
}

// Plays the character forward to the level cap, or until the give-up clock
// runs out.
void ClimbToCap(Session& run) {
  double give_up = absl::GetFlag(FLAGS_give_up_hours) * 3600.0;
  Retool(run.state, run.path, &run.taken, run.maps, run.beats, run.step,
         run.purse, run.shopper);

  int level = run.state.character.proto().level();
  double level_began = 0.0;
  Stint stint = {level, 0.0, run.state.current_map,
                 HeldWeaponName(run.state.character)};
  CombatSim sim;
  // Built once and reused until something changes it. Nothing in a fight moves
  // between two steps of the same level on the same map, and rebuilding it
  // every step is where this sim used to spend almost all of its time.
  CombatParams params = ComputeCombatParams(run.state);
  while (level < kTrialLevelCap && run.seconds < give_up) {
    AdvanceCombat(run.state, sim, params, run.step);
    NoteTokenChances(params, sim, run.climb);
    run.purse.Note(run.state.character);
    run.seconds += run.step;
    if (MaybeDailies(run, level) || sim.died_this_step()) {
      // Dying sends them home, where there is nothing to fight, and a fight
      // fought elsewhere leaves the map behind unchosen either way.
      PickMap(run.state, run.maps, run.beats, run.step);
      params = ComputeCombatParams(run.state);
    }
    if (run.state.character.proto().level() == level) {
      continue;
    }
    stint.seconds = run.seconds - level_began;
    run.climb.stints.push_back(stint);
    level_began = run.seconds;
    level = run.state.character.proto().level();
    NoteFrozenDrops(run.state, level, run.climb);
    Retool(run.state, run.path, &run.taken, run.maps, run.beats, run.step,
           run.purse, run.shopper);
    NoteMilestones(run.state, level, run.seconds, run.purse, run.climb);
    params = ComputeCombatParams(run.state);
    stint = {level, 0.0, run.state.current_map,
             HeldWeaponName(run.state.character)};
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
  CombatSim sim;
  CombatParams params = ComputeCombatParams(run.state);
  // Retooled on the clock rather than on levelling up, since nothing levels
  // any more: often enough that a star bought is felt, rarely enough that the
  // measurement behind it is not the whole cost of the section.
  double next_retool = run.seconds + kDaySeconds / 24.0;
  while (run.seconds < horizon) {
    AdvanceCombat(run.state, sim, params, run.step);
    run.purse.Note(run.state.character);
    run.seconds += run.step;
    bool fought = MaybeDailies(run, level);
    if (run.seconds >= next_retool) {
      next_retool += kDaySeconds / 24.0;
      WearBestFromBag(run.state.character);
      Outfit(run.state, /*budget=*/true);
      run.shopper.Spend(run.state);
      run.purse.Note(run.state.character);
      PickMoneyMap(run.state, run.maps, run.beats, run.step);
      run.climb.money_map = run.state.current_map;
      fought = true;
    }
    if (fought || sim.died_this_step()) {
      params = ComputeCombatParams(run.state);
    }
  }
  std::pair<int, int> weapon = WeaponUpgrades(run.state);
  run.climb.endgame_seconds = run.seconds - began;
  run.climb.endgame_earned = run.purse.earned - earned_at_cap;
  run.climb.endgame_spent = run.purse.spent - spent_at_cap;
  run.climb.endgame_stars = weapon.first;
  run.climb.endgame_frozen = PiecesWorn(run.state, "frozen");
  run.climb.endgame_boss_set = PiecesWorn(run.state, "boss_accessory");
  std::pair<int, int> finished =
      PiecesFinished(run.state, absl::GetFlag(FLAGS_star_target));
  run.climb.endgame_pieces = finished.first;
  run.climb.endgame_finished = finished.second;
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
  plan.star_target = absl::GetFlag(FLAGS_star_target);
  plan.scroll_rate = absl::GetFlag(FLAGS_scroll_rate);

  Session run = {state, maps, PathTo(branch), 0, Purse(), GearShopper(plan),
                 climb};
  run.step = absl::GetFlag(FLAGS_step);
  run.beats = absl::GetFlag(FLAGS_probe_beats);
  ClimbToCap(run);
  if (absl::GetFlag(FLAGS_endgame) &&
      state.character.proto().level() >= kTrialLevelCap) {
    FarmAtCap(run);
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
  int star_target = absl::GetFlag(FLAGS_star_target);
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
                  " of 6"
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
          "  %-46s %-12s %d* weapon, %d of 6 Frozen, %d Boss acc.\n",
          ("after " + Clock(typical.endgame_seconds) + " at the cap").c_str(),
          (std::string(earned) + " earned").c_str(), typical.endgame_stars,
          typical.endgame_frozen, typical.endgame_boss_set);
      char spent[16];
      FormatShort(static_cast<double>(typical.endgame_spent), spent,
                  sizeof(spent));
      std::printf("  %-46s %-12s %d of %d pieces finished at %d*\n",
                  "  spent on gear", spent, typical.endgame_finished,
                  typical.endgame_pieces, star_target);
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
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
