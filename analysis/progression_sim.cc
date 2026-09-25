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
 *   - the daily symbol claim, once a day;
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
 * Three sections, each on its own flag.
 *
 *   --playtime  how long each branch takes to each level, and what it earned
 *               on the way. The climb alone.
 *   --ledger    what the purse went on and what the character has to show for
 *               it: the weapon's slots and stars, and how much of each set is
 *               on their back. The potions are two columns of it -- what they
 *               drank and what a permanent unlock cost; --buffs reads the
 *               counterfactual, off, rent or buy.
 *   --boss_report
 *               where each fight falls across the branches: the level of the
 *               first clear, the clock at it, and how far the branches that
 *               never won it got. Read it with --total_days, which gives every
 *               branch one budget for the climb and the days after it --
 *               otherwise a fight nobody can beat is only a fight the run
 *               stopped short of.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 * The sweep climbs the branches that take a 4th advancement. The rest stop at
 * their 2nd or 3rd job and were never built to reach the cap -- --branch takes
 * any one of them when the question is about one.
 *
 * One branch takes about fifteen seconds, and the whole sweep about twenty-six
 * -- it climbs a branch a core. Anything far past that is a bug to chase
 * rather than a wait to sit through.
 *
 * --checkpoint_at writes each climb down at a level and starts the next run
 * there, which is worth having while a change past that level is being tuned:
 * a resumed run is identical to one that climbed the whole way, down to the
 * last rolled drop. A file is stamped with the binary that wrote it and named
 * for the flags that shaped the climb, so it can outlive neither a rebuild nor
 * a setting.
 *
 *   bazelisk run //analysis:progression_sim
 *   bazelisk run //analysis:progression_sim -- --detail
 *   bazelisk run //analysis:progression_sim -- --checkpoint_at=160
 *   bazelisk run //analysis:progression_sim -- --branch=DARK_KNIGHT
 *   bazelisk run //analysis:progression_sim -- --total_days=30 --runs=3
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "analysis/ability_plan.h"
#include "analysis/buff_plan.h"
#include "analysis/checkpoint.h"
#include "analysis/cube_plan.h"
#include "analysis/drop_value.h"
#include "analysis/gear_plan.h"
#include "analysis/hyper_plan.h"
#include "analysis/meso_rate.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_format.h"
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "analysis/skill_plan.h"
#include "src/character/arcane_force.h"
#include "src/character/boss_reset.h"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/dailies.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/inner_ability.h"
#include "src/character/job_advancement.h"
#include "src/character/progression.h"
#include "src/combat/combat.h"
#include "src/combat/constants.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/loot.h"
#include "src/combat/measure.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/currency.h"
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
ABSL_FLAG(bool, playtime, true, "Print how long the climb takes.");
ABSL_FLAG(std::string, buffs, "auto",
          "What the player does about the potions: auto (on when they pay, "
          "bought when the horizon says they pay twice over), off, rent or "
          "buy.");
ABSL_FLAG(bool, ledger, true,
          "Print what the climb spent and what it is wearing at the cap.");
ABSL_FLAG(int, star_ceiling, ms::kMaxStarForce,
          "A star the shopper will not go past whatever its arithmetic says. "
          "Here so one run can be pinned against another: the shopper decides "
          "where to stop by price, and stops well short of this.");
ABSL_FLAG(bool, cubes, true,
          "Whether the shopper cubes. Off is the counterfactual: the same "
          "climb with every cube's meso left for the stars.");
ABSL_FLAG(int, scroll_rate, 100,
          "Success rate of the scrolls bought, as a whole percent. A lower "
          "rate pays more per slot it lands and wastes the rest, which on a "
          "drop-only piece is a slot nothing gets back.");
ABSL_FLAG(bool, endgame, true,
          "Print what the days after the cap add up to: the best money map "
          "farmed and the dailies run, with the purse still spending.");
ABSL_FLAG(double, endgame_days, 7.0,
          "Days played at the cap for the endgame section.");
ABSL_FLAG(double, total_days, 40.0,
          "Stop every branch this many days after it started -- the climb and "
          "the days after the cap are one budget, which is what makes a fight "
          "nobody beats a wall rather than a run that stopped short. 0 leaves "
          "--give_up_hours and --endgame_days to say where each half stops on "
          "its own.");
ABSL_FLAG(bool, boss_report, true,
          "Print where each fight falls across the branches: the level of the "
          "first clear, the clock at it, and how far the branches that never "
          "won it got.");
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
ABSL_FLAG(int, checkpoint_at, 0,
          "Write each branch's climb down the moment it reaches this level, "
          "and start the next run there instead of climbing to it again. 0 "
          "climbs the whole ladder every time. A file is stamped with the "
          "binary that wrote it and thrown away by any other, so a checkpoint "
          "cannot outlive the change it was cut through -- which also means "
          "the first run after a build climbs the whole way.");
ABSL_FLAG(std::string, ability_rank, "legendary",
          "The Inner Ability rank a character rolls up to before they hold "
          "any line through a reset. A lock buys nothing while what they are "
          "short of is a rank, and the ladder to Legendary costs more honor "
          "than a climb to the cap is paid.");
ABSL_FLAG(std::string, branch, "",
          "One branch to climb, as its Job enum name without the JOB_ prefix "
          "(DARK_KNIGHT). Any branch at all, including the ones that stop at "
          "their 2nd or 3rd job and the sweep leaves out. Empty climbs the "
          "sweep, which waits on the slowest of them however many cores are "
          "free -- so name one when one is the question.");

namespace ms {
namespace {

// The levels the table reports a running total at. Every tenth to 140, where
// the SP schedule ends and the books are finished, then every twentieth, and
// the cap itself last -- past 140 a level buys HP, MP and AP and nothing else,
// so the rungs need not be as close together.
constexpr int kMilestones[] = {
    10,  20,  30,  40,  50,  60,  70,  80,  90,  100,
    110, 120, 130, 140, 160, 180, 200, 220, 240, kTrialLevelCap};
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

// How many of the slowest cycle a ranking window holds. ONE, which is all it
// takes to see a buff go up and come down again and so to price a lever that
// lengthens it. It held two until 2026-09-15, which bought nothing the second
// cycle did not already show and cost the whole of it -- and the ranking
// window is half of a progression run.
constexpr double kBookCycles = 1.0;

// Seconds in a day, which is what a daily reset waits out. The game is idle,
// so a day of playtime is a day.
constexpr double kDaySeconds = 24.0 * 60.0 * 60.0;

// The wall clock the run's seconds are laid over, for what the game gates on
// its own reset: 2026-01-01 at the reset hour, read in UTC (see main) so a
// day is always 24 hours and turns over with the sim's own boss day.
constexpr int64_t kSimEpoch = 1767225600 + kBossResetHour * 60 * 60;

// How long to play a character out for. A window shorter than a buff's cycle
// cannot see it go up or come down, so a lever that lengthens a buff reads
// EXACTLY nothing -- a question about a buff buys a window wide enough for the
// slowest cycle.
//
// Off the character's BOOK, not the buffs they have learned: the book does not
// move while a decision is taken and the learned list does, so SpendBook would
// otherwise compare its two candidates over different horizons.
//
// `seconds` is in game seconds where MeasureFight counts in the stretched
// clock, so the window is scaled on the way out.
double WindowFor(const GameState& state, double seconds) {
  double cycle = 0.0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    if (skill.has_buff() && state.character.HasBookFor(skill)) {
      cycle = std::max(cycle, skill.cooldown_seconds());
    }
  }
  return std::max(seconds, kBookCycles * cycle) *
         GameSpeedFactor(state.character.proto().level());
}

// What a point of damage into the map is worth, in meso: what the crowd pays
// for falling over against what felling it costs, each type weighted by how
// many stand there at once. A measured fight CANNOT count kills -- measuring
// holds every monster's HP still on purpose -- so the kills are read off the
// damage at what one costs.
double MesoPerDamage(const GameState& state, const DropBasis& basis,
                     const CombatParams& params, double item_drop_pct) {
  double paid = 0.0;
  double body = 0.0;
  for (const CombatType& type : params.types) {
    if (type.mob == nullptr || type.mob->boss() || type.mob->max_hp() <= 0) {
      continue;
    }
    double count = std::max(1, type.simultaneous);
    paid +=
        count * MesoPerKill(*type.mob, DropsPerKill(state, basis, *type.mob),
                            item_drop_pct);
    body += count * type.mob->max_hp();
  }
  return body > 0.0 ? paid / body : 0.0;
}

// What the character takes off the map a second, in meso. Damage is in it, the
// kills being what the swings buy, and so are the drop and meso levers, which
// fill the purse without moving a swing.
//
// It does NOT carry the respawn cap: a character already killing the map as
// fast as it stands back up buys nothing with more damage, and this still
// reads the swing. BuffYield asks that of a fight played out.
double CrowdRateOver(GameState& state, const DropBasis& basis, double seconds) {
  CombatParams params = ComputeCombatParams(state);
  if (!params.active || params.types.empty()) {
    return 0.0;
  }
  int enemies = 0;
  for (const CombatType& type : params.types) {
    enemies += type.simultaneous;
  }
  enemies = std::max(1, enemies);
  Sequence played = MeasureFight(params, WindowFor(state, seconds), enemies);
  if (played.seconds <= 0.0) {
    return 0.0;
  }
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  return played.damage / played.seconds *
         MesoPerDamage(state, basis, params, derived.item_drop_pct) *
         (1.0 + MesoBonus(derived)) * derived.meso_final_mult;
}

double CrowdRate(GameState& state, const DropBasis& basis) {
  return CrowdRateOver(state, basis, kBookSeconds);
}

// What one clear of `difficulty` pays. The meso is FLAT, no %meso reaching it,
// and the drops take the rate. Most of a boss's table sells for nothing, so
// each is worth what the purse would otherwise spend for the same combat
// power. See //analysis:drop_value.
double BossPayout(const GameState& state, const DropBasis& basis,
                  const BossDifficulty& difficulty, double item_drop_pct) {
  double paid = difficulty.meso();
  for (const MobDrop& drop : difficulty.drops()) {
    double rate = BossDropRate(drop, item_drop_pct);
    if (rate <= 0.0) {
      continue;
    }
    if (drop.has_item()) {
      std::map<std::string, ItemPrototype>::const_iterator it =
          state.items.find(drop.item());
      if (it != state.items.end()) {
        paid += rate * ItemDropValue(basis, drop.item(), it->second);
      }
      continue;
    }
    if (!drop.has_equip()) {
      continue;
    }
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    if (it != state.equips.end()) {
      paid += rate * EquipDropValue(state, basis, it->second);
    }
  }
  return paid;
}

// What the aimed-at fight pays a second: the purse a clear hands over, divided
// by how long it takes. ONE enemy behind its own defence, a different question
// from the crowd -- the swing that clears twelve rarely kills the one that
// matters. In meso, like the crowd rate, so the book can weigh the two.
double BossRateOver(GameState& state, const DropBasis& basis, double seconds) {
  std::pair<std::string, int> fight;
  if (!AimedFight(state, &fight)) {
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
  Sequence played = MeasureFight(params, WindowFor(state, seconds));
  int64_t body = BossTotalHp(state.mobs, difficulty);
  if (played.seconds <= 0.0 || body <= 0) {
    return 0.0;
  }
  double dps = played.damage / played.seconds;
  double drop_pct = DerivedStatsFor(state.character, state.skills, {},
                                    state.party, Activity::kBossing)
                        .item_drop_pct;
  return BossPayout(state, basis, difficulty, drop_pct) * dps / body;
}

double BossRate(GameState& state, const DropBasis& basis) {
  return BossRateOver(state, basis, kBookSeconds);
}

// The book ranked on both at once. A book answering only one is not a build
// anybody plays: ranked on the map alone the Hero reaches Hilla holding
// nothing that kills her.
//
// Both legs are meso a second, so a point can be weighed on either. The
// GEOMETRIC mean rather than a sum: a sum lets a build that cannot touch a
// boss score well on the map alone. Either leg stands in alone where the other
// has nothing to measure.
double BookRate(GameState& state, const DropBasis& basis) {
  double crowd = CrowdRate(state, basis);
  double boss = BossRate(state, basis);
  if (crowd <= 0.0) {
    return boss;
  }
  if (boss <= 0.0) {
    return crowd;
  }
  return std::sqrt(crowd * boss);
}

// Where a run's meso came from and went. The purse says how much moved; this
// says WHAT moved it. Everything combat pays that no named source claims is a
// mob's drop, so the rows add up to the purse even when a source is added and
// nobody writes a line for it.
struct Ledger {
  int64_t etc_sales = 0;
  int64_t boss_clears = 0;
  int64_t gear_bought = 0;  // the shop's own shelves: weapon, off-hand, equips
  GearSpend gear;           // scrolls, stars, hammers, replacements
  BuffSpend buffs;          // drunk by the second and by the fight, and bought

  int64_t named_income() const {
    return etc_sales + gear.sold + boss_clears;
  }
};

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
  WornGear::const_iterator it =
      state.character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it == state.character.equipped().end()) {
    return {-1, -1};
  }
  return {it->second->stars(),
          it->second->equip_state().remaining_upgrade_slots()};
}

// Sells the Etc tab, keeping what the counter pays nothing for. The
// currencies are not on it to be sold by accident, but a drop that is worth
// nothing is still worth keeping out of the buy-back shelf.
int64_t SellDrops(CharacterInstance& character) {
  int64_t earned = 0;
  int i = 0;
  while (i < static_cast<int>(character.stackables().size())) {
    const StackableItem& stack = character.stackables()[i];
    int count = stack.count();
    if (stack.prototype().sell_price() <= 0) {
      ++i;
      continue;
    }
    earned += character.SellStackable(i, count);
  }
  return earned;
}

// What a map came to over the probe.
struct Probe {
  double exp_per_second = 0.0;
  double meso_per_second = 0.0;
  bool died = false;
};

// Plays `map` out for a few respawn beats with the character exactly as they
// stand, and reports what it pays. Nothing is banked -- the fight is run
// straight off CombatParams rather than through AdvanceCombat, so the probe
// costs the character neither EXP nor meso nor HP.
Probe ProbeMap(GameState& state, const DropBasis& basis, const std::string& map,
               int beats, double step) {
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
  std::vector<double> killed(params.types.size(), 0.0);
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < horizon; elapsed += step) {
    sim.Advance(params, step);
    const std::vector<int64_t>& kills = sim.view().kills_this_step;
    for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
      exp += kills[i] * params.types[i].mob->exp();
      killed[i] += kills[i];
    }
    if (sim.view().died_this_step) {
      probe.died = true;
      return probe;
    }
  }
  for (double& kills : killed) {
    kills /= horizon;
  }
  probe.exp_per_second = exp / horizon;
  // The character's own levers are in it, not left out as multiplying every
  // map alike: drop rate does not. It lifts the Etc a map leaves without
  // limit and the meso it drops only as far as certain, so which map pays
  // best is a different answer at +100% than at nothing.
  probe.meso_per_second =
      MesoPerSecondFor(state, CrowdFor(state, basis, params, killed));
  return probe;
}

// Moves the character to whichever map pays the most of what they are farming
// for, of the ones they survive. Leaves them where they are if every map kills
// them, which the give-up clock then catches.
void PickMapFor(GameState& state, const std::vector<std::string>& candidates,
                int beats, double step, double power_per_meso,
                HeldYardstick& held, bool for_meso) {
  // One basis for the whole shelf of maps: what a drop is worth depends on the
  // character, not on which map dropped it.
  DropBasis basis = DropBasisFor(state, power_per_meso, held);
  std::string best;
  double best_rate = 0.0;
  for (const std::string& map : candidates) {
    Probe probe = ProbeMap(state, basis, map, beats, step);
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
             int beats, double step, double power_per_meso,
             HeldYardstick& held) {
  PickMapFor(state, candidates, beats, step, power_per_meso, held,
             /*for_meso=*/false);
}

// At the cap, where there is no EXP left to earn: the most meso a second.
void PickMoneyMap(GameState& state, const std::vector<std::string>& candidates,
                  int beats, double step, double power_per_meso,
                  HeldYardstick& held) {
  PickMapFor(state, candidates, beats, step, power_per_meso, held,
             /*for_meso=*/true);
}

// What the last scout settled on, and the level it was asked at.
struct WeaponScout {
  EquipType settled = EQUIP_TYPE_UNSPECIFIED;
  int settled_at = 0;
};

// How many levels a settled weapon type is trusted for. The scout is dear --
// ten ladders, each swung for a simulated minute -- and the answer is a
// branch's whole identity: a Paladin does not stop being a Paladin between
// Lv61 and Lv65. It is re-asked at every advancement regardless, which is
// where it actually moves.
constexpr int kScoutEveryLevels = 5;

// What the last map choice was taken at: the level, and what was worn. Picking
// a map probes EVERY hunting ground with a played fight, and the answer only
// moves when the character can reach further up the ladder. A star or a scroll
// is not here: they land on nearly every look, and the levelling re-asks.
struct MapChoice {
  int level = 0;
  std::string worn;
  // What they were hitting for when the money map was last picked. The cap is
  // where the level stops moving and the stars and cubes do not, so the
  // endgame's hourly look re-asks on growth instead -- the same rule the Hyper
  // Stat table is re-measured on.
  int power = 0;
};

// One line per slot, the item's name and nothing else. What both of the
// decisions below re-ask on: a piece with a different name is a different
// character to them, and a star on the same piece is not.
std::string WornNames(const GameState& state) {
  std::string worn;
  for (const std::pair<const EquipSlot, const EquipInstance*>& item :
       state.character.equipped()) {
    absl::StrAppend(&worn, item.second->name(), "\n");
  }
  return worn;
}

// What the book and the matrix were last planned against. Both are swept the
// same way -- every purchase priced against a played fight, over and over
// until nothing left is worth buying -- which makes a sweep the most expensive
// thing a look does. Two looks agreeing on every field below would rank every
// purchase alike and buy nothing, so the second sweep is pure cost.
//
// The node levels are deliberately not in `skills`: the matrix plan is half of
// what this gates, and a gate its own output re-triggers gates nothing.
struct PlanKey {
  std::string worn;    // one line per slot, the item's name
  std::string skills;  // the levels the book holds, the nodes left out
  std::string map;
  std::pair<std::string, int> fight;
  int64_t points = 0;  // unspent SP, Hyper SP and V Points, summed

  bool operator==(const PlanKey& other) const {
    return points == other.points && worn == other.worn &&
           skills == other.skills && map == other.map && fight == other.fight;
  }
};

// The part of a PlanKey a matrix plan stands on: the gear, the book and the
// fight. What the points or the map do leaves it standing.
std::string MatrixKey(const PlanKey& key) {
  return absl::StrCat(key.worn, "|", key.skills, "|", key.fight.first, "/",
                      key.fight.second);
}

PlanKey PlanKeyFor(const GameState& state) {
  PlanKey key;
  const Character& proto = state.character.proto();
  key.worn = WornNames(state);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.v_node() != V_NODE_KIND_UNSPECIFIED) {
      continue;
    }
    int level = state.character.skill_level(entry.second);
    if (level > 0) {
      absl::StrAppend(&key.skills, entry.first, "=", level, "\n");
    }
  }
  key.map = state.current_map;
  AimedFight(state, &key.fight);
  key.points = proto.hyper_sp() + proto.v_points();
  for (const std::pair<const int32_t, int32_t>& pool : proto.sp_by_stage()) {
    key.points += pool.second;
  }
  return key;
}

// Everything the player does on levelling up, in the order that makes each
// step pay for the next: the advancement first, then the points it hands over,
// then the drops turned into meso, then the weapon that meso buys, and only
// then the choice of where to take it.
void Retool(GameState& state, const std::vector<Job>& path, int* taken,
            const std::vector<std::string>& maps, int beats, double step,
            Purse& purse, GearShopper& shopper, WeaponScout& scout,
            PlanKey& planned, ToggleChoice& toggles, MapChoice& mapped,
            Ledger& ledger) {
  // What the matrix was last planned from scratch against. See below.
  const std::string planned_matrix = MatrixKey(planned);
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
  ledger.etc_sales += SellDrops(state.character);
  purse.Note(state.character);
  // What fell goes on before what is bought, so the weapon measurement is
  // taken with the rest of the outfit already in place. The spare symbols go
  // into the worn ones in the same breath: a duplicate is a bag row until
  // something takes it, and what it banks is what the shopper then pays to
  // cash in.
  WearBestFromBag(state.character);
  CollectSymbols(state.character);
  int level = state.character.proto().level();
  if (scout.settled == EQUIP_TYPE_UNSPECIFIED ||
      level - scout.settled_at >= kScoutEveryLevels) {
    scout.settled = SettledWeaponType(state, /*budget=*/true);
    scout.settled_at = level;
  }
  // Around the shelf rather than inside it: Outfit buys the weapon, the
  // off-hand and whatever equipment the shop stocks, and the ledger wants the
  // three together under what the shop was paid.
  int64_t before_shelf = state.character.meso();
  Outfit(state, /*budget=*/true, scout.settled);
  ledger.gear_bought +=
      std::max<int64_t>(0, before_shelf - state.character.meso());
  // The book after the weapon, a point being worth what the thing in their
  // hands can swing; the weapon is settled on what the branch is FOR, which is
  // what keeps the two from talking each other into a corner.
  //
  // Only where something has moved that could reorder them: the pair is swept
  // several times a day, and a sweep against an unchanged character re-derives
  // the plan it already holds. See PlanKey.
  if (!(PlanKeyFor(state) == planned)) {
    DropBasis basis =
        DropBasisFor(state, shopper.power_per_meso(), shopper.yardstick());
    SpendBookWithToggles(
        state, [&basis](GameState& inner) { return BookRate(inner, basis); },
        &toggles);
    // The matrix after the book and on the same rate: its own pool, its own
    // ladder, and nothing in it is worth anything until the skills it lifts
    // have been bought.
    //
    // Planned afresh only where the gear, the book or the fight has moved --
    // what carries a character over a defence wall. Otherwise the new points
    // go on top: a full plan is most of what a climb costs.
    PlanKey now = PlanKeyFor(state);
    SpendVMatrix(
        state, [&basis](GameState& inner) { return BookRate(inner, basis); },
        /*replan=*/MatrixKey(now) != planned_matrix);
  }
  LearnTheRest(state);
  // After the free skills rather than before them, since they are levels the
  // key reads: taken first, every look would find a key the last look could
  // not have stored and sweep again regardless.
  planned = PlanKeyFor(state);
  // After the weapon, because a scroll on last tier's weapon is meso that
  // buys nothing: the next one displaces it slots and stars and all.
  shopper.Spend(state);
  purse.Note(state.character);
  std::string worn = WornNames(state);
  if (mapped.level != state.character.proto().level() || mapped.worn != worn) {
    mapped.level = state.character.proto().level();
    mapped.worn = std::move(worn);
    PickMap(state, maps, beats, step, shopper.power_per_meso(),
            shopper.yardstick());
  }
}

// What one fight -- one boss at one difficulty -- came to over a run. Kept
// apart by difficulty because Hard Hilla is not Hilla: they open sixty levels
// apart and a branch can be years off one while farming the other.
struct BossLog {
  std::string label;  // "Normal Zakum", the way a player names the fight
  std::string boss;   // the catalog key, for reading the fight back out
  int difficulty = 0;
  int unlock_level = 0;
  int attempts = 0;
  int clears = 0;
  // The level the first try was taken at, which is not the unlock level: a
  // reset comes round once a day, and a climb can pass several levels inside
  // one.
  int first_attempt_level = 0;
  int first_clear_level = 0;          // 0 for a fight never won
  double first_clear_seconds = -1.0;  // the playtime that clear landed at
  // The most of the fight any attempt took down, 1.0 once it is won. Phases
  // never reached count whole, so a rout reads low rather than reading the
  // fraction of the one phase it could see.
  double best_done = 0.0;
  // The quickest clear: what it took, and what the character was hitting for
  // at the time. Zero for a fight never won.
  double best_seconds = 0.0;
  int best_power = 0;
};

// How one fight is named among the rest: the boss and which of its
// difficulties, since the log holds a row for each.
std::string FightKey(const std::string& boss, int difficulty) {
  return absl::StrCat(boss, "/", difficulty);
}

// Where a climb is written down and read back, worked out once for the sweep.
struct Checkpointing {
  std::string dir;  // empty for a run that keeps none
  std::string stamp;
  int level = 0;  // the level one is taken at, 0 for off
};

// What one worn piece's potential came to, for the table at the end.
struct PotentialRow {
  std::string slot;
  std::string item;
  PotentialRank rank = POTENTIAL_RANK_UNSPECIFIED;
  // The lines, already named and valued -- "12% ATT", "40% boss".
  std::vector<std::string> lines;
  // Whether the shopper was discounting this piece as one it might replace.
  bool replaceable = false;
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

// What one milestone level found: when the climb reached it, and what the
// character was standing in and had been paid by then.
struct Milestone {
  // Playtime in seconds when the level was reached, or -1 for one never
  // reached -- which is what every other field here is read against.
  double seconds = -1.0;
  // Meso the character had been paid, all told, and what the shop had taken
  // back off them.
  int64_t meso = 0;
  int64_t spent = 0;
  // What they were standing in: the weapon's stars and the slots still open in
  // it, and how much of each set was on their back.
  int stars = 0;
  int slots = 0;
  int frozen = 0;
  int boss_set = 0;
  // The honor the pool had been PAID -- what it is holding plus whatever has
  // been rerolled away -- and the two parts of it that can be named: what the
  // levels paid and what the daily clears paid. Whatever is left over is what
  // the monsters dropped.
  int64_t honor = 0;
  int64_t level_honor = 0;
  int64_t boss_honor = 0;
};

// What one Frozen token came to over a climb.
struct TokenProgress {
  // The level it first dropped at, or 0 for one that never did. The only
  // reading there is on whether a climb ever gets to hold the tier it buys.
  int level = 0;
  // Kills off mobs that carry it, and the log of the chance every one of them
  // came up empty. One climb either drops a token or does not, which says
  // almost nothing at a rate this long; the kills behind it say how likely
  // that was, which is the number worth reading.
  int64_t kills = 0;
  double log_miss = 0.0;
};

// What one branch's climb came to.
struct Climb {
  // One per kMilestones entry, in that order.
  Milestone milestones[kNumMilestones];
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
  // Pieces the shopper destroyed and put back over the whole climb, which is
  // the reading on whether the stars past fifteen were worth walking into.
  int booms = 0;
  // What the purse was paid over the whole run, and what it was holding at the
  // end of it -- the two ends of the ledger below.
  int64_t endgame_earned_total = 0;
  int64_t meso_held = 0;
  // Where the meso came from and where it went. End-of-run, like the endgame
  // rows above, so no checkpoint carries it.
  Ledger ledger;
  // What the run left on each cubed piece: one row per worn slot that takes a
  // potential, already in reading order.
  std::vector<PotentialRow> potentials;
  // The character the run left behind, for the sheet the weakest branch gets
  // printed as.
  Character final_character;
  std::string money_map;
  // The level each Frozen piece first dropped at, or 0 for one that never
  // did. The rates are set so that every piece arrives before the level cap --
  // this is the check on that, farmed rather than argued.
  int frozen_level[kNumFrozenPieces] = {0};
  // One per kFrozenTokens entry, in that order.
  TokenProgress tokens[kNumFrozenTokens];
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
        climb.tokens[token].kills += kills[i];
        climb.tokens[token].log_miss += kills[i] * std::log1p(-drop.per_kill());
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

// Plays the encounter out for a few respawn beats, banking none of it, and
// reports the rate it settled at.
//
// Nothing that decides that rate moves between two steps of the same level on
// the same map, so it holds until one of them does. That is what lets the
// climb be JUMPED rather than stepped: measure once, then hand a whole
// stretch's kills to AwardCombatRewards in one go.
//
// It gives up the drift inside a stretch -- the HP wanders over an evening and
// this cannot see them die of it -- against sixty times the speed.
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
    const std::vector<int64_t>& kills = sim.view().kills_this_step;
    for (std::size_t i = 0; i < params.types.size(); ++i) {
      total[i] += kills[i];
      exp += kills[i] * params.types[i].mob->exp();
    }
    if (sim.view().died_this_step) {
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
  for (int token = 0; token < kNumFrozenTokens; ++token) {
    if (climb.tokens[token].level == 0 &&
        state.character.currencies().Count(kFrozenTokens[token]) > 0) {
      climb.tokens[token].level = level;
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
    if (level < kMilestones[i] || climb.milestones[i].seconds >= 0.0) {
      continue;
    }
    climb.milestones[i].seconds = seconds;
    climb.milestones[i].meso = purse.earned;
    climb.milestones[i].spent = purse.spent;
    climb.milestones[i].stars = weapon.first;
    climb.milestones[i].slots = weapon.second;
    climb.milestones[i].frozen = frozen;
    climb.milestones[i].boss_set = PiecesWorn(state, "boss_accessory");
    // Paid, not held: the pool has one sink and this is the only thing that
    // spends it, so what went out of it is known exactly.
    climb.milestones[i].honor =
        state.character.honor() + climb.ability_honor_spent;
    climb.milestones[i].level_honor = HonorForLevels(1, level);
    climb.milestones[i].boss_honor = kBossClearHonor * ClearsSoFar(climb);
  }
}

// Fights one boss once and reports what it took off the clock. A run that
// misses pays nothing and still costs whatever the player sat through.
double FightOnce(GameState& state, const std::pair<std::string, int>& fight,
                 int level, int power, double now, Climb& climb,
                 BossOutcome* result) {
  const Boss& boss = state.bosses[fight.first];
  const BossDifficulty& difficulty = boss.difficulties(fight.second);
  BossLog& log = climb.bosses[FightKey(fight.first, fight.second)];
  if (log.attempts == 0) {
    log.label = absl::StrCat(difficulty.name(), " ", boss.name());
    log.boss = fight.first;
    log.difficulty = fight.second;
    log.unlock_level = difficulty.unlock_level();
    log.first_attempt_level = level;
  }
  ++log.attempts;
  // Drunk on the way in, and counted before the purse is read for the clear
  // -- what a fight pays is not what a fight cost.
  EnterFightWithBuffs(state, &climb.ledger.buffs);
  int64_t before_fight = state.character.meso();
  BossOutcome outcome = FightBoss(state, fight.first, fight.second);
  climb.ledger.boss_clears +=
      std::max<int64_t>(0, state.character.meso() - before_fight);
  *result = outcome;
  log.best_done = std::max(log.best_done, 1.0 - outcome.left);
  if (!outcome.won) {
    return outcome.seconds;
  }
  ++log.clears;
  if (log.first_clear_level == 0) {
    log.first_clear_level = level;
    log.first_clear_seconds = now + outcome.seconds;
  }
  if (log.best_seconds == 0.0 || outcome.seconds < log.best_seconds) {
    log.best_seconds = outcome.seconds;
    log.best_power = power;
  }
  return outcome.seconds;
}

// How often the player opens the game, and when. NOT a metronome: a day is a
// handful of SESSIONS -- a look before work, a long gap, an evening checking
// every half hour -- and both thin out as the climb slows.
//
// The character farms throughout either way. This decides only when the PLAYER
// is there to sell, spend, move map, advance and walk up to a boss.
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
// in an evening half an hour apart: twelve looks a day is one at breakfast and
// eleven after work. `left` is what the evening still owes.
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
  // What the book and the matrix were last swept against -- see PlanKey.
  PlanKey planned;
  // Which switches they settled on, so the book is not spent both ways round
  // at every look -- see SpendBookWithToggles.
  ToggleChoice toggles;
  // What the map they are standing on was chosen at -- see MapChoice.
  MapChoice mapped;
  Climb& climb;
  double step = 0.5;
  int beats = 4;
  // Playtime so far, when the next reset falls due, and when the run ends.
  // The horizon is what a permanent purchase is weighed against, so it is
  // rewritten when the endgame section takes over the clock.
  double seconds = 0.0;
  double next_daily = kDaySeconds;
  double horizon = 0.0;
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
  // The Hyper Stat pool's own table, one per preset -- the two are allocated
  // apart, since what a crowd pays for is not what a boss does.
  HyperWorth hyper_farming;
  HyperWorth hyper_bossing;
  bool hyper_measured = false;
  int hyper_power = 0;
  // The fight the tables above were measured against. What a point of ignored
  // defence is worth is a fact about that fight, and the ladder triples it
  // between Cygnus and Lotus -- so a new target is as good a reason to
  // re-measure as a bigger character.
  std::pair<std::string, int> hyper_aim;
  std::pair<std::string, int> ability_aim;
  bool ability_measured = false;
  // The CombatPower the worth table was measured on, so it can be taken again
  // once the character it described has been outgrown.
  int ability_power = 0;
  // What was worn last time anything was allocated: one line per slot, the
  // item's name and nothing else. See GearChanged.
  std::string worn_gear;
  // Where this climb is written down, and what it is called there.
  const Checkpointing* saves = nullptr;
  std::string key;
  unsigned int seed = 0;
  bool saved = false;
};

// The flag's word for what the player does about the potions.
BuffMode BuffModeFromFlag() {
  std::string mode = absl::GetFlag(FLAGS_buffs);
  if (mode == "off") {
    return BuffMode::kOff;
  }
  if (mode == "rent") {
    return BuffMode::kRent;
  }
  if (mode == "buy") {
    return BuffMode::kBuy;
  }
  if (mode != "auto") {
    LOG(FATAL) << "--buffs must be auto, off, rent or buy";
  }
  return BuffMode::kAuto;
}

// What the player knows about the buffs as they stand: how much run is left,
// and how often they have been walking into a fight.
BuffPolicy BuffPolicyFor(const Session& run) {
  BuffPolicy policy;
  policy.mode = BuffModeFromFlag();
  policy.seconds_left = std::max(0.0, run.horizon - run.seconds);
  if (run.seconds > 0.0) {
    policy.boss_entries_per_second =
        run.climb.ledger.buffs.entries / run.seconds;
  }
  return policy;
}

// Takes the buff decisions on the standing encounter. The rates come off the
// yield already measured, so the potions cost no fight of their own.
//
// The TOTEM is the exception: what it is worth is the kills a halved beat
// buys, which only the fight knows, so the encounter is played a second time
// with the shorter beat. A character who was never waiting on the respawn
// comes back with the same rate and leaves it in the bag.
void PlanBuffsFor(Session& run, const CombatParams& params,
                  const Yield& yield) {
  BuffYield rates;
  rates.crowd = CrowdFor(run.state,
                         DropBasisFor(run.state, run.shopper.power_per_meso(),
                                      run.shopper.yardstick()),
                         params, yield.kills_per_second);
  // The yield already covers one side of the totem's question -- whichever
  // beat the character is standing on -- so only the other side is played out.
  Yield counter;
  const ConsumableInfo* totem = ConsumableInfoFor(CONSUMABLE_TYPE_WILD_TOTEM);
  if (totem != nullptr &&
      run.state.character.proto().level() >= totem->unlock_level) {
    bool planted =
        run.state.character.ConsumableInEffect(CONSUMABLE_TYPE_WILD_TOTEM);
    CombatParams other = params;
    other.respawn_seconds =
        (planted ? kRespawnIntervalSeconds : kWildTotemRespawnSeconds) *
        GameSpeedFactor(run.state.character.proto().level());
    counter = MeasureYield(run.state, other, run.beats, run.step);
    if (!counter.died) {
      rates.kills_without_totem = absl::MakeConstSpan(
          planted ? counter.kills_per_second : yield.kills_per_second);
      rates.kills_with_totem = absl::MakeConstSpan(
          planted ? yield.kills_per_second : counter.kills_per_second);
    }
  }
  PlanBuffs(run.state, BuffPolicyFor(run), rates, &run.climb.ledger.buffs);
}

// What the shopper needs to price a %meso or %drop potential line: those pay
// a rate rather than damage, and only the encounter in front of the character
// says what the rate is. The Crowd carries its own copies -- the shopper keeps
// this between looks, and the fight it came from does not.
void SetShopperIncome(Session& run, const CombatParams& params,
                      const Yield& yield) {
  Crowd crowd = CrowdFor(run.state,
                         DropBasisFor(run.state, run.shopper.power_per_meso(),
                                      run.shopper.yardstick()),
                         params, yield.kills_per_second);
  double mult =
      DerivedStatsFor(run.state.character, run.state.skills).meso_final_mult;
  CubeIncome income;
  income.seconds_left = std::max(0.0, run.horizon - run.seconds);
  income.rate = [crowd, mult](double meso_bonus, double drop_pct) {
    return MesoPerSecond(crowd, meso_bonus, mult, drop_pct);
  };
  run.shopper.SetIncome(income);
}

// Where the climb has got to inside a level: what the loop below keeps that is
// in neither the Session nor the Climb. Held together so a checkpoint can
// write it down and a resumed run pick it up mid-stride.
struct ClimbCursor {
  int level = 0;
  double level_began = 0.0;
  Stint stint;
  // Fractions of a kill carried over from the last jump.
  std::vector<double> carry;
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

// A line as the table reads it: "12% ATT", "40% boss". The enum's own name
// with the prefix and the tail cut, since //analysis has no business pulling
// the frontend's naming in to print eight rows.
std::string WithoutPrefix(const std::string& name, const std::string& prefix) {
  return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
}

std::string LineText(const PotentialLine& line, int item_level) {
  return absl::StrCat(PotentialLineValue(line.type(), line.rank(), item_level),
                      " ",
                      WithoutPrefix(PotentialLineType_Name(line.type()),
                                    "POTENTIAL_LINE_TYPE_"));
}

// What every cubed piece the character is standing in came to.
std::vector<PotentialRow> PotentialsWorn(const GameState& state) {
  std::vector<PotentialRow> rows;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped()) {
    if (!entry.second->CanCube()) {
      continue;
    }
    const EquipInstance& item = *entry.second;
    int level = item.prototype().required_level();
    PotentialRow row;
    row.slot = WithoutPrefix(EquipSlot_Name(entry.first), "EQUIP_SLOT_");
    row.item = item.prototype().name();
    row.rank = item.potential().rank();
    row.replaceable = Replaceable(state, entry.first);
    for (const PotentialLine& line : item.potential().lines()) {
      row.lines.push_back(LineText(line, level));
    }
    rows.push_back(row);
  }
  return rows;
}

GearReached ReachedOnGear(const GameState& state) {
  GearReached reached;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped()) {
    const EquipInstance& item = *entry.second;
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

// What the character is hitting a boss for as they stand.
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

// How much stronger the character has to have got before the worth table is
// taken again. The order of the types moves with the kit: the Ability opens at
// 160 holding five of the twelve hyper points, and what a line is worth to the
// character who spends the last of the honor is not what it was worth to the
// one who spent the first.
constexpr double kRemeasureGrowth = 1.5;

// Whether a slot holds a different ITEM than at the last allocation. A new
// piece is reason to allocate again whatever the power reading says: a weapon
// two tiers up reorders what every stat is worth. NAMES alone, so stars and
// scrolls do not count -- they land on nearly every look.
bool GearChanged(Session& run) {
  std::string worn;
  for (const std::pair<const EquipSlot, const EquipInstance*>& item :
       run.state.character.equipped()) {
    worn += item.second->name();
    worn += '\n';
  }
  if (worn == run.worn_gear) {
    return false;
  }
  run.worn_gear = std::move(worn);
  return true;
}

// The Hyper Stat points the levels have paid out, spent on BOTH presets --
// unlike the Ability below, since the points are paid per level and cost
// nothing to move, so there is no pool to split.
void SpendHyperPoints(Session& run, bool regeared) {
  if (run.state.character.proto().level() < kHyperStatUnlockLevel) {
    return;
  }
  int power = PowerNow(run.state);
  std::pair<std::string, int> aim;
  AimedFight(run.state, &aim);
  if (!run.hyper_measured || regeared || aim != run.hyper_aim ||
      power >= run.hyper_power * kRemeasureGrowth) {
    // One basis for the whole table: a Hyper Stat point moves what the
    // character hits for, not what the catalogs hand over for a kill.
    DropBasis basis = DropBasisFor(run.state, run.shopper.power_per_meso(),
                                   run.shopper.yardstick());
    run.hyper_farming = MeasureHyperWorth(
        run.state, StatPreset::kFirst, [&basis](GameState& state) {
          return CrowdRateOver(state, basis, kBookSeconds);
        });
    run.hyper_bossing = MeasureHyperWorth(
        run.state, StatPreset::kSecond, [&basis](GameState& state) {
          return BossRateOver(state, basis, kBookSeconds);
        });
    run.hyper_measured = true;
    run.hyper_power = power;
    run.hyper_aim = aim;
  }
  // Redone every look rather than only on a fresh table: the pool grows a
  // level at a time, and a new point can be worth moving an old one.
  SpendHyperStats(run.state, StatPreset::kFirst, run.hyper_farming);
  SpendHyperStats(run.state, StatPreset::kSecond, run.hyper_bossing);
}

// The honor collected, spent on the BOSSING Inner Ability alone: one pool, and
// a character this early cannot finish both. The farming table is measured
// anyway, being one more pass and what says whether that is still right.
//
// New gear does NOT force the table here, though it forces the Hyper Stats: a
// fresh table can name a different goal line, restarting a chase that a dry
// pool then leaves half done. Measured both ways -- forcing it cost a Cygnus
// clear and bought nothing the Hyper Stats did not.
void SpendHonor(Session& run) {
  if (!run.state.character.inner_ability_unlocked()) {
    return;
  }
  int power = PowerNow(run.state);
  std::pair<std::string, int> aim;
  AimedFight(run.state, &aim);
  if (!run.ability_measured || aim != run.ability_aim ||
      power >= run.ability_power * kRemeasureGrowth) {
    DropBasis basis = DropBasisFor(run.state, run.shopper.power_per_meso(),
                                   run.shopper.yardstick());
    // Over a buff cycle rather than the ten seconds the book is ranked on:
    // Buff Duration is one of the lines being priced, and it is invisible to
    // any window a buff does not lapse inside. See WindowFor.
    run.farming_worth = MeasureAbilityWorth(
        run.state, StatPreset::kFirst, [&basis](GameState& state) {
          return CrowdRateOver(state, basis, kBookSeconds);
        });
    run.bossing_worth = MeasureAbilityWorth(
        run.state, StatPreset::kSecond, [&basis](GameState& state) {
          return BossRateOver(state, basis, kBookSeconds);
        });
    run.ability_measured = true;
    run.ability_power = power;
    run.ability_aim = aim;
    run.climb.farming_worth = run.farming_worth;
    run.climb.bossing_worth = run.bossing_worth;
  }
  run.climb.ability_honor_spent += SpendHonorOnAbility(
      run.state, AbilityRankWanted(), StatPreset::kSecond, run.bossing_worth);
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

// Every fight open at this level, gathered under its boss and hardest first.
// The bosses come in the order they first opened, which is the order a player
// met them.
std::vector<std::pair<std::string, std::vector<int>>> OpenLadders(
    const GameState& state, int level) {
  std::vector<std::pair<std::string, std::vector<int>>> ladders;
  std::map<std::string, int> where;
  for (const std::pair<std::string, int>& open : UnlockedBosses(state, level)) {
    std::map<std::string, int>::iterator seen = where.find(open.first);
    if (seen == where.end()) {
      where[open.first] = static_cast<int>(ladders.size());
      ladders.push_back({open.first, {open.second}});
      continue;
    }
    ladders[seen->second].second.push_back(open.second);
  }
  // UnlockedBosses hands them over easiest first, and the ladder is walked
  // the other way.
  for (std::pair<std::string, std::vector<int>>& ladder : ladders) {
    std::reverse(ladder.second.begin(), ladder.second.end());
  }
  return ladders;
}

// A clear closes the boss for the day at every difficulty, not just the one
// that won it: the lockout is on the boss.
void CloseForToday(Session& run,
                   const std::pair<std::string, std::vector<int>>& boss) {
  for (int index : boss.second) {
    run.fights[FightKey(boss.first, index)].cleared_today = true;
  }
}

// Takes on every fight that is open and worth a try, wears what dropped, and
// says whether any was fought. A loss costs the CLOCK and nothing else: the
// day is spent by beating a boss, not by walking into one, so a player who
// misses goes again rather than waiting out the reset.
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
  for (const std::pair<std::string, std::vector<int>>& boss :
       OpenLadders(run.state, level)) {
    // Hardest first, and down a rung whenever that one is beyond them: the
    // clear pays what the difficulty is worth, and it closes the boss for the
    // day whichever rung won it.
    for (int index : boss.second) {
      FightState& fight = run.fights[FightKey(boss.first, index)];
      if (!WorthATry(run, fight, levelled, power)) {
        continue;
      }
      fight.attempted = true;
      fight.power_at_last_try = power;
      BossOutcome outcome;
      spent += FightOnce(run.state, {boss.first, index}, level, power,
                         run.seconds + spent, run.climb, &outcome);
      if (outcome.won) {
        CloseForToday(run, boss);
        break;
      }
      fight.retry_at = run.seconds + spent + kRetrySeconds;
      fight.near_miss = outcome.left <= kNearMiss;
      // A near miss keeps them at the keyboard, so the next look comes forward
      // to meet it rather than waiting for the evening.
      if (fight.near_miss) {
        run.next_look = std::min(run.next_look, fight.retry_at);
      }
    }
  }
  if (spent <= 0.0) {
    return false;
  }
  run.seconds += spent;
  AfterFighting(run);
  return true;
}

// Writing a climb down and picking it up again. Every one of these walks a
// struct field for field, so a row added above without a line here is a row a
// resumed run comes back without.
template <typename Field, typename T>
void SaveArray(const T* from, int count, Field* to) {
  for (int i = 0; i < count; ++i) {
    to->Add(from[i]);
  }
}

template <typename Field, typename T>
void LoadArray(const Field& from, int count, T* to) {
  for (int i = 0; i < count && i < from.size(); ++i) {
    to[i] = from.Get(i);
  }
}

void SaveWorth(const AbilityWorth& worth, CheckpointWorth* to) {
  for (int type = 0; type < AbilityLineType_ARRAYSIZE; ++type) {
    for (int rank = 0; rank < AbilityRank_ARRAYSIZE; ++rank) {
      to->add_rate(worth.rate[type][rank]);
    }
  }
}

void LoadWorth(const CheckpointWorth& from, AbilityWorth* worth) {
  int i = 0;
  for (int type = 0; type < AbilityLineType_ARRAYSIZE; ++type) {
    for (int rank = 0; rank < AbilityRank_ARRAYSIZE; ++rank, ++i) {
      if (i < from.rate_size()) {
        worth->rate[type][rank] = from.rate(i);
      }
    }
  }
}

void SaveBossLogs(const Climb& climb, CheckpointClimb* to) {
  for (const std::pair<const std::string, BossLog>& entry : climb.bosses) {
    CheckpointBossLog* log = to->add_bosses();
    log->set_fight(entry.first);
    log->set_label(entry.second.label);
    log->set_boss(entry.second.boss);
    log->set_difficulty(entry.second.difficulty);
    log->set_unlock_level(entry.second.unlock_level);
    log->set_attempts(entry.second.attempts);
    log->set_clears(entry.second.clears);
    log->set_first_attempt_level(entry.second.first_attempt_level);
    log->set_first_clear_level(entry.second.first_clear_level);
    log->set_first_clear_seconds(entry.second.first_clear_seconds);
    log->set_best_done(entry.second.best_done);
    log->set_best_seconds(entry.second.best_seconds);
    log->set_best_power(entry.second.best_power);
  }
}

void LoadBossLogs(const CheckpointClimb& from, Climb* climb) {
  for (const CheckpointBossLog& saved : from.bosses()) {
    BossLog& log = climb->bosses[saved.fight()];
    log.label = saved.label();
    log.boss = saved.boss();
    log.difficulty = saved.difficulty();
    log.unlock_level = saved.unlock_level();
    log.attempts = saved.attempts();
    log.clears = saved.clears();
    log.first_attempt_level = saved.first_attempt_level();
    log.first_clear_level = saved.first_clear_level();
    log.first_clear_seconds = saved.first_clear_seconds();
    log.best_done = saved.best_done();
    log.best_seconds = saved.best_seconds();
    log.best_power = saved.best_power();
  }
}

void SaveStint(const Stint& stint, CheckpointStint* to) {
  to->set_level(stint.level);
  to->set_seconds(stint.seconds);
  to->set_map(stint.map);
  to->set_weapon(stint.weapon);
}

Stint LoadStint(const CheckpointStint& from) {
  return {from.level(), from.seconds(), from.map(), from.weapon()};
}

void SaveMilestone(const Milestone& milestone, CheckpointMilestone* to) {
  to->set_seconds(milestone.seconds);
  to->set_meso(milestone.meso);
  to->set_spent(milestone.spent);
  to->set_stars(milestone.stars);
  to->set_slots(milestone.slots);
  to->set_frozen(milestone.frozen);
  to->set_boss_set(milestone.boss_set);
  to->set_honor(milestone.honor);
  to->set_level_honor(milestone.level_honor);
  to->set_boss_honor(milestone.boss_honor);
}

Milestone LoadMilestone(const CheckpointMilestone& from) {
  return {from.seconds(),     from.meso(),      from.spent(),    from.stars(),
          from.slots(),       from.frozen(),    from.boss_set(), from.honor(),
          from.level_honor(), from.boss_honor()};
}

void SaveToken(const TokenProgress& token, CheckpointToken* to) {
  to->set_level(token.level);
  to->set_kills(token.kills);
  to->set_log_miss(token.log_miss);
}

TokenProgress LoadToken(const CheckpointToken& from) {
  return {from.level(), from.kills(), from.log_miss()};
}

void SaveClimb(const Climb& climb, CheckpointClimb* to) {
  for (const Milestone& milestone : climb.milestones) {
    SaveMilestone(milestone, to->add_milestones());
  }
  to->set_ability_honor_spent(climb.ability_honor_spent);
  *to->mutable_ability_farming() = climb.ability_farming;
  *to->mutable_ability_bossing() = climb.ability_bossing;
  SaveWorth(climb.farming_worth, to->mutable_farming_worth());
  SaveWorth(climb.bossing_worth, to->mutable_bossing_worth());
  to->set_ten_star_level(climb.ten_star_level);
  to->set_frozen_set_level(climb.frozen_set_level);
  SaveBossLogs(climb, to);
  SaveArray(climb.frozen_level, kNumFrozenPieces, to->mutable_frozen_level());
  for (const TokenProgress& token : climb.tokens) {
    SaveToken(token, to->add_tokens());
  }
  for (const Stint& stint : climb.stints) {
    SaveStint(stint, to->add_stints());
  }
}

void LoadClimb(const CheckpointClimb& from, Climb* climb) {
  for (int i = 0; i < kNumMilestones && i < from.milestones_size(); ++i) {
    climb->milestones[i] = LoadMilestone(from.milestones(i));
  }
  climb->ability_honor_spent = from.ability_honor_spent();
  climb->ability_farming = from.ability_farming();
  climb->ability_bossing = from.ability_bossing();
  LoadWorth(from.farming_worth(), &climb->farming_worth);
  LoadWorth(from.bossing_worth(), &climb->bossing_worth);
  climb->ten_star_level = from.ten_star_level();
  climb->frozen_set_level = from.frozen_set_level();
  LoadBossLogs(from, climb);
  LoadArray(from.frozen_level(), kNumFrozenPieces, climb->frozen_level);
  for (int i = 0; i < kNumFrozenTokens && i < from.tokens_size(); ++i) {
    climb->tokens[i] = LoadToken(from.tokens(i));
  }
  for (const CheckpointStint& stint : from.stints()) {
    climb->stints.push_back(LoadStint(stint));
  }
}

// The two random streams, written the way their own library writes them.
std::string SaveRng(const std::mt19937& rng) {
  std::ostringstream text;
  text << rng;
  return text.str();
}

void LoadRng(const std::string& saved, std::mt19937* rng) {
  std::istringstream text(saved);
  text >> *rng;
}

SimCheckpoint SaveRun(const Session& run, const ClimbCursor& cursor) {
  SimCheckpoint saved;
  saved.set_stamp(run.saves->stamp);
  saved.set_branch(run.key);
  saved.set_seed(run.seed);
  saved.set_level(cursor.level);
  *saved.mutable_character() = run.state.character.ToProto();
  saved.set_current_map(run.state.current_map);
  saved.set_taken(run.taken);
  saved.set_earned(run.purse.earned);
  saved.set_spent(run.purse.spent);
  saved.set_held(run.purse.held);
  saved.set_scout_weapon(run.scout.settled);
  saved.set_scout_level(run.scout.settled_at);
  saved.set_seconds(run.seconds);
  saved.set_next_daily(run.next_daily);
  saved.set_next_look(run.next_look);
  saved.set_looks_left(run.looks_left);
  for (const std::pair<const std::string, FightState>& entry : run.fights) {
    CheckpointFight* fight = saved.add_fights();
    fight->set_fight(entry.first);
    fight->set_attempted(entry.second.attempted);
    fight->set_cleared_today(entry.second.cleared_today);
    fight->set_near_miss(entry.second.near_miss);
    fight->set_retry_at(entry.second.retry_at);
    fight->set_power_at_last_try(entry.second.power_at_last_try);
  }
  saved.set_ability_measured(run.ability_measured);
  saved.set_ability_power(run.ability_power);
  saved.set_world_rng(SaveRng(run.state.rng));
  saved.set_run_rng(SaveRng(run.rng));
  SaveClimb(run.climb, saved.mutable_climb());
  saved.set_level_began(cursor.level_began);
  SaveStint(cursor.stint, saved.mutable_stint());
  for (double carried : cursor.carry) {
    saved.add_carry(carried);
  }
  return saved;
}

void LoadRun(const SimCheckpoint& saved, Session& run, ClimbCursor* cursor) {
  run.state.character.RestoreFrom(saved.character(), run.state.equips,
                                  run.state.items);
  run.state.current_map = saved.current_map();
  run.taken = saved.taken();
  run.purse.earned = saved.earned();
  run.purse.spent = saved.spent();
  run.purse.held = saved.held();
  run.scout.settled = static_cast<EquipType>(saved.scout_weapon());
  run.scout.settled_at = saved.scout_level();
  run.seconds = saved.seconds();
  run.next_daily = saved.next_daily();
  run.next_look = saved.next_look();
  run.looks_left = saved.looks_left();
  for (const CheckpointFight& saved_fight : saved.fights()) {
    FightState& fight = run.fights[saved_fight.fight()];
    fight.attempted = saved_fight.attempted();
    fight.cleared_today = saved_fight.cleared_today();
    fight.near_miss = saved_fight.near_miss();
    fight.retry_at = saved_fight.retry_at();
    fight.power_at_last_try = saved_fight.power_at_last_try();
  }
  run.ability_measured = saved.ability_measured();
  run.ability_power = saved.ability_power();
  LoadRng(saved.world_rng(), &run.state.rng);
  LoadRng(saved.run_rng(), &run.rng);
  LoadClimb(saved.climb(), &run.climb);
  run.farming_worth = run.climb.farming_worth;
  run.bossing_worth = run.climb.bossing_worth;
  cursor->level = saved.level();
  cursor->level_began = saved.level_began();
  cursor->stint = LoadStint(saved.stint());
  cursor->carry.assign(saved.carry().begin(), saved.carry().end());
}

// Writes the climb down the first time it stands at the checkpoint level.
void NoteCheckpoint(Session& run, const ClimbCursor& cursor) {
  if (run.saved || run.saves == nullptr || run.saves->level <= 0 ||
      cursor.level < run.saves->level) {
    return;
  }
  run.saved = true;
  WriteCheckpoint(run.saves->dir, run.key, SaveRun(run, cursor));
}

// Picks a written-down climb up, if this build wrote one for this branch and
// seed. A run that resumes has nothing left to write.
bool ResumeClimb(Session& run, ClimbCursor* cursor) {
  SimCheckpoint saved;
  if (run.saves == nullptr || run.saves->level <= 0 ||
      !ReadCheckpoint(run.saves->dir, run.key, run.saves->stamp, &saved)) {
    return false;
  }
  LoadRun(saved, run, cursor);
  run.saved = true;
  return true;
}

// When the climb gives up, whether or not it has reached the cap. Under
// --total_days the whole run has one budget and the climb may spend all of it.
double GiveUpAt() {
  double total = absl::GetFlag(FLAGS_total_days);
  if (total > 0.0) {
    return total * kDaySeconds;
  }
  return absl::GetFlag(FLAGS_give_up_hours) * 3600.0;
}

// The daily claim, taken at a look before the bag is tidied, so what it packs
// goes straight into the worn symbols. Refused for a full bag, and then the
// next look tries again, as a player would.
void ClaimDailySymbols(Session& run) {
  ClaimDailies(run.state.character, run.state.equips,
               kSimEpoch + static_cast<int64_t>(run.seconds));
}

// What every look at the game does once the shopping is settled: buy the gear
// the purse can now afford, then spend the points that were waiting.
void Restock(Session& run) {
  ClaimDailySymbols(run);
  Retool(run.state, run.path, &run.taken, run.maps, run.beats, run.step,
         run.purse, run.shopper, run.scout, run.planned, run.toggles,
         run.mapped, run.climb.ledger);
  SpendHyperPoints(run, GearChanged(run));
  SpendHonor(run);
}

// The player opening the game. The potion plan and the shopper's income come
// ahead of the gear: a buff that pays its price back in a day multiplies every
// meso the rest of the run earns, and a star bought first is a star bought
// with the slower purse.
void TakeLook(Session& run, const CombatParams& params, const Yield& yield) {
  PlanBuffsFor(run, params, yield);
  SetShopperIncome(run, params, yield);
  run.purse.Note(run.state.character);
  Restock(run);
}

// Picks the climb up where a checkpoint left it, or starts it from scratch.
void BeginClimb(Session& run, ClimbCursor& cursor) {
  if (ResumeClimb(run, &cursor)) {
    return;
  }
  Restock(run);
  run.next_look = NextLook(run.seconds, run.state.character.proto().level(),
                           &run.looks_left, run.rng);
  cursor.level = run.state.character.proto().level();
  cursor.stint = {cursor.level, 0.0, run.state.current_map,
                  HeldWeaponName(run.state.character)};
}

// Jumps the run forward by `horizon` and pays out what the stretch earned.
void EarnOver(Session& run, const CombatParams& params, const Yield& yield,
              double horizon, ClimbCursor& cursor) {
  std::vector<int64_t> kills = KillsOver(yield, horizon, &cursor.carry);
  AwardCombatRewards(run.state, params, kills);
  NoteTokenChances(params, kills, run.climb);
  // The stretch is jumped rather than ticked, so the potion is charged for it
  // here -- AdvanceCombat, which does it in the game, never runs.
  DrinkBuffs(run.state, horizon, &run.climb.ledger.buffs);
  run.purse.Note(run.state.character);
  run.seconds += horizon;
}

// Closes the stint the level just ended and opens the next one.
void NoteLevelReached(Session& run, ClimbCursor& cursor, int reached) {
  cursor.stint.seconds = run.seconds - cursor.level_began;
  run.climb.stints.push_back(cursor.stint);
  cursor.level_began = run.seconds;
  cursor.level = reached;
  NoteFrozenDrops(run.state, cursor.level, run.climb);
  NoteMilestones(run.state, cursor.level, run.seconds, run.purse, run.climb);
  cursor.stint = {cursor.level, 0.0, run.state.current_map,
                  HeldWeaponName(run.state.character)};
}

void ClimbToCap(Session& run) {
  double give_up = GiveUpAt();
  run.horizon = give_up;
  ClimbCursor cursor;
  BeginClimb(run, cursor);
  CombatParams params = ComputeCombatParams(run.state);
  while (cursor.level < kTrialLevelCap && run.seconds < give_up) {
    NoteCheckpoint(run, cursor);
    Yield yield = MeasureYield(run.state, params, run.beats, run.step);
    if (yield.died || yield.exp_per_second <= 0.0) {
      // Nowhere they can stand, or nothing to be earned standing there. Ask
      // for a map again and, if there is still none, let the give-up clock
      // have it rather than spinning.
      PickMap(run.state, run.maps, run.beats, run.step,
              run.shopper.power_per_meso(), run.shopper.yardstick());
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
        (ExpToNextLevel(cursor.level) - proto.exp()) / yield.exp_per_second;
    double horizon = std::min(std::max(to_level, run.step),
                              std::max(run.next_look - run.seconds, run.step));
    horizon = std::min(horizon, give_up - run.seconds);
    EarnOver(run, params, yield, horizon, cursor);

    int reached = run.state.character.proto().level();
    bool levelled = reached != cursor.level;
    bool watching = absl::GetFlag(FLAGS_attention) <= 0.0;
    bool looked = false;
    if (watching ? levelled : run.seconds >= run.next_look) {
      if (!watching) {
        run.next_look =
            NextLook(run.seconds, reached, &run.looks_left, run.rng);
      }
      TakeLook(run, params, yield);
      looked = true;
    }
    if (levelled) {
      NoteLevelReached(run, cursor, reached);
      // The character got stronger whether or not anybody was watching, and
      // the fight is built off what they are.
      looked = true;
    }
    // After retooling, so a fight is taken on in the gear this look bought
    // rather than in what the last one left.
    if (looked && TakeOnBosses(run, reached, levelled)) {
      PickMap(run.state, run.maps, run.beats, run.step,
              run.shopper.power_per_meso(), run.shopper.yardstick());
    }
    if (looked) {
      params = ComputeCombatParams(run.state);
      cursor.stint.map = run.state.current_map;
      cursor.stint.weapon = HeldWeaponName(run.state.character);
    }
  }
}

// The hourly look the endgame takes, in place of the climb's look on levelling
// up. The Etc tab is CLEARED first, as the climb's retool does: the tab holds
// 128 stacks, twenty days of drops fill it, and a full one refuses the spell
// traces every scroll is bought with.
void RestockAtCap(Session& run, const CombatParams& params,
                  const Yield& yield) {
  run.climb.ledger.etc_sales += SellDrops(run.state.character);
  ClaimDailySymbols(run);
  WearBestFromBag(run.state.character);
  CollectSymbols(run.state.character);
  // Before the shelf, for the reason the climb takes it before Retool.
  PlanBuffsFor(run, params, yield);
  SetShopperIncome(run, params, yield);
  run.purse.Note(run.state.character);
  int64_t before_shelf = run.state.character.meso();
  Outfit(run.state, /*budget=*/true);
  run.climb.ledger.gear_bought +=
      std::max<int64_t>(0, before_shelf - run.state.character.meso());
  run.shopper.Spend(run.state);
  run.purse.Note(run.state.character);
  SpendHyperPoints(run, GearChanged(run));
  SpendHonor(run);
  // Hourly, but only re-asked once they have outgrown the answer: probing the
  // shelf of maps plays a fight on every one of them, and twenty endgame days
  // of hourly looks is most of the section.
  int power = PowerNow(run.state);
  if (power >= run.mapped.power * kRemeasureGrowth) {
    run.mapped.power = power;
    PickMoneyMap(run.state, run.maps, run.beats, run.step,
                 run.shopper.power_per_meso(), run.shopper.yardstick());
  }
  run.climb.money_map = run.state.current_map;
}

// What the endgame section reached, read off the character once its days are
// played out.
void NoteEndgame(Session& run, double began, int64_t earned_at_cap,
                 int64_t spent_at_cap) {
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
  run.climb.potentials = PotentialsWorn(run.state);
}

// Plays the days after the cap: the map that pays the most meso a second
// rather than the most EXP, the dailies as they fall due, and the purse still
// spending on whatever it can afford.
void FarmAtCap(Session& run) {
  double began = run.seconds;
  double total = absl::GetFlag(FLAGS_total_days);
  double horizon =
      total > 0.0 ? total * kDaySeconds
                  : began + absl::GetFlag(FLAGS_endgame_days) * kDaySeconds;
  // The endgame's own end, which is the run's: the climb was weighing a
  // permanent purchase against a give-up clock nobody reaches.
  run.horizon = horizon;
  int level = run.state.character.proto().level();
  int64_t earned_at_cap = run.purse.earned;
  int64_t spent_at_cap = run.purse.spent;

  PickMoneyMap(run.state, run.maps, run.beats, run.step,
               run.shopper.power_per_meso(), run.shopper.yardstick());
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
      PickMoneyMap(run.state, run.maps, run.beats, run.step,
                   run.shopper.power_per_meso(), run.shopper.yardstick());
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
    DrinkBuffs(run.state, jump, &run.climb.ledger.buffs);
    run.purse.Note(run.state.character);
    run.seconds += jump;
    bool fought = TakeOnBosses(run, level, /*levelled=*/false);
    if (run.seconds >= next_retool) {
      next_retool += kDaySeconds / 24.0;
      RestockAtCap(run, params, yield);
      fought = true;
    }
    if (fought) {
      params = ComputeCombatParams(run.state);
    }
  }
  NoteEndgame(run, began, earned_at_cap, spent_at_cap);
}

// Everything the flags say about how a character climbs. A checkpoint written
// under one of these answers nothing about another, so it goes in the file's
// name -- two settings then keep two files rather than one quietly standing in
// for the other. What only shapes the printing, or only what happens after the
// cap, is left out.
std::string ClimbSettings() {
  return absl::StrCat(
      absl::GetFlag(FLAGS_step), ";", absl::GetFlag(FLAGS_probe_beats), ";",
      absl::GetFlag(FLAGS_give_up_hours), ";", absl::GetFlag(FLAGS_total_days),
      ";", absl::GetFlag(FLAGS_star_ceiling), ";",
      absl::GetFlag(FLAGS_scroll_rate), ";", absl::GetFlag(FLAGS_cubes), ";",
      absl::GetFlag(FLAGS_dailies), ";", absl::GetFlag(FLAGS_attention), ";",
      absl::GetFlag(FLAGS_ability_rank), ";",
      absl::GetFlag(FLAGS_checkpoint_at));
}

// What one climb's file is called: the branch, the seed and the settings --
// which between them are everything that makes two climbs differ.
std::string ClimbKey(Job branch, unsigned int seed) {
  char settled[24];
  std::snprintf(settled, sizeof(settled), "%08x",
                static_cast<unsigned int>(
                    std::hash<std::string>()(ClimbSettings()) & 0xffffffffu));
  return absl::StrCat(
      absl::AsciiStrToLower(Job_Name(branch).substr(std::strlen("JOB_"))), "_",
      seed, "_", settled);
}

// Plays the character forward to the level cap, or until the give-up clock
// runs out.
Climb Play(const Catalogs& catalogs, Job branch,
           const std::vector<std::string>& maps, unsigned int seed,
           const Checkpointing& saves) {
  GameState state = NewState(catalogs, seed);
  // A plain field rather than something the constructor takes, so a sim that
  // fights one has to say so. This one runs the dailies.
  state.bosses = catalogs.bosses;
  Climb climb;
  GearPlan plan;
  plan.star_ceiling = absl::GetFlag(FLAGS_star_ceiling);
  plan.scroll_rate = absl::GetFlag(FLAGS_scroll_rate);
  plan.cubes = absl::GetFlag(FLAGS_cubes);

  Session run = {state,          maps,
                 PathTo(branch), 0,
                 Purse(),        GearShopper(plan),
                 WeaponScout(),  PlanKey(),
                 ToggleChoice(), MapChoice(),
                 climb};
  run.step = absl::GetFlag(FLAGS_step);
  run.beats = absl::GetFlag(FLAGS_probe_beats);
  run.rng.seed(seed);
  run.saves = &saves;
  run.key = ClimbKey(branch, seed);
  run.seed = seed;
  ClimbToCap(run);
  if (absl::GetFlag(FLAGS_endgame) &&
      state.character.proto().level() >= kTrialLevelCap) {
    FarmAtCap(run);
  }
  // The shopper's whole life, read where every run ends rather than where the
  // endgame section does: it spends on every level of the climb, and a run
  // that never reached the cap was reporting none of it.
  climb.ledger.gear = run.shopper.life();
  climb.booms = run.shopper.life().booms;
  climb.ability_farming = state.character.ability(StatPreset::kFirst);
  climb.ability_bossing = state.character.ability(StatPreset::kSecond);
  // ToProto, not proto(): the live containers hold the gear, and the backing
  // message they came out of has none of it. See MeasureAbilityWorth.
  climb.final_character = state.character.ToProto();
  climb.endgame_earned_total = run.purse.earned;
  climb.meso_held = state.character.meso();
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
                static_cast<long long>(climbs[i].tokens[0].kills));
    for (int token = 0; token < kNumFrozenTokens; ++token) {
      std::printf("  %17.1f%%",
                  100.0 * std::exp(climbs[i].tokens[token].log_miss));
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
      std::printf("  %8s", Clock(climbs[i].milestones[m].seconds).c_str());
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
      FormatShort(static_cast<double>(climbs[i].milestones[m].meso), money,
                  sizeof(money));
      std::printf("  %8s", climbs[i].milestones[m].seconds < 0.0 ? "-" : money);
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
      FormatShort(static_cast<double>(climbs[i].milestones[m].honor), honor,
                  sizeof(honor));
      std::printf("  %8s", climbs[i].milestones[m].seconds < 0.0 ? "-" : honor);
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
      if (climb.milestones[m].seconds < 0.0) {
        std::printf("  %9s %9s %9s %9s", "-", "-", "-", "-");
        continue;
      }
      int64_t mobs = climb.milestones[m].honor -
                     climb.milestones[m].level_honor -
                     climb.milestones[m].boss_honor;
      std::printf("  %9lld %9lld %9lld %9lld",
                  static_cast<long long>(climb.milestones[m].honor),
                  static_cast<long long>(climb.milestones[m].level_honor),
                  static_cast<long long>(climb.milestones[m].boss_honor),
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
std::vector<std::string> RatedTypes(const AbilityWorth& worth, AbilityRank rank,
                                    int most) {
  std::vector<std::pair<double, std::string>> ranked;
  for (int t = ABILITY_LINE_TYPE_STR; t < AbilityLineType_ARRAYSIZE; ++t) {
    double rate = worth.rate[t][rank];
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
  // At the preset's own rank, which is the only rank its top line can be --
  // and the rank where Attack Speed exists at all.
  for (const std::string& name : RatedTypes(worth, preset.rank(), 3)) {
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
      "farming, the fight for bossing, taken again\nwhenever they outgrow "
      "it -- and the pool is spent chasing the one line %s can\nput in the "
      "top slot, the only slot that ever carries the ability's rank.\n",
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
      int level = climbs[i].tokens[token].level;
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
  FormatShort(static_cast<double>(climb.milestones[m].meso), earned,
              sizeof(earned));
  FormatShort(static_cast<double>(climb.milestones[m].spent), spent,
              sizeof(spent));
  char weapon[16];
  if (climb.milestones[m].stars < 0) {
    std::snprintf(weapon, sizeof(weapon), "%s", "-");
  } else {
    std::snprintf(weapon, sizeof(weapon), "%d*/%d open",
                  climb.milestones[m].stars, climb.milestones[m].slots);
  }
  std::printf("  %5d  %9s  %9s  %13s  %8d  %10d\n", kMilestones[m], earned,
              spent, weapon, climb.milestones[m].frozen,
              climb.milestones[m].boss_set);
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
      if (climbs[i].milestones[m].seconds >= 0.0) {
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

// The fight a log is of, read back out of the catalog, or null if the catalog
// no longer holds it.
const BossDifficulty* FightIn(const Catalogs& catalogs, const BossLog& log) {
  std::map<std::string, Boss>::const_iterator boss =
      catalogs.bosses.find(log.boss);
  if (boss == catalogs.bosses.end() ||
      log.difficulty >= boss->second.difficulties_size()) {
    return nullptr;
  }
  return &boss->second.difficulties(log.difficulty);
}

// One boss's row: what the quickest clear took, against what the fight allows.
void PrintReadinessRow(const Catalogs& catalogs, const BossLog& log) {
  const BossDifficulty* difficulty = FightIn(catalogs, log);
  if (difficulty == nullptr) {
    return;
  }
  int clock = difficulty->time_limit_seconds();
  char rate[16];
  FormatShort(BossTotalHp(catalogs.mobs, *difficulty) / log.best_seconds, rate,
              sizeof(rate));
  std::printf("  %-18s %8s %9s %7.0f%% %10s %9d\n", log.label.c_str(),
              FightClock(log.best_seconds).c_str(), FightClock(clock).c_str(),
              100.0 * log.best_seconds / clock, rate, log.best_power);
}

// How much of each fight's clock the branch needed. A boss is beaten inside
// its limit or not beaten, so the MARGIN is the reading -- a clear at nine
// tenths is a fight the next patch takes away. Read off the climb, so what
// walks up to the boss has the AP, book and gear a player really had.
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
        PrintReadinessRow(catalogs, entry.second);
      }
    }
  }
}

// Every fight the game will actually let a player walk into, in the order
// they open. Read off the catalog rather than off the climbs so a fight
// nobody in the sweep ever reached still gets a row.
struct FightRow {
  std::string key;
  std::string boss;  // the catalog key on its own, which the key above joins
  std::string label;
  int unlock_level = 0;
  const BossDifficulty* difficulty = nullptr;
};

std::vector<FightRow> LiveFights(const Catalogs& catalogs) {
  std::vector<FightRow> fights;
  for (const std::pair<const std::string, Boss>& entry : catalogs.bosses) {
    for (int i = 0; i < entry.second.difficulties_size(); ++i) {
      const BossDifficulty& difficulty = entry.second.difficulties(i);
      if (difficulty.coming_soon()) {
        continue;
      }
      fights.push_back(
          {FightKey(entry.first, i), entry.first,
           absl::StrCat(difficulty.name(), " ", entry.second.name()),
           difficulty.unlock_level(), &difficulty});
    }
  }
  std::sort(fights.begin(), fights.end(),
            [](const FightRow& a, const FightRow& b) {
              return a.unlock_level != b.unlock_level
                         ? a.unlock_level < b.unlock_level
                         : a.label < b.label;
            });
  return fights;
}

// The last level a climb was seen at, which for a run that ran out of month
// is where it stopped rather than the cap.
int LevelReached(const Climb& climb) {
  return climb.stints.empty() ? 1 : climb.stints.back().level;
}

// Where one branch stands with one fight at the end of its run.
struct FightStanding {
  int level = 0;            // the level of the first clear, 0 for never won
  double seconds = -1.0;    // the playtime that clear landed at
  double after_cap = -1.0;  // how much of that came after the cap
  double best_done = 0.0;   // the most of the fight any attempt took down
  int attempts = 0;
  int reached = 0;  // the level the branch itself got to
};

FightStanding StandingOn(const Climb& climb, const std::string& key) {
  FightStanding standing;
  standing.reached = LevelReached(climb);
  std::map<std::string, BossLog>::const_iterator log = climb.bosses.find(key);
  if (log == climb.bosses.end()) {
    return standing;
  }
  standing.attempts = log->second.attempts;
  standing.best_done = log->second.best_done;
  standing.level = log->second.first_clear_level;
  standing.seconds = log->second.first_clear_seconds;
  double cap = climb.milestones[kNumMilestones - 1].seconds;
  if (standing.level > 0 && cap >= 0.0 && standing.seconds > cap) {
    standing.after_cap = standing.seconds - cap;
  }
  return standing;
}

// Where one clear falls in a character's life. A fight beaten at the cap is
// read on the clock instead of the level, since every branch that waits that
// long would otherwise print the same capped level.
struct ClearPoint {
  int level = 0;            // 0 for a fight never won
  double after_cap = -1.0;  // playtime past the cap the clear landed at
};

std::string ClearCell(const ClearPoint& point) {
  if (point.level == 0) {
    return "never";
  }
  if (point.level < kMilestones[kNumMilestones - 1]) {
    return absl::StrCat("Lv", point.level);
  }
  return absl::StrCat("+", Clock(std::max(point.after_cap, 0.0)));
}

// The headline: every fight against every branch, one cell each. A summary
// over the row -- a median, a spread -- hides the thing the table is read for,
// which is WHICH branches are in the column that says never.
void PrintClearMatrix(const std::vector<FightRow>& fights,
                      const std::vector<Job>& branches,
                      const std::vector<Climb>& climbs) {
  std::printf("  %-20s %6s %8s", "fight", "opens", "cleared");
  for (Job branch : branches) {
    std::printf(" %7s", BranchAbbrev(branch).c_str());
  }
  std::printf("\n  %s\n", std::string(33 + 8 * branches.size(), '-').c_str());
  for (const FightRow& fight : fights) {
    std::vector<std::string> cells;
    int cleared = 0;
    for (const Climb& climb : climbs) {
      FightStanding standing = StandingOn(climb, fight.key);
      cells.push_back(ClearCell({standing.level, standing.after_cap}));
      cleared += standing.level > 0 ? 1 : 0;
    }
    std::printf("  %-20s %6s %4d/%-3d", fight.label.c_str(),
                absl::StrCat("Lv", fight.unlock_level).c_str(), cleared,
                static_cast<int>(branches.size()));
    for (const std::string& cell : cells) {
      std::printf(" %7s", cell.c_str());
    }
    std::printf("\n");
  }
}

// One fight, branch by branch, quickest to it first. The two clocks are the
// reading once a fight opens at the cap and every level column says the same.
void PrintFightDetail(const Catalogs& catalogs, const FightRow& fight,
                      const std::vector<Job>& branches,
                      const std::vector<Climb>& climbs) {
  char hp[16];
  FormatShort(BossTotalHp(catalogs.mobs, *fight.difficulty), hp, sizeof(hp));
  std::printf("\n%s -- opens Lv%d, %s HP, %s clock\n", fight.label.c_str(),
              fight.unlock_level, hp,
              FightClock(fight.difficulty->time_limit_seconds()).c_str());
  std::printf("  %-15s %11s %13s %11s %10s %6s\n", "branch", "first clear",
              "total played", "after cap", "most down", "tries");

  std::vector<std::pair<double, int>> order;
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    FightStanding standing = StandingOn(climbs[i], fight.key);
    order.push_back({standing.level > 0
                         ? standing.seconds
                         : std::numeric_limits<double>::infinity(),
                     i});
  }
  std::sort(order.begin(), order.end());

  for (const std::pair<double, int>& row : order) {
    FightStanding standing = StandingOn(climbs[row.second], fight.key);
    std::string when = "never";
    if (standing.level > 0) {
      when = absl::StrCat("Lv", standing.level);
    } else if (standing.attempts == 0) {
      // Never opened for them: they stopped short of the level it wants.
      when = absl::StrCat("<Lv", fight.unlock_level);
    }
    char done[16];
    std::snprintf(done, sizeof(done), "%.0f%%", 100.0 * standing.best_done);
    std::printf("  %-15s %11s %13s %11s %10s %6d\n",
                BranchName(branches[row.second]).c_str(), when.c_str(),
                Clock(standing.level > 0 ? standing.seconds : -1.0).c_str(),
                Clock(standing.after_cap).c_str(),
                standing.attempts == 0 ? "-" : done, standing.attempts);
  }
}

// The stiffest fight the game holds: the last rung of the ladder, which is
// where it opens and then what it is carrying. Null for a catalog with no
// fight in it at all.
const FightRow* HardestFight(const Catalogs& catalogs,
                             const std::vector<FightRow>& fights) {
  const FightRow* hardest = nullptr;
  int64_t hardest_hp = 0;
  for (const FightRow& fight : fights) {
    int64_t body = BossTotalHp(catalogs.mobs, *fight.difficulty);
    if (hardest == nullptr ||
        std::make_pair(fight.unlock_level, body) >
            std::make_pair(hardest->unlock_level, hardest_hp)) {
      hardest = &fight;
      hardest_hp = body;
    }
  }
  return hardest;
}

// How long a build is played out for to read its throughput. The clock an
// endgame fight allows, and the one almost everything past Lotus will allow:
// long enough that every cooldown in the game comes round several times, so
// what it reads is throughput rather than an opening burst.
constexpr double kDpmSeconds = 30.0 * 60.0;

// What one branch's final build takes off `fight` a minute, or 0 where it
// cannot fight at all.
double FinalDpm(const Catalogs& catalogs, const FightRow& fight,
                const Climb& climb) {
  GameState state = NewState(catalogs, 1);
  state.bosses = catalogs.bosses;
  state.character.RestoreFrom(climb.final_character, state.equips, state.items);
  int phase = BossObjectivePhase(state.mobs, *fight.difficulty);
  CombatParams params =
      ComputeBossParams(state, fight.boss, *fight.difficulty, phase);
  if (!params.active) {
    return 0.0;
  }
  Sequence played = MeasureFight(params, kDpmSeconds);
  return played.seconds > 0.0 ? played.damage / played.seconds * 60.0 : 0.0;
}

// What every branch's final build does to the hardest fight, MEASURED rather
// than fought: its parts stand the whole clock and never fall, so a branch
// that never won still reads a throughput. Played over kDpmSeconds, not put
// through WindowFor: a boss runs in real seconds at any level.
void PrintFinalDpm(const Catalogs& catalogs,
                   const std::vector<FightRow>& fights,
                   const std::vector<Job>& branches,
                   const std::vector<Climb>& climbs) {
  const FightRow* fight = HardestFight(catalogs, fights);
  if (fight == nullptr) {
    return;
  }
  int64_t body = BossTotalHp(catalogs.mobs, *fight->difficulty);
  char hp[16];
  FormatShort(static_cast<double>(body), hp, sizeof(hp));
  std::printf(
      "\nWhat each branch's final build does to %s, measured rather than "
      "cleared: its parts stood\nup for %s and never falling, at the bossing "
      "preset. %s to take down, so the clear that\nimplies is what the "
      "throughput asks for with nothing left to the clock.\n\n",
      fight->label.c_str(), FightClock(kDpmSeconds).c_str(), hp);
  std::printf("  %-16s %10s %10s\n", "branch", "DPM", "implies");
  std::printf("  %s\n", std::string(38, '-').c_str());

  std::vector<std::pair<double, int>> order;
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    order.push_back({FinalDpm(catalogs, *fight, climbs[i]), i});
  }
  std::sort(order.begin(), order.end(), std::greater<std::pair<double, int>>());
  for (const std::pair<double, int>& row : order) {
    char dpm[16];
    FormatShort(row.first, dpm, sizeof(dpm));
    std::printf(
        "  %-16s %10s %10s\n", BranchName(branches[row.second]).c_str(), dpm,
        Clock(row.first > 0.0 ? body / (row.first / 60.0) : -1.0).c_str());
  }
}

// When each fight falls in a character's life: the level of the first clear,
// or -- for the fights that only open at the cap -- the clock. The point of a
// run under --total_days: a fight nobody beats inside the month is not merely
// late, and how far they got says whether it is tuning or a wall.
void PrintBossTimeline(const Catalogs& catalogs,
                       const std::vector<Job>& branches,
                       const std::vector<Climb>& climbs) {
  std::printf(
      "\nWhere each fight falls, over the typical run of every branch: the "
      "level its first clear\ncame at. A cell reading +Nh is a clear at the "
      "cap, timed from Lv%d, since every branch\nthat waited that long would "
      "otherwise print the same level.\n\n",
      kTrialLevelCap);
  std::vector<FightRow> fights = LiveFights(catalogs);
  PrintClearMatrix(fights, branches, climbs);
  PrintFinalDpm(catalogs, fights, branches, climbs);
  for (const FightRow& fight : fights) {
    PrintFightDetail(catalogs, fight, branches, climbs);
  }
}

// What each branch's ignored defence leaves of the fight they are aimed at.
//
// Defence multiplies the whole swing, and past the point ignored defence stops
// cancelling it the factor clamps at zero and every line floors at 1 damage.
// So a small number here is not a little behind: a branch reading 0% is not
// fighting the boss at all.
//
// The CHARACTER's ignored defence, not a swing's. A skill carrying its own
// meets a different number, and its branch clears fights this says it
// cannot.
void PrintDefence(const Catalogs& catalogs, const std::vector<Job>& branches,
                  const std::vector<Climb>& climbs) {
  std::printf(
      "\nWhat each branch's ignored defence leaves of the fight their points "
      "are aimed at. The\nfactor multiplies the whole swing and clamps at "
      "zero, so a branch reading 0%% is on the\n1-damage floor rather than "
      "merely behind.\n\n");
  std::printf("%-16s  %8s  %26s  %8s  %8s\n", "branch", "IED", "aimed at",
              "its DEF", "left");
  std::printf("  %s\n", std::string(74, '-').c_str());
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    GameState state = NewState(catalogs, 1);
    state.bosses = catalogs.bosses;
    state.character.RestoreFrom(climbs[i].final_character, state.equips,
                                state.items);
    DerivedStats derived = DerivedStatsFor(state.character, state.skills, {},
                                           state.party, Activity::kBossing);
    std::pair<std::string, int> fight;
    if (!AimedFight(state, &fight)) {
      continue;
    }
    const BossDifficulty& difficulty =
        state.bosses[fight.first].difficulties(fight.second);
    double pdr = BossPdr(state.mobs, difficulty) / 100.0;
    double left = std::max(0.0, 1.0 - pdr * (1.0 - derived.ied));
    std::string aimed =
        absl::StrCat(difficulty.name(), " ", state.bosses[fight.first].name());
    std::printf("%-16s  %7.0f%%  %26s  %7.0f%%  %7.0f%%\n",
                BranchName(branches[i]).c_str(), 100.0 * derived.ied,
                aimed.c_str(), 100.0 * pdr, 100.0 * left);
  }
}

// What the cubing came to: what each branch paid, how much landed a roll worth
// keeping, and -- for the branch printed in full -- the lines every cubed piece
// ended up wearing. BOUGHT against KEPT is the reading that matters: a cube is
// a chance rather than a purchase, so the gap is the meso that bought nothing.
void PrintCubing(const std::vector<Job>& branches,
                 const std::vector<Climb>& climbs) {
  std::printf(
      "\nWhat the cubing came to. Kept is the rolls that beat what the piece "
      "already held;\nthe rest is the price of the chance.\n\n");
  std::printf("%-13s %9s %8s %8s %6s\n", "branch", "meso", "cubes", "kept",
              "kept%");
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    const GearSpend& gear = climbs[i].ledger.gear;
    char meso[16];
    FormatShort(static_cast<double>(gear.cubes), meso, sizeof(meso));
    std::printf("%-13s %9s %8d %8d %5.0f%%\n", BranchName(branches[i]).c_str(),
                meso, gear.cubes_bought, gear.cubes_kept,
                gear.cubes_bought == 0
                    ? 0.0
                    : 100.0 * gear.cubes_kept / gear.cubes_bought);
  }
  int weakest = 0;
  for (int i = 1; i < static_cast<int>(climbs.size()); ++i) {
    if (climbs[i].ledger.gear.cubes > climbs[weakest].ledger.gear.cubes) {
      weakest = i;
    }
  }
  if (climbs[weakest].potentials.empty()) {
    return;
  }
  std::printf(
      "\n%s's potentials, as the run left them. A piece marked "
      "\"replaceable\" was\ndiscounted: the catalog still offers "
      "something better for that slot.\n\n",
      BranchName(branches[weakest]).c_str());
  for (const PotentialRow& row : climbs[weakest].potentials) {
    std::printf(
        "  %-18s %-27s %-10s %-13s %s\n", row.slot.c_str(), row.item.c_str(),
        WithoutPrefix(PotentialRank_Name(row.rank), "POTENTIAL_RANK_").c_str(),
        row.replaceable ? "replaceable" : "",
        absl::StrJoin(row.lines, ", ").c_str());
  }
}

// Where every branch's meso came from and where it went. Mob drops are what is
// left over: everything combat paid that no named source claims.
void PrintMesoLedger(const std::vector<Job>& branches,
                     const std::vector<Climb>& climbs) {
  std::printf(
      "\nWhere the meso came from and where it went, over the whole run. Mobs "
      "is the remainder --\neverything the purse was paid that no named "
      "source claims.\n\n");
  const char* kHeads[] = {"mobs",     "Etc sold", "gear sold", "bosses",
                          "shelf",    "scrolls",  "stars",     "hammers",
                          "cubes",    "symbols",  "copies",    "buffs",
                          "buffs own"};
  std::printf("%-13s", "branch");
  for (const char* head : kHeads) {
    std::printf(" %9s", head);
  }
  std::printf(" %9s\n", "held");
  for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
    const Ledger& ledger = climbs[i].ledger;
    int64_t earned = climbs[i].endgame_earned_total;
    int64_t rows[] = {earned - ledger.named_income(),
                      ledger.etc_sales,
                      ledger.gear.sold,
                      ledger.boss_clears,
                      ledger.gear_bought,
                      ledger.gear.scrolls,
                      ledger.gear.stars,
                      ledger.gear.hammers,
                      ledger.gear.cubes,
                      ledger.gear.symbols,
                      ledger.gear.replacements,
                      ledger.buffs.drained,
                      ledger.buffs.bought};
    std::printf("%-13s", BranchName(branches[i]).c_str());
    for (int64_t value : rows) {
      char text[16];
      FormatShort(static_cast<double>(value), text, sizeof(text));
      std::printf(" %9s", text);
    }
    char held[16];
    FormatShort(static_cast<double>(climbs[i].meso_held), held, sizeof(held));
    std::printf(" %9s\n", held);
  }
}

// The branch that got least of the way through the boss roster: over every
// fight the game opens, the share of it they never took down, summed. A fight
// they never reached at all counts whole, which is what makes this a measure
// of how much of the game is shut to them rather than of how close one fight
// came.
int WeakestBranch(const Catalogs& catalogs, const std::vector<Climb>& climbs) {
  std::vector<FightRow> fights = LiveFights(catalogs);
  int weakest = 0;
  double most_left = -1.0;
  for (int i = 0; i < static_cast<int>(climbs.size()); ++i) {
    double left = 0.0;
    for (const FightRow& fight : fights) {
      left += 1.0 - StandingOn(climbs[i], fight.key).best_done;
    }
    if (left > most_left) {
      most_left = left;
      weakest = i;
    }
  }
  return weakest;
}

// Every skill the character has a level in, the hypers marked, in the order
// the book lists them.
void PrintBook(const GameState& state) {
  // Off the character's own map rather than off the catalog. A skill is keyed
  // there by NAME, and several branches still write a name of their own -- two
  // Endures, three Final Attacks -- so walking the catalog would list one per
  // job and call them all learned.
  std::vector<std::pair<int, std::string>> learned;
  for (const std::pair<const std::string, int32_t>& held :
       state.character.proto().skill_levels()) {
    if (held.second <= 0) {
      continue;
    }
    int order = 0;
    bool hyper = false;
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      JobAdvancement book = state.character.BookHeldFor(entry.second);
      if (entry.second.name() == held.first &&
          book != JOB_ADVANCEMENT_UNSPECIFIED) {
        order = SkillOrderIn(entry.second, book);
        hyper = entry.second.hyper();
        break;
      }
    }
    learned.push_back(
        {order, absl::StrCat(held.first, " ", held.second, hyper ? "H" : "")});
  }
  std::sort(learned.begin(), learned.end());
  std::printf("\n  Book (%d skills, %d hyper SP unspent)\n",
              static_cast<int>(learned.size()), state.character.hyper_sp());
  for (int i = 0; i < static_cast<int>(learned.size()); ++i) {
    std::printf("%s%-28s", i % 3 == 0 ? "    " : "", learned[i].second.c_str());
    if (i % 3 == 2) {
      std::printf("\n");
    }
  }
  if (learned.size() % 3 != 0) {
    std::printf("\n");
  }
}

// What the bag is holding. The equip tab by piece, the Etc tab by stack --
// both of them are capacities the shopper can run into, so the counts matter
// as much as the contents.
void PrintBag(const GameState& state) {
  std::map<std::string, int> equips;
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    equips[bag.equip_instance(i) == nullptr
               ? absl::StrCat(bag[i].prototype().name(), " (trace)")
               : bag[i].prototype().name()]++;
  }
  std::printf("\n  Equip tab (%d of %d slots)\n", bag.size(), kTabCapacity);
  for (const std::pair<const std::string, int>& entry : equips) {
    std::printf("    %-40s x%d\n", entry.first.c_str(), entry.second);
  }
  const std::vector<StackableItem>& etc = state.character.stackables();
  std::printf("  Etc tab (%d of %d stacks)\n", static_cast<int>(etc.size()),
              kTabCapacity);
  for (const StackableItem& stack : etc) {
    std::printf("    %-40s x%d\n", stack.prototype().name().c_str(),
                stack.count());
  }
  const std::vector<CurrencyAmount>& purse =
      state.character.currencies().entries();
  std::printf("  Currencies (%d)\n", static_cast<int>(purse.size()));
  for (const CurrencyAmount& held : purse) {
    std::printf("    %-40s x%lld\n", held.prototype().name().c_str(),
                static_cast<long long>(held.count()));
  }
}

// One equipped piece, as a player reads it off the panel: what it is, the
// slots it has spent, its stars and any hammers driven into it.
void PrintWornRow(const EquipInstance& item) {
  const EquipPrototype& proto = item.prototype();
  // A symbol takes neither scroll nor star: what it has is a level and the
  // duplicates banked toward the next one, so the two columns say that
  // instead.
  if (IsArcaneSymbol(proto)) {
    const ms::Equip& worn = item.equip_state();
    int level = SymbolLevel(worn);
    char rung[16];
    std::snprintf(rung, sizeof(rung), "Lv%d", level);
    char banked[16];
    std::snprintf(banked, sizeof(banked), "%d/%d", worn.symbol_exp(),
                  SymbolExpToNextLevel(level));
    std::printf("    %-30s Lv%-4d %-8s %-6s %s\n", proto.name().c_str(),
                proto.required_level(), banked, rung, "");
    return;
  }
  int slots = proto.upgrade_slots() + item.equip_state().hammers();
  char stars[16];
  std::snprintf(stars, sizeof(stars), "%d*", item.stars());
  char scrolled[16];
  std::snprintf(scrolled, sizeof(scrolled), "%d/%d",
                slots - item.equip_state().remaining_upgrade_slots(), slots);
  std::printf("    %-30s Lv%-4d %-8s %-6s %s\n", proto.name().c_str(),
              proto.required_level(), scrolled, stars,
              item.equip_state().hammers() > 0 ? "hammered" : "");
}

// A Hyper Stat allocation, the stats that have anything on them. By field
// number, since a proto map hands them over in whatever order it likes and two
// runs of the sim are meant to diff.
void PrintHyperRow(const char* label, const HyperStatPreset& preset) {
  std::vector<std::pair<int, int32_t>> spread;
  for (const std::pair<const int, int32_t>& entry : preset.levels()) {
    if (entry.second > 0) {
      spread.push_back(entry);
    }
  }
  std::sort(spread.begin(), spread.end());
  std::printf("    %-9s", label);
  int spent = 0;
  for (const std::pair<int, int32_t>& entry : spread) {
    HyperStatField field = static_cast<HyperStatField>(entry.first);
    std::printf("  %s %d",
                absl::AsciiStrToLower(HyperStatField_Name(field).substr(
                                          std::strlen("HYPER_STAT_FIELD_")))
                    .c_str(),
                entry.second);
    spent += HyperStatTotalCost(entry.second);
  }
  std::printf("   (%d points)\n", spent);
}

// The whole character, printed for reading rather than for a table: what a
// player would see across the panels if they opened every one of them.
void PrintCharacterSheet(const Catalogs& catalogs, Job branch,
                         const Climb& climb) {
  GameState state = NewState(catalogs, 1);
  state.bosses = catalogs.bosses;
  state.character.RestoreFrom(climb.final_character, state.equips, state.items);
  const Character& proto = state.character.proto();
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);

  std::printf(
      "\nThe weakest branch's character, as the run left them -- most of a "
      "boss roster still\nstanding is usually one thing gone wrong rather "
      "than ten, and this is where to look.\n\n");
  char held[16];
  FormatShort(static_cast<double>(state.character.meso()), held, sizeof(held));
  std::printf("%s, Lv%d, %s played, %s meso held, %d CombatPower\n",
              BranchName(branch).c_str(), proto.level(),
              Clock(climb.milestones[kNumMilestones - 1].seconds).c_str(), held,
              PowerNow(state));
  // Both presets: the two differ in every lever a fight reads, and quoting
  // the farming one against a boss roster is how a branch looks armed for a
  // fight it cannot touch.
  DerivedStats bossing = DerivedStatsFor(state.character, state.skills, {},
                                         state.party, Activity::kBossing);
  std::printf("  HP %d   MP %d\n", derived.max_hp, derived.max_mp);
  const std::pair<const char*, const DerivedStats*> presets[] = {
      {"farming", &derived}, {"bossing", &bossing}};
  for (const std::pair<const char*, const DerivedStats*>& preset : presets) {
    std::printf(
        "    %-7s  crit %.0f%% at %.0f%%   boss %.0f%%   IED %.0f%%   "
        "damage %.0f%%\n",
        preset.first, 100.0 * preset.second->crit_rate,
        100.0 * preset.second->crit_dmg, 100.0 * preset.second->boss_pct,
        100.0 * preset.second->ied, 100.0 * preset.second->damage_pct);
  }

  std::printf("\n  Worn\n");
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped()) {
    PrintWornRow(*entry.second);
  }

  std::printf("\n  Hyper Stats (%d points paid, by preset)\n",
              state.character.hyper_stat_points());
  PrintHyperRow("farming", PresetOf(proto.hyper_stats(), StatPreset::kFirst));
  PrintHyperRow("bossing", PresetOf(proto.hyper_stats(), StatPreset::kSecond));

  std::printf("\n  Inner Ability (%s honor held)\n",
              std::to_string(state.character.honor()).c_str());
  PrintAbilityRow("farming", climb.ability_farming, climb.farming_worth);
  PrintAbilityRow("bossing", climb.ability_bossing, climb.bossing_worth);

  PrintBook(state);
  PrintBag(state);
}

// The branches to climb: the ones that take a 4th advancement, or the one
// --branch names. A branch that stops earlier was never built to reach the
// cap, so what it measures past its own advancement is a build nobody plays.
std::vector<Job> BranchesToClimb() {
  const std::string& name = absl::GetFlag(FLAGS_branch);
  if (!name.empty()) {
    return {ParseBranch(name)};
  }
  std::vector<Job> wanted;
  for (Job branch : EveryBranch()) {
    if (StageOf(branch) >= 4) {
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
    double capped = runs[i].milestones[kNumMilestones - 1].seconds;
    // Never reached is the far end of the sort, not the near one.
    by_time.push_back(
        {capped < 0.0 ? std::numeric_limits<double>::infinity() : capped, i});
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
            ? std::to_string(typical.milestones[kNumMilestones - 1].frozen) +
                  " of " + std::to_string(typical.frozen_set_size)
            : "Lv" + std::to_string(typical.frozen_set_level));

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
          "hammers, %d booms\n",
          "  spent on gear", spent, typical.endgame_scrolled,
          typical.endgame_pieces,
          typical.endgame_pieces == 0
              ? 0.0
              : static_cast<double>(typical.endgame_stars_worn) /
                    typical.endgame_pieces,
          typical.endgame_hammers, typical.booms);
      std::printf("  %-46s %s\n", "  farmed", typical.money_map.c_str());
    }
  }
}

// Where this sweep's climbs are written down. Worked out once, because
// preparing the directory throws away another build's and two threads racing
// to do that is two threads throwing away each other's.
Checkpointing PrepareCheckpoints() {
  Checkpointing saves;
  saves.level = absl::GetFlag(FLAGS_checkpoint_at);
  if (saves.level <= 0) {
    return saves;
  }
  saves.stamp = CheckpointStamp();
  saves.dir = PrepareCheckpointDir("progression_sim", saves.stamp);
  if (saves.dir.empty()) {
    LOG(WARNING) << "no checkpoint directory; climbing the whole way";
    saves.level = 0;
  }
  return saves;
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
  Checkpointing saves = PrepareCheckpoints();
  std::vector<std::vector<Climb>> runs(count, std::vector<Climb>(per_branch));
  ParallelFor(count * per_branch, [&](int i) {
    unsigned int seed =
        static_cast<unsigned int>(absl::GetFlag(FLAGS_seed)) + i / count;
    runs[i % count][i / count] =
        Play(catalogs, branches[i % count], maps, seed, saves);
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

  // Third jobs only. A branch stopping at its 2nd is scaffolding, not a
  // player, and the two answer differently: a 2nd job finds the top maps a
  // coin flip and stays put where a 3rd is paid a third more for moving up --
  // and the cape only falls off the mobs up there.
  PrintFrozenDrops(branches, typical);
  PrintTokenOdds(branches.data(), typical, count);
  PrintTargets(branches, runs);
  if (absl::GetFlag(FLAGS_boss_report)) {
    PrintBossTimeline(catalogs, branches, typical);
    PrintMesoLedger(branches, typical);
    PrintCubing(branches, typical);
    PrintDefence(catalogs, branches, typical);
    int weakest = WeakestBranch(catalogs, typical);
    PrintCharacterSheet(catalogs, branches[weakest], typical[weakest]);
  }
  PrintBossReadiness(catalogs, branches, typical);
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // The reset reads local time; see kSimEpoch.
  setenv("TZ", "UTC", /*overwrite=*/1);
  tzset();
  ms::Run();
  return 0;
}
