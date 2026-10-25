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
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "analysis/cube_plan.h"
#include "analysis/flame_plan.h"
#include "analysis/gear_plan.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_world.h"
#include "src/character/character_stats.h"
#include "src/character/guild.h"
#include "src/character/noblesse_plan.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/flame.h"
#include "src/item/potential.h"
#include "src/protos/account.pb.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/save.h"

ABSL_FLAG(std::string, save, "", "The save to read. Use a copy.");
ABSL_FLAG(int, trials, 3, "Attempts at each fight, each on its own seed.");
ABSL_FLAG(bool, flames, false,
          "List each boss-preset piece's flame score in DEX instead.");
ABSL_FLAG(bool, bpot, false,
          "List what each armour bonus potential line is worth instead.");
ABSL_FLAG(bool, gear, false, "Also list what the boss preset wears.");
ABSL_FLAG(bool, shelf, false,
          "List every upgrade the shopper would price instead, with its "
          "expected meso per Combat Power.");
ABSL_FLAG(bool, goals, false,
          "List the shopper's purchases on the save in order instead, each as "
          "a goal with its expected meso per Combat Power.");
ABSL_FLAG(int, goal_runs, 1000,
          "Follow-throughs of each cube or flame run --goals measures.");
ABSL_FLAG(int, goal_steps, 40, "Purchases --goals lists at most.");
ABSL_FLAG(int, goal_samples, 4096,
          "Rolls --goals solves each stopping rule from. The sims' 64 sets "
          "the goal near the best of a few and prices the run far too short.");
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

// Boss damage from each bonus line alone on an armour piece, against the same
// piece with no bonus lines, at Legendary and Unique. Every other piece keeps
// what it has, so a line is valued against the rest of the kit.
bool PrintBonusLines(const Catalogs& catalogs, const std::string& path) {
  const EquipSlot slots[] = {EQUIP_SLOT_HAT, EQUIP_SLOT_GLOVES, EQUIP_SLOT_TOP};
  for (EquipSlot slot : slots) {
    const PotentialGroup group = PotentialGroupOf(slot);
    auto power_with = [&](const Potential& potential, double& power) {
      GameState state = NewState(catalogs, 1);
      if (!Load(catalogs, path, state) ||
          !state.character.TakePotential(slot, PotentialTrack::kBonus,
                                         potential, StatPreset::kSecond)) {
        return false;
      }
      GearShopper shopper{GearPlan()};
      power = shopper.Power(state);
      return true;
    };
    Potential empty;
    empty.set_rank(POTENTIAL_RANK_LEGENDARY);
    double base = 0.0;
    if (!power_with(empty, base)) {
      return false;
    }
    std::printf("%s (and every piece in its pool), one line:\n",
                FormatSlot(slot).c_str());
    for (PotentialLineType type : PotentialPool(PotentialTrack::kBonus, group,
                                                POTENTIAL_RANK_LEGENDARY)) {
      std::printf("  %-28s", PotentialLineName(type).c_str());
      for (PotentialRank rank :
           {POTENTIAL_RANK_LEGENDARY, POTENTIAL_RANK_UNIQUE}) {
        Potential one = empty;
        PotentialLine* line = one.add_lines();
        line->set_type(type);
        line->set_rank(rank);
        double power = 0.0;
        if (!power_with(one, power)) {
          return false;
        }
        std::printf(
            "  %s %3d: %+6.2f%%", rank == POTENTIAL_RANK_LEGENDARY ? "L" : "U",
            PotentialLineValue(type, rank, 160), 100.0 * (power / base - 1.0));
      }
      std::printf("\n");
    }
  }
  return true;
}

// Each boss-preset piece's flame as the flat DEX worth the same boss damage
// on this kit: the gain over no flame, over a 100 DEX line's gain on the same
// piece.
bool PrintFlameScores(const Catalogs& catalogs, const std::string& path) {
  GameState held = NewState(catalogs, 1);
  if (!Load(catalogs, path, held)) {
    return false;
  }
  auto power_with = [&](EquipSlot slot, const FlameLines& lines,
                        double& power) {
    GameState state = NewState(catalogs, 1);
    if (!Load(catalogs, path, state) ||
        !state.character.TakeFlame(slot, lines, StatPreset::kSecond)) {
      return false;
    }
    GearShopper shopper{GearPlan()};
    power = shopper.Power(state);
    return true;
  };
  std::printf("%-12s %-28s %6s  %s\n", "Slot", "Item", "Score", "Lines");
  for (int i = EquipSlot_MIN; i <= EquipSlot_MAX; ++i) {
    if (!EquipSlot_IsValid(i)) {
      continue;
    }
    const EquipSlot slot = static_cast<EquipSlot>(i);
    const EquipInstance* item =
        held.character.WornAt(StatPreset::kSecond, slot);
    if (item == nullptr || item->equip_state().flame().empty()) {
      continue;
    }
    const FlameLines none;
    FlameLines dex;
    FlameLine* line = dex.Add();
    line->set_stat(FLAME_STAT_DEX);
    line->set_tier(7);
    const int dex_value = FlameLineValue(*line, item->prototype());
    double bare = 0.0;
    double with_dex = 0.0;
    double held_power = 0.0;
    if (!power_with(slot, none, bare) || !power_with(slot, dex, with_dex) ||
        !power_with(slot, item->equip_state().flame(), held_power)) {
      return false;
    }
    const double per_dex = (with_dex - bare) / dex_value;
    std::string lines;
    for (const FlameLine& held_line : item->equip_state().flame()) {
      lines += FlameStatName(held_line.stat()) + " " +
               FlameLineValueText(held_line, item->prototype()) + " T" +
               std::to_string(held_line.tier()) + ", ";
    }
    std::printf("%-12s %-28s %6.0f  %s\n", FormatSlot(slot).c_str(),
                item->name().c_str(), (held_power - bare) / per_dex,
                lines.substr(0, lines.size() - 2).c_str());
  }
  return true;
}

// What one offer is, in a few words.
std::string OfferName(const GameState& state,
                      const GearShopper::Candidate& offer) {
  const EquipInstance* item = state.character.WornAt(offer.gear, offer.slot);
  if (!offer.symbol_run.empty()) {
    return absl::StrCat("Arcane Force: ", offer.symbol_run.size(),
                        " symbol levels");
  }
  std::string what = item == nullptr ? FormatSlot(offer.slot) : item->name();
  if (offer.gear == kFarmGear) {
    what += " (farm)";
  }
  if (offer.symbol) {
    return absl::StrCat(what, ": +1 level");
  }
  if (offer.star) {
    return absl::StrCat(what, ": stars ", item->stars(), " to ", offer.star_to);
  }
  if (offer.scroll != nullptr) {
    return absl::StrCat(what, ": ", offer.scroll->name());
  }
  if (offer.cube) {
    return absl::StrCat(what, ": ", CubeName(offer.cube_type), " x",
                        absl::StrFormat("%.1f", offer.cube_program.cubes));
  }
  if (offer.flame) {
    return absl::StrCat(what, ": ", FlameName(offer.flame_type), " x",
                        absl::StrFormat("%.1f", offer.flame_program.flames));
  }
  if (offer.split) {
    return absl::StrCat(what, ": farm copy, cubed");
  }
  return what;
}

// Every offer on the shopper's shelf, best first, against the boss preset.
// CP-eq is the boss damage gain at the save's CP per unit of damage, so rolls
// and Arcane Force, which no stat line shows, rank on one scale; CP is the stat
// line's true change, for a scroll, stars or symbols.
bool PrintShelf(const Catalogs& catalogs, const std::string& path) {
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, path, state)) {
    return false;
  }
  GearShopper shopper{GearPlan()};
  const double power = shopper.Power(state);
  const std::vector<GearShopper::Candidate> offers = shopper.Shelf(state);
  const CharacterInstance& character = state.character;
  const DerivedStats derived =
      DerivedStatsFor(character, state.skills, {}, {}, Activity::kBossing);
  const EquipStats worn = TotalEquipStats(character, derived);
  const Character& p = character.proto();
  auto cp_with = [&](const EquipStats& added) {
    const EquipStats sources[] = {worn, added};
    return CombatPower(
        OffenseStatsFor(p.job(), p.level(), p.allocated_stats(),
                        SumEquipStats(sources),
                        character.weapon_type(derived.gear),
                        /*attack_skill=*/nullptr, /*attack_level=*/0,
                        PassiveOffenseFor(derived)),
        /*vs_boss=*/true);
  };
  const int cp = cp_with(EquipStats());
  std::printf(
      "%s, level %d: %s meso, boss Combat Power %s%s.\n\n", p.name().c_str(),
      p.level(), FormatWithCommas(character.meso()).c_str(),
      FormatWithCommas(cp).c_str(),
      state.account.gms_combat_power() ? " (ours; the save shows GMS's)" : "");
  std::printf("%-52s %15s %7s %8s %13s %7s %13s\n", "Offer", "Exp. meso", "Dmg",
              "CP-eq", "Meso/CP-eq", "CP", "Meso/CP");
  for (const GearShopper::Candidate& offer : offers) {
    if (offer.gain <= 0.0 || offer.cost <= 0) {
      continue;
    }
    const double cp_eq = cp * offer.gain / power;
    std::string true_cp = "-";
    std::string per_cp = "-";
    if (offer.stats.has_value()) {
      const int gained = cp_with(*offer.stats) - cp;
      true_cp = absl::StrCat(gained);
      if (gained > 0) {
        per_cp = FormatWithCommas(offer.cost / gained);
      }
    }
    const char* afford =
        (offer.outlay > 0 ? offer.outlay : offer.cost) <= character.meso()
            ? " "
            : "*";
    std::printf(
        "%-52s %14s%s %6.2f%% %8.0f %13s %7s %13s\n",
        OfferName(state, offer).substr(0, 52).c_str(),
        FormatWithCommas(offer.cost).c_str(), afford,
        100.0 * offer.gain / power, cp_eq,
        FormatWithCommas(static_cast<int64_t>(offer.cost / cp_eq)).c_str(),
        true_cp.c_str(), per_cp.c_str());
  }
  std::printf("\n* more than the purse holds to start.\n");
  return true;
}

// What `runs` follow-throughs of one cube or flame run cost, and what they
// stopped on.
struct GoalRuns {
  std::vector<int64_t> costs;  // sorted
  // Stopping lines, as text, by how many runs ended on them.
  std::map<std::string, int> outcomes;
  // The least boss damage over the start any run stopped on, as a share, and
  // for a flame the least flame score.
  double least_gain = 0.0;
  double least_score = 0.0;
  double start_score = 0.0;
  int capped = 0;         // runs cut off at kGoalRollCap
  double gain_sum = 0.0;  // boss damage over the start, summed over runs

  double MeanGain() const {
    return costs.empty() ? 0.0 : gain_sum / costs.size();
  }

  int64_t Mean() const {
    double sum = 0.0;
    for (int64_t cost : costs) {
      sum += static_cast<double>(cost);
    }
    return costs.empty() ? 0 : static_cast<int64_t>(sum / costs.size());
  }
  int64_t P90() const {
    return costs.empty() ? 0 : costs[costs.size() * 9 / 10];
  }
};

// Rolls one run gives up after, so a rule that almost never stops can't hang
// the report. Reported when reached.
constexpr int kGoalRollCap = 20000;

// What a potential grants this character, as the Equipped list writes it: the
// same effect summed over lines, best first, so stops that differ only in
// lines worth nothing read as one.
std::string LinesText(const Potential& potential, PotentialRank start,
                      int item_level, Job job) {
  std::string text = absl::StrJoin(
      PotentialCellEffects(potential, item_level, PrimaryStatField(job),
                           SecondaryStatField(job)),
      ", ");
  if (potential.rank() != start) {
    text = PotentialRankName(potential.rank()) + ": " + text;
  }
  return text;
}

// Follows `offer`'s cube run through `runs` times from the piece as it is,
// putting the piece and the purse back after each.
GoalRuns SimulateCubes(GameState& state, GearShopper& shopper,
                       const GearShopper::Candidate& offer, int runs) {
  GoalRuns out;
  const Cube& shelf = CubeOf(offer.cube_type);
  const EquipInstance* item = state.character.WornAt(offer.gear, offer.slot);
  const int level = item->prototype().required_level();
  const Potential start = shelf.track == PotentialTrack::kMain
                              ? item->equip_state().main_potential()
                              : item->equip_state().bonus_potential();
  const int64_t purse = state.character.meso();
  const double power = shopper.Power(state);
  out.least_gain = std::numeric_limits<double>::infinity();
  for (int r = 0; r < runs; ++r) {
    const CubeRun run(state, shopper.yardstick().For(state), offer.gear,
                      offer.slot, offer.cube_type, shopper.income(),
                      offer.cube_program);
    int64_t cost = 0;
    int rolls = 0;
    do {
      state.character.AddMeso(shelf.cost);
      std::optional<Potential> rolled =
          state.character.BuyCube(offer.slot, offer.cube_type, offer.gear);
      if (!rolled.has_value()) {
        break;
      }
      cost += shelf.cost;
      if (!shelf.choose || run.Takes(state, *rolled)) {
        state.character.TakePotential(offer.slot, shelf.track, *rolled,
                                      offer.gear);
      }
    } while (++rolls < kGoalRollCap && run.Continues(state));
    out.capped += rolls >= kGoalRollCap;
    out.costs.push_back(cost);
    const EquipInstance* now = state.character.WornAt(offer.gear, offer.slot);
    const Potential& held = shelf.track == PotentialTrack::kMain
                                ? now->equip_state().main_potential()
                                : now->equip_state().bonus_potential();
    const double gain = shopper.Power(state) / power - 1.0;
    out.gain_sum += gain;
    // A run cut off stopped on whatever it last rolled, not on the goal.
    if (rolls < kGoalRollCap) {
      ++out.outcomes[LinesText(held, start.rank(), level,
                               state.character.proto().job())];
      out.least_gain = std::min(out.least_gain, gain);
    }
    state.character.TakePotential(offer.slot, shelf.track, start, offer.gear);
  }
  state.character.SpendMeso(state.character.meso() - purse);
  std::sort(out.costs.begin(), out.costs.end());
  return out;
}

// The same for a flame run, with each stop scored as the flat DEX worth the
// same boss damage (see PrintFlameScores).
GoalRuns SimulateFlames(GameState& state, GearShopper& shopper,
                        const GearShopper::Candidate& offer, int runs) {
  GoalRuns out;
  const Flame& shelf = FlameOf(offer.flame_type);
  const FlameLines start =
      state.character.WornAt(kBossGear, offer.slot)->equip_state().flame();
  const int64_t purse = state.character.meso();
  const double power = shopper.Power(state);
  // The DEX a flame's damage is scored in, measured on this piece.
  state.character.TakeFlame(offer.slot, FlameLines(), kBossGear);
  const double bare = shopper.Power(state);
  FlameLines dex;
  FlameLine* line = dex.Add();
  line->set_stat(FLAME_STAT_DEX);
  line->set_tier(7);
  const int dex_value = FlameLineValue(
      *line, state.character.WornAt(kBossGear, offer.slot)->prototype());
  state.character.TakeFlame(offer.slot, dex, kBossGear);
  const double per_dex = (shopper.Power(state) - bare) / dex_value;
  state.character.TakeFlame(offer.slot, start, kBossGear);
  out.start_score = (power - bare) / per_dex;
  out.least_gain = std::numeric_limits<double>::infinity();
  out.least_score = std::numeric_limits<double>::infinity();
  for (int r = 0; r < runs; ++r) {
    const FlameRun run(state, shopper.yardstick().For(state), offer.slot,
                       offer.flame_program);
    int64_t cost = 0;
    int rolls = 0;
    do {
      state.character.AddMeso(shelf.cost);
      std::optional<FlameLines> rolled = state.character.BuyFlame(
          offer.slot, offer.flame_type,
          state.character.WornAt(kBossGear, offer.slot)->equip_state().flame(),
          kBossGear);
      if (!rolled.has_value()) {
        break;
      }
      cost += shelf.cost;
      if (!shelf.choose || run.Takes(state, *rolled)) {
        state.character.TakeFlame(offer.slot, *rolled, kBossGear);
      }
    } while (++rolls < kGoalRollCap && run.Continues(state));
    out.capped += rolls >= kGoalRollCap;
    out.costs.push_back(cost);
    const double held = shopper.Power(state);
    out.gain_sum += held / power - 1.0;
    if (rolls < kGoalRollCap) {
      out.least_gain = std::min(out.least_gain, held / power - 1.0);
      out.least_score = std::min(out.least_score, (held - bare) / per_dex);
    }
    state.character.TakeFlame(offer.slot, start, kBossGear);
  }
  state.character.SpendMeso(state.character.meso() - purse);
  std::sort(out.costs.begin(), out.costs.end());
  return out;
}

// One purchase of the plan, as printed.
struct GoalRow {
  std::string goal;
  std::vector<std::string> detail;
  int64_t mean = 0;
  std::string p90;
  double cp = 0.0;
  std::string piece;  // what it is bought for, which orders steps on it
  double rank = 0.0;  // meso per CP, never ahead of an earlier step's piece
};

// The shopper's purchases on the save, each as a goal: what to buy and where
// to stop. The shopper picks the best boss damage per meso at each step,
// re-ranked after every purchase; a cube or flame run's cost is measured over
// `runs` follow-throughs of its stopping rule, and the plan goes on from one
// of them. Printed by that measured meso per CP, with a later step on a piece
// never ahead of an earlier one.
bool PrintGoals(const Catalogs& catalogs, const std::string& path, int runs,
                int steps, int samples) {
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, path, state)) {
    return false;
  }
  GearPlan plan;
  plan.samples = samples;
  GearShopper shopper{plan};
  const int start_cp =
      CharacterCombatPower(state.character, state.skills, Activity::kBossing);
  const int64_t purse = state.character.meso();
  std::printf("%s, level %d: %s meso, boss Combat Power %s.\n",
              state.character.proto().name().c_str(),
              state.character.proto().level(), FormatWithCommas(purse).c_str(),
              FormatWithCommas(start_cp).c_str());
  std::printf(
      "Cube and flame costs over %d runs of each stopping rule. Meso and "
      "drop lines are valued at nothing.\n\n",
      runs);
  std::vector<GoalRow> rows;
  for (int step = 1; step <= steps; ++step) {
    const double power = shopper.Power(state);
    const int cp =
        CharacterCombatPower(state.character, state.skills, Activity::kBossing);
    bool bought = false;
    for (const GearShopper::Candidate& offer : shopper.Shelf(state)) {
      if (offer.gain <= 0.0 || offer.cost <= 0 ||
          (offer.outlay > 0 ? offer.outlay : offer.cost) >
              state.character.meso()) {
        continue;
      }
      const EquipInstance* item =
          state.character.WornAt(offer.gear, offer.slot);
      const std::string piece =
          item == nullptr ? FormatSlot(offer.slot) : item->name();
      std::string goal = OfferName(state, offer);
      std::vector<std::string> detail;
      int64_t mean = offer.cost;
      double gain = offer.gain / power;
      std::string p90 = "-";
      if (offer.cube || offer.flame) {
        const GoalRuns sim = offer.cube
                                 ? SimulateCubes(state, shopper, offer, runs)
                                 : SimulateFlames(state, shopper, offer, runs);
        mean = sim.Mean();
        gain = sim.MeanGain();
        p90 = FormatWithCommas(sim.P90());
        if (offer.flame) {
          goal = absl::StrFormat("%s: %ss until score %.0f+ (now %.0f)", piece,
                                 FlameName(offer.flame_type), sim.least_score,
                                 sim.start_score);
        } else {
          goal = absl::StrFormat("%s: %ss until +%.2f%% boss damage", piece,
                                 CubeName(offer.cube_type),
                                 100.0 * sim.least_gain);
          std::vector<std::pair<int, std::string>> common;
          for (const auto& [lines, count] : sim.outcomes) {
            common.push_back({count, lines});
          }
          std::sort(common.rbegin(), common.rend());
          for (size_t i = 0; i < common.size() && i < 3; ++i) {
            detail.push_back(absl::StrFormat("%3.0f%% of runs stop on %s",
                                             100.0 * common[i].first / runs,
                                             common[i].second));
          }
        }
        if (sim.capped > 0) {
          detail.push_back(absl::StrFormat("%d runs gave up at %d rolls",
                                           sim.capped, kGoalRollCap));
        }
      }
      GearSpend spend;
      if (!shopper.Buy(state, offer, spend)) {
        continue;  // refused by the bag; the next offer is tried
      }
      bought = true;
      GoalRow row{goal, detail, mean, p90, cp * gain, piece};
      row.rank = row.cp > 0.0 ? mean / row.cp
                              : std::numeric_limits<double>::infinity();
      for (const GoalRow& earlier : rows) {
        if (earlier.piece == piece) {
          row.rank = std::max(row.rank, earlier.rank);
        }
      }
      rows.push_back(std::move(row));
      break;
    }
    if (!bought) {
      break;
    }
  }
  std::stable_sort(
      rows.begin(), rows.end(),
      [](const GoalRow& a, const GoalRow& b) { return a.rank < b.rank; });
  std::printf("%3s  %-71s %14s %14s %10s %9s %14s\n", "#", "Goal", "Exp. meso",
              "90% within", "CP", "Meso/CP", "Total exp.");
  int64_t total = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    const GoalRow& row = rows[i];
    if (total <= purse && total + row.mean > purse) {
      std::printf("     --- the purse's %s runs out here, on average ---\n",
                  FormatWithCommas(purse).c_str());
    }
    total += row.mean;
    std::printf(
        "%3zu  %-71s %14s %14s %10s %9s %14s\n", i + 1,
        row.goal.substr(0, 71).c_str(), FormatWithCommas(row.mean).c_str(),
        row.p90.c_str(), FormatWithCommas(static_cast<int64_t>(row.cp)).c_str(),
        FormatWithCommas(static_cast<int64_t>(row.mean / row.cp)).c_str(),
        FormatWithCommas(total).c_str());
    for (const std::string& line : row.detail) {
      std::printf("     %s\n", line.c_str());
    }
  }
  std::printf(
      "\nBoss Combat Power after one play-through of the plan with the "
      "purse: %s.\n",
      FormatWithCommas(CharacterCombatPower(state.character, state.skills,
                                            Activity::kBossing))
          .c_str());
  return true;
}

int Run() {
  std::string path = absl::GetFlag(FLAGS_save);
  const int trials = absl::GetFlag(FLAGS_trials);
  const Catalogs catalogs = LoadCatalogs();
  if (absl::GetFlag(FLAGS_flames)) {
    return PrintFlameScores(catalogs, path) ? 0 : 1;
  }
  if (absl::GetFlag(FLAGS_goals)) {
    return PrintGoals(catalogs, path, absl::GetFlag(FLAGS_goal_runs),
                      absl::GetFlag(FLAGS_goal_steps),
                      absl::GetFlag(FLAGS_goal_samples))
               ? 0
               : 1;
  }
  if (absl::GetFlag(FLAGS_shelf)) {
    return PrintShelf(catalogs, path) ? 0 : 1;
  }
  if (absl::GetFlag(FLAGS_bpot)) {
    return PrintBonusLines(catalogs, path) ? 0 : 1;
  }
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
