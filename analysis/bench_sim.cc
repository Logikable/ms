/* bench_sim: the controlled bench. Every branch is held at the same numbers, so
 * a gap between two can be attributed to a cause.
 *
 * progression_sim can't answer this, by design: every character it produces got
 * there differently. Here the gear is written rather than earned, the charm
 * flags (--bonus_stat, --bonus_attack, --bonus_boss_pct, --bonus_ied) give
 * every branch the same amount of one lever, and --boss_pdr sets a property of
 * the target. Sweep one and see how the field responds. That is how ignored
 * defence was shown to be the whole of the Lv230 class spread: the ten branches
 * spread 1.50x at 0% defence and 2.76x at 300%.
 *
 * So don't read anything here as a forecast. The DPS column is what a branch
 * would do with gear this file wrote for it, which no one actually reached. For
 * example, the max character gets one ignored-defence line by fiat, while the
 * played character ends up with whatever the shopper bought. What a branch
 * actually achieves is progression_sim's question.
 *
 * By default DPS is measured against a lone mob of the character's own level on
 * an otherwise empty map, so only the character and weapon are compared: no
 * crowd for a wide skill to exploit, and no spawn cap to hide a difference.
 * --enemies and --boss ask the other two questions, and the header says which
 * was asked.
 *
 *   bazelisk run //analysis:bench_sim -- --level=140 --enemies=8
 *   bazelisk run //analysis:bench_sim -- --level=230 --max --boss \
 *       --boss_pdr=300
 *   bazelisk run //analysis:bench_sim -- --level=230 --max --boss \
 *       --bonus_ied=60
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
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
#include "analysis/hyper_plan.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_gear.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "analysis/skill_plan.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/hyper_stats.h"
#include "src/character/progression.h"
#include "src/character/stat_preset.h"
#include "src/character/v_matrix.h"
#include "src/combat/damage.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/projectile.h"
#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

ABSL_FLAG(int, level, 60, "Character level, and the level of the mob fought.");
ABSL_FLAG(bool, detail, false,
          "Also print the stat line, the skill levels and the share of the "
          "damage each attack took.");
ABSL_FLAG(int, enemies, 1,
          "How many mobs stand together. 1 compares weapons, which is what "
          "the table is for; a larger crowd asks a different question -- what "
          "a wide skill is worth once there is something to be wide against.");
ABSL_FLAG(bool, boss, false,
          "Fight a boss instead of an ordinary monster: boss damage counts, "
          "a swing's bonus against normal monsters does not, and elemental "
          "damage is halved. Nothing in the shipped catalog is one yet.");
ABSL_FLAG(int, bonus_stat, 0,
          "Hands every build this much of its own primary stat, and nothing "
          "else, so two jobs can be compared holding the same numbers. Rides "
          "a fabricated charm rather than AP, so no skill lifts it.");
ABSL_FLAG(int, bonus_attack, 0,
          "The same for attack, physical and magic alike -- a build reads "
          "whichever of the two its weapon uses.");
ABSL_FLAG(int, bonus_boss_pct, 0,
          "The same for boss damage, as a percent. Only counts with --boss.");
ABSL_FLAG(int, bonus_ied, 0,
          "The same for ignored defence, as a percent. It combines with what "
          "the build already holds the way every other source does, so it "
          "asks what a shared pool -- the one GMS hands out on potential "
          "lines nobody's class decides -- does to the field.");
ABSL_FLAG(bool, upgraded, false,
          "Wear everything at its ceiling: every upgrade slot filled with the "
          "spell trace that swings hardest on it, and stars up to the item's "
          "own maximum. The default is gear straight off the shelf.");
ABSL_FLAG(bool, max, false,
          "Measure the ceiling character at the level instead of one grown "
          "down its path: --mode=max's gear, hyper stats, ability, buffs and "
          "V matrix. Below the 5th job it changes only the gear; at 200 it is "
          "the difference between a V node at 1 and one at 30, which is most "
          "of what a 5th job is. One row a branch, holding whatever the "
          "ceiling armed it with.");
ABSL_FLAG(double, seconds, 600.0,
          "How long the swings are played out for, in the game's own seconds "
          "-- the clock a cooldown is written in. The default is ten minutes, "
          "long enough that a two-minute cooldown lands five times and the "
          "figure is a sustained one. A window shorter than the longest "
          "cooldown is a burst instead, with everything up for the whole of "
          "it: 30 seconds and 240 are two different questions, and at level "
          "200 the four branches holding V job nodes swap order between "
          "them.");
ABSL_FLAG(int, boss_pdr, 0,
          "Percent of the mob's physical defence, which every Ignore DEF "
          "lever in the game is measured against. 0 is the shipped catalog, "
          "where all of them cancel nothing. Ignored once --fight names a real "
          "boss, which carries its own.");
ABSL_FLAG(bool, endowed, false,
          "Hand every branch the same purse, honor and V Points and let them "
          "spend it: the shopper buys the gear, the Hyper Stats, the Inner "
          "Ability and the V Matrix, each ranked against the fight the row is "
          "measured on. The third way to seed a row, beside a grown path and "
          "--max's fiat ceiling -- and the one that asks what a branch DOES "
          "with a budget rather than what it does holding one it was given. "
          "Everything that drops below AbsoLab is worn; AbsoLab itself is "
          "not, since Lotus and Damien are what pay for it.");
ABSL_FLAG(int64_t, meso, 30'000'000'000,
          "The purse --endowed hands every branch.");
ABSL_FLAG(int64_t, honor, 1'000'000,
          "The honor --endowed hands every branch, for the Inner Ability.");
ABSL_FLAG(int64_t, v_points, 1'500,
          "The V Points --endowed hands every branch. A whole explorer matrix "
          "is 4,505, so this is a choice rather than a buy-out -- which is "
          "the point of handing it over.");
ABSL_FLAG(std::string, ability_rank, "legendary",
          "The Inner Ability rank --endowed rolls towards.");
ABSL_FLAG(std::string, fight, "",
          "Measure against a boss out of the catalog -- 'lotus', 'damien' -- "
          "rather than the invented dummy, and report whether the fight was "
          "actually won. Implies --boss.");
ABSL_FLAG(std::string, difficulty, "Normal",
          "Which of --fight's difficulties to meet.");
ABSL_FLAG(double, plan_seconds, 180.0,
          "How long one spending decision is played out for. Longer than the "
          "book's own ranking wants: a V node on a two-minute cooldown reads "
          "as free damage inside any window it never comes back in. Widened "
          "on its own to twice the slowest cooldown the character holds.");
ABSL_FLAG(std::string, branch, "",
          "Measure this branch alone, as --job spells it (\"bishop\"). Every "
          "branch by default.");
ABSL_FLAG(int, rounds, 2,
          "How many times --endowed goes round the spending. One pass cannot "
          "settle it: a V node changes what a Hyper Stat point is worth and a "
          "weapon changes what every cube is.");

namespace ms {
namespace {

// Fixes the random stream for every run of this sim. Rewards are rolled, so an
// unseeded run would print a slightly different table each time and hide real
// changes in the noise.
constexpr unsigned int kSimSeed = 20260813;

// Map and mob for the comparison, made up rather than taken from the catalog. A
// real map has mobs only at its own levels, and its crowd would let a wide
// skill skew a comparison meant for one weapon against one mob.
constexpr char kDummyMap[] = "__dps_dummy";
constexpr char kDummyMob[] = "__dps_dummy_mob";
constexpr char kDummyBoss[] = "__dps_dummy_boss";

// One row of the table: a branch and its weapon. The weapon's ammunition is
// bought with it, since an empty projectile slot looks like a broken build;
// most of an Assassin's damage is in the ammunition.
struct Build {
  Job job = JOB_UNSPECIFIED;
  EquipType weapon = EQUIP_TYPE_UNSPECIFIED;
};

// Name of the row's primary stat, so the detail line labels the stat it
// actually holds rather than assuming a warrior's.
const char* PrimaryStatName(Job job) {
  switch (PrimaryStatField(job)) {
    case STAT_FIELD_DEX:
      return "DEX";
    case STAT_FIELD_INT:
      return "INT";
    case STAT_FIELD_LUK:
      return "LUK";
    default:
      return "STR";
  }
}

// The charm the two bonus flags use: a hat, since nothing in the catalog fills
// that slot, carrying only the stat and the attack. A player can't get it; it
// exists to hold two jobs at the same numbers and see what difference remains.
constexpr char kCharm[] = "__sim_charm";

EquipPrototype Charm(Job job, int stat, int attack, int boss_pct, int ied_pct) {
  EquipPrototype proto;
  proto.set_name("Sim Charm");
  proto.set_equip_slot(EQUIP_SLOT_HAT);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  proto.add_unsupported_upgrades(UPGRADE_SCROLL);
  proto.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  EquipStats* stats = proto.mutable_base_stats();
  stats->set_attack(attack);
  stats->set_magic_attack(attack);
  stats->set_boss_damage(boss_pct);
  stats->set_ignore_enemy_defense(ied_pct);
  switch (PrimaryStatField(job)) {
    case STAT_FIELD_DEX:
      stats->set_dex(stat);
      break;
    case STAT_FIELD_INT:
      stats->set_int_(stat);
      break;
    case STAT_FIELD_LUK:
      stats->set_luk(stat);
      break;
    default:
      stats->set_str(stat);
      break;
  }
  return proto;
}

// The catalogs plus this sim's dummy, at the character's own level so the level
// multiplier matches fighting their own tier. No PDR or boss flag unless the
// flags ask. Its HP is the measurement's, since the dummy must look unkillable
// to a held attack.
Catalogs LoadCatalogsWithDummy(int level) {
  Catalogs c = LoadCatalogs();
  Mob dummy;
  dummy.set_name("Dummy");
  dummy.set_level(level);
  dummy.set_max_hp(kMeasuredMobHp);
  dummy.set_boss(absl::GetFlag(FLAGS_boss));
  dummy.set_pdr(absl::GetFlag(FLAGS_boss_pdr));
  c.mobs[kDummyMob] = dummy;

  MapData map;
  map.set_name("Dummy");
  Spawn* spawn = map.add_spawns();
  spawn->set_mob(kDummyMob);
  spawn->set_count(1);
  c.maps[kDummyMap] = map;
  return c;
}

// Whether the row is measured against a boss: the made-up one --boss creates,
// or the catalog boss --fight names.
bool Bossing() {
  return absl::GetFlag(FLAGS_boss) || !absl::GetFlag(FLAGS_fight).empty();
}

// The fight --boss asks about: one phase with only the dummy.
//
// It's passed to ComputeBossParams rather than built here, which is the point.
// A boss fight runs at 1x where a map is stretched, halves reach, targets the
// healthiest part first, and uses the bossing preset. Approximating it with a
// boss-flagged mob on a farming character measured a different character than
// progression_sim did, by 49% on a Bishop.
BossDifficulty DummyFight() {
  BossDifficulty difficulty;
  difficulty.set_name("Dummy");
  Spawn* spawn = difficulty.add_phases()->add_spawns();
  spawn->set_mob(kDummyMob);
  spawn->set_count(1);
  return difficulty;
}

// What every row is measured against and every planning decision is ranked on.
// The made-up dummy and a catalog boss are the same thing here (a difficulty
// and a phase), so nothing downstream has to know which it got.
struct Fight {
  std::string boss;  // catalog key, or the dummy's name
  int difficulty = 0;
  BossDifficulty data;
  // Whether the fight can be won. The dummy never dies, so only a catalog boss
  // is played to the end.
  bool real = false;
};

Fight ChosenFight(const Catalogs& catalogs) {
  Fight fight;
  const std::string& named = absl::GetFlag(FLAGS_fight);
  if (named.empty()) {
    fight.boss = kDummyBoss;
    fight.data = DummyFight();
    return fight;
  }
  std::map<std::string, Boss>::const_iterator it = catalogs.bosses.find(named);
  if (it == catalogs.bosses.end()) {
    LOG(FATAL) << "Unknown --fight '" << named << "'";
  }
  int index = BossDifficultyIndex(it->second, absl::GetFlag(FLAGS_difficulty));
  if (index < 0) {
    LOG(FATAL) << "Boss '" << named << "' has no difficulty '"
               << absl::GetFlag(FLAGS_difficulty) << "'";
  }
  fight.boss = named;
  fight.difficulty = index;
  fight.data = it->second.difficulties(index);
  fight.real = true;
  return fight;
}

// The fight's combat params. One builder serves both the measurement and every
// spending decision, so points are ranked against the same fight the row
// reports.
CombatParams ParamsFor(GameState& state, const Fight& fight) {
  if (!Bossing()) {
    return ComputeCombatParams(state);
  }
  return ComputeBossParams(state, fight.boss, fight.data,
                           BossObjectivePhase(state.mobs, fight.data));
}

// How long one spending decision is played out. Twice the slowest cooldown the
// character has, if that's longer than the flag. A node on a two-minute
// cooldown looks like free damage in any window too short for it to come back,
// and a shopper ranking on that window buys the wrong thing.
double PlanWindow(const GameState& state) {
  double cycle = 0.0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (state.character.HoldsSkillFrom(entry.second)) {
      cycle = std::max(cycle, entry.second.cooldown_seconds());
    }
  }
  return std::max(absl::GetFlag(FLAGS_plan_seconds), 2.0 * cycle);
}

// Damage per second the character deals to the fight, at 1x. Every --endowed
// decision (gear, Hyper Stats, Inner Ability and matrix) is ranked on this one
// rate, so all the resources go toward one answer.
double PlanRate(GameState& state, const Fight& fight) {
  CombatParams params = ParamsFor(state, fight);
  if (!params.active) {
    return 0.0;
  }
  // A boss fight runs at 1x; a map is stretched, and MeasureFight counts in the
  // stretched clock either way. See GameSpeedFactor.
  double speed =
      Bossing() ? 1.0 : GameSpeedFactor(state.character.proto().level());
  Sequence played = MeasureFight(params, PlanWindow(state) * speed,
                                 absl::GetFlag(FLAGS_enemies));
  return played.seconds > 0.0 ? played.damage * speed / played.seconds : 0.0;
}

// The token AbsoLab's shelf is priced in, and the one thing the endowed
// character isn't given: Lotus and Damien drop it, so a character just entering
// those fights has none.
constexpr char kAbsoLabToken[] = "absolab_coin";

// The best weapon of `type` a character at `level` can wear. Chosen by type
// rather than catalog key so the table below needn't change when a tier is
// added; that's why a build says "claw", not "dark_gigantic". `below_absolab`
// stops at the tier below the one Lotus and Damien pay for.
std::string BestOfType(const Catalogs& catalogs, EquipType type, int level,
                       bool below_absolab) {
  std::string best;
  int best_level = -1;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       catalogs.equips) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() != type || proto.required_level() > level ||
        proto.required_level() <= best_level ||
        (below_absolab && proto.token_item() == kAbsoLabToken)) {
      continue;
    }
    best_level = proto.required_level();
    best = entry.first;
  }
  return best;
}

// Puts `key` on the character in the slot its prototype names, replacing what
// was there. Returns false if the catalog has no such item.
bool Wear(GameState& state, const std::string& key) {
  std::map<std::string, EquipPrototype>::const_iterator it =
      state.equips.find(key);
  if (it == state.equips.end()) {
    return false;
  }
  state.character.Unequip(it->second.equip_slot());
  state.character.PickUp(std::make_unique<EquipInstance>(it->second));
  // The item just picked up is the last in the bag.
  return state.character.Equip(state.character.inventory().size() - 1);
}

// Result of one build.
struct Result {
  int combat_power = 0;
  double dps = 0.0;
  std::string swing;  // the attack the fight chose against a lone mob
  double swing_seconds = 0.0;
  // Everything behind the two headline numbers, for --detail.
  //
  // The weapon is read from the character rather than the weapon table, since
  // --endowed buys its weapon and an unaffordable shelf is part of the answer.
  std::string weapon;
  int primary = 0;
  int attack = 0;
  double mastery = 0.0;
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  double boss_pct = 0.0;
  double ied = 0.0;
  double weapon_constant = 0.0;
  double skill_pct = 0.0;
  int lines = 0;
  // Value of one shadow copy of a line, or 0 without Shadow Partner. The attack
  // line next to it counts the copies, so it has to say they're there.
  double mirror_pct = 0.0;
  double swing_damage = 0.0;
  double final_attack_damage = 0.0;
  int unspent_sp = 0;
  // Result of playing the fight out, for a --fight in the catalog. The rate
  // above is the damage dealt; this says whether the fight ends before the
  // character gives up.
  bool cleared = false;
  double clear_seconds = 0.0;
  double left = 0.0;
  // What --endowed did with its resources, for the detail line. A bench that
  // hands out meso must show what it turned into, or a low row can't be told
  // apart from a shopper that never spent.
  GearSpend spend;
  int64_t meso_left = 0;
  int64_t v_points_left = 0;
  std::vector<std::pair<std::string, int>> skills;
  // The matrix: each node, its level and its cost. Kept apart from the book
  // because the two are bought from different pools.
  struct NodeHeld {
    std::string name;
    int level = 0;
    int points = 0;
  };
  std::vector<NodeHeld> nodes;
  // Share of the run's damage from each source, largest first: a row per attack
  // and a row per source on its own clock. Used to decide whether a skill is
  // earning its points.
  std::vector<std::pair<std::string, double>> shares;
};

// Extra hits other skills in the character's book give `swing`. Greater Vessel
// of Light gives Blast an eleventh, and printing ten next to eleven hits' worth
// of damage wouldn't add up.
int BoostedLines(const GameState& state, const std::string& swing) {
  int lines = 0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (state.character.skill_level(entry.second) <= 0 ||
        !state.character.HasBookFor(entry.second)) {
      continue;
    }
    for (const SkillBoost& boost : entry.second.boost()) {
      if (boost.skill_name() == swing) {
        lines += boost.lines();
      }
    }
  }
  return lines;
}

// The character's whole book, and the figures of the attack they settled on.
// Filled here rather than from the AttackOption, because the page prints the
// skill's own data, not the damage it produced.
void RecordBook(const GameState& state, const DerivedStats& derived,
                const std::string& swing, Result* result) {
  for (const std::pair<const int32_t, int32_t>& entry :
       state.character.proto().sp_by_stage()) {
    result->unspent_sp += entry.second;
  }
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    int learned = state.character.skill_level(entry.second);
    if (learned <= 0) {
      continue;
    }
    // Nodes come from the matrix rather than a book, so the two lists are
    // gathered separately and kept apart, because a node's cost is what the V
    // pool's allocation is judged by.
    if (entry.second.v_node() != V_NODE_KIND_UNSPECIFIED) {
      if (state.character.HoldsSkillFrom(entry.second)) {
        result->nodes.push_back({entry.second.name(), learned,
                                 VNodeCost(entry.second.v_node(), 0, learned)});
      }
      continue;
    }
    if (!state.character.HasBookFor(entry.second)) {
      continue;
    }
    result->skills.push_back({entry.second.name(), learned});
    if (entry.second.name() != swing) {
      continue;
    }
    result->skill_pct = entry.second.base().skill_pct() +
                        entry.second.per_level().skill_pct() * (learned - 1);
    // An attack strengthened by another skill is worth more per line than its
    // own data says, and the printed line must agree with the damage next to
    // it.
    std::map<std::string, SkillBonus>::const_iterator boost =
        derived.skill_bonus.find(swing);
    if (boost != derived.skill_bonus.end()) {
      result->skill_pct += boost->second.skill_pct;
    }
    result->lines =
        SkillLinesAt(entry.second, learned) + BoostedLines(state, swing);
    // Bolt Surplus's extra hit is in the damage next to this count, so it must
    // be in the count too, on the same terms the damage chain grants it.
    if (result->lines > 1) {
      result->lines += derived.bonus_attack_lines;
    }
  }
}

// Where the run's damage went, as shares: a row per attack, and a row per
// source on its own clock. A summon doesn't compete with anything for time, so
// its share is simply what it added.
void RecordShares(const CombatParams& params, const Sequence& played,
                  Result* result) {
  double total = played.damage;
  if (total <= 0.0) {
    return;
  }
  for (int i = 0; i < static_cast<int>(played.by_attack.size()); ++i) {
    if (played.by_attack[i].damage <= 0.0) {
      continue;
    }
    // Damage triggered by the attack is named separately and removed from it. A
    // Meso Explosion or a poison lands under the attack's own figure, and
    // someone tuning the attack means the hit itself, not what it set off.
    const AttackTally& tally = played.by_attack[i];
    double rode = tally.final_attack_damage + tally.burn_damage;
    result->shares.push_back(
        {params.attacks[i].name, (tally.damage - rode) / total});
    if (tally.final_attack_damage > 0.0) {
      result->shares.push_back({params.attacks[i].name + " (final attack)",
                                tally.final_attack_damage / total});
    }
    if (tally.burn_damage > 0.0) {
      result->shares.push_back(
          {params.attacks[i].name + " (burn)", tally.burn_damage / total});
    }
  }
  // Split by source rather than lumped into one row. On a branch whose summons
  // outdo its attacks, "(own clock) 61%" names no skill to tune.
  for (const std::pair<std::string, double>& source :
       played.own_clock_by_source) {
    if (source.second <= 0.0) {
      continue;
    }
    result->shares.push_back({source.first, source.second / total});
  }
  std::sort(result->shares.begin(), result->shares.end(),
            [](const std::pair<std::string, double>& a,
               const std::pair<std::string, double>& b) {
              return a.second > b.second;
            });
}

// The max character at `level`, geared and skilled by --mode=max. The branch is
// named by its own advancement, so the level decides how far it goes: a Hero at
// 200 is seeded as a 5th job Hero with the matrix maxed.
GameState MaxState(const Catalogs& catalogs, int level, Job branch) {
  return NewMaxState(catalogs, AdvancementForJobStage(branch, StageOf(branch)),
                     level, kSimSeed);
}

// How many of each other token the bag gets. More than any shelf asks, so what
// the character ends up with depends on tier, not count.
constexpr int kTokensGiven = 999;

// Every token shelf below AbsoLab's, paid for, since a player has that gear by
// the time they face Lotus. Only tokens some shelf is priced in: the bag holds
// 128 rows a tab, and one of everything would leave no room for the gear the
// shopper is about to buy.
void GiveTokens(GameState& state) {
  std::set<std::string> shelves;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state.equips) {
    if (entry.second.token_price() > 0 &&
        entry.second.token_item() != kAbsoLabToken) {
      shelves.insert(entry.second.token_item());
    }
  }
  for (const std::string& key : shelves) {
    std::map<std::string, ItemPrototype>::const_iterator it =
        state.items.find(key);
    if (it != state.items.end()) {
      state.character.AddItem(it->second, kTokensGiven);
    }
  }
}

// The rank --endowed rerolls the Inner Ability toward.
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

// Spends the endowed character's resources in the order that makes each step
// worthwhile: the matrix first, since Hyper Stats and cubes are bought around
// the nodes, then the two allocations, then the shelf. Repeated --rounds times,
// because each of the four changes the value of the others.
GearSpend SpendEverything(GameState& state, const Fight& fight) {
  SkillRate rate = [&fight](GameState& inner) {
    return PlanRate(inner, fight);
  };
  GearShopper shopper{GearPlan()};
  for (int round = 0; round < absl::GetFlag(FLAGS_rounds); ++round) {
    // The shelf first, since everything after is ranked on damage dealt, and a
    // character still under the fight's defence wall deals nothing whatever
    // they buy. Gear is what gets them over, so the rest plans against it.
    shopper.Spend(state);
    WearBestFromBag(state.character);
    SpendVMatrix(state, rate);
    if (state.character.proto().level() >= kHyperStatUnlockLevel) {
      SpendHyperStats(
          state, StatPreset::kSecond,
          MeasureHyperWorth(state, StatPreset::kSecond,
                            [&rate](GameState& in) { return rate(in); }));
    }
    if (state.character.inner_ability_unlocked()) {
      SpendHonorOnAbility(
          state, AbilityRankWanted(), StatPreset::kSecond,
          MeasureAbilityWorth(state, StatPreset::kSecond,
                              [&rate](GameState& in) { return rate(in); }));
    }
  }
  return shopper.life();
}

// The endowed character: every branch gets the same meso, honor and V Points,
// wears everything that drops or sells below AbsoLab, and spends it all however
// it likes. What --max grants by fiat this one has to buy, so a branch that
// can't turn meso into damage scores lower here than there, which is what this
// mode measures.
GearSpend Endow(GameState& state, const Catalogs& catalogs, int level,
                const Build& build, const Fight& fight) {
  state.current_map = kDummyMap;
  GrowTo(state, level, PathTo(build.job));
  // After the climb, so nothing is spent on the way up. The pools are what the
  // bench hands out, and spending at each level would give different amounts to
  // branches that got there sooner.
  state.character.AddMeso(absl::GetFlag(FLAGS_meso));
  state.character.AddHonor(absl::GetFlag(FLAGS_honor));
  state.character.AddVPoints(absl::GetFlag(FLAGS_v_points));
  GiveTokens(state);
  // The weapon comes first and is given, not bought. The tier above the shop's
  // is a drop no shelf sells, so shopping would leave every branch four tiers
  // short. It's also first because it needs bag space, and because Outfit then
  // measures the shelf against the weapon in hand.
  Wear(state, BestOfType(catalogs, build.weapon, level,
                         /*below_absolab=*/true));
  EquipType ammo = AmmoFor(build.weapon);
  if (ammo != EQUIP_TYPE_UNSPECIFIED) {
    Wear(state, BestOfType(catalogs, ammo, level, /*below_absolab=*/true));
  }
  // Drops before the shelf, so the shop fills only what's still empty rather
  // than buying over what dropped.
  OutfitDrops(state);
  Outfit(state, /*budget=*/true, build.weapon);
  WearBestFromBag(state.character);
  CollectSymbols(state.character);
  return SpendEverything(state, fight);
}

// Prepares the character a row measures: the max character seeded whole, an
// endowed character's resources spent, or a fresh character grown and armed.
// False if the build asks for a weapon this character never ends up holding.
bool Outfit(const Catalogs& catalogs, int level, const Build& build,
            const Fight& fight, GameState& state, Result& result) {
  bool max = absl::GetFlag(FLAGS_max);
  bool endowed = absl::GetFlag(FLAGS_endowed);
  state.bosses = catalogs.bosses;
  if (endowed) {
    result.spend = Endow(state, catalogs, level, build, fight);
    result.meso_left = state.character.meso();
    result.v_points_left = state.character.v_points();
  } else if (max) {
    // The max character is already armed; this build's row is whichever of the
    // branch's weapons it chose.
    if (state.character.weapon_type() != build.weapon) {
      return false;
    }
  } else {
    GrowTo(state, level, PathTo(build.job));
    if (!Wear(state, BestOfType(catalogs, build.weapon, level,
                                /*below_absolab=*/false))) {
      return false;
    }
    EquipType ammo = AmmoFor(build.weapon);
    if (ammo != EQUIP_TYPE_UNSPECIFIED &&
        !Wear(state, BestOfType(catalogs, ammo, level,
                                /*below_absolab=*/false))) {
      return false;
    }
  }
  state.current_map = kDummyMap;
  if (!max && !endowed && absl::GetFlag(FLAGS_upgraded)) {
    FullyUpgrade(state);
  }
  int bonus_stat = absl::GetFlag(FLAGS_bonus_stat);
  int bonus_attack = absl::GetFlag(FLAGS_bonus_attack);
  int bonus_boss = absl::GetFlag(FLAGS_bonus_boss_pct);
  int bonus_ied = absl::GetFlag(FLAGS_bonus_ied);
  if (bonus_stat > 0 || bonus_attack > 0 || bonus_boss > 0 || bonus_ied > 0) {
    state.equips[kCharm] =
        Charm(build.job, bonus_stat, bonus_attack, bonus_boss, bonus_ied);
    Wear(state, kCharm);
  }
  return true;
}

// The stat line a row prints: everything the damage chain read, so a suspicious
// figure can be traced to its cause.
void RecordStatLine(const OffenseStats& bare, const AttackOption& best,
                    Result& result) {
  result.swing = best.name;
  result.primary = bare.primary;
  result.attack = bare.attack;
  result.mastery = bare.mastery;
  result.crit_rate = bare.crit_rate;
  result.crit_dmg = bare.crit_dmg;
  result.damage_pct = bare.damage_pct;
  result.final_dmg_pct = bare.final_dmg_pct;
  result.boss_pct = bare.boss_pct;
  result.ied = bare.ied;
  result.weapon_constant = bare.weapon_constant;
  result.swing_damage = best.damage_per_hit[0];
  result.final_attack_damage =
      best.final_attack_damage.empty() ? 0.0 : best.final_attack_damage[0];
}

Result Measure(const Catalogs& catalogs, int level, const Build& build,
               const Fight& fight) {
  Result result;
  // The max character is seeded whole; the other two start from a fresh
  // character. A GameState can't be assigned, so which one is decided here.
  GameState state = absl::GetFlag(FLAGS_max)
                        ? MaxState(catalogs, level, build.job)
                        : NewState(catalogs, kSimSeed);
  if (!Outfit(catalogs, level, build, fight, state, result)) {
    return result;
  }

  // A boss fight uses the character's bossing preset (its hyper stats, Inner
  // Ability and gear), and that's the only preset the Extreme Green Potion's
  // attack speed applies under. Using farming here geared every branch for the
  // wrong fight.
  bool boss = Bossing();
  Activity activity = boss ? Activity::kBossing : Activity::kFarming;

  const Character& proto = state.character.proto();
  DerivedStats derived =
      DerivedStatsFor(state.character, state.skills, {}, {}, activity);
  OffenseStats bare = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(),
      TotalEquipStats(state.character, derived), state.character.weapon_type(),
      /*attack_skill=*/nullptr, /*attack_level=*/0, PassiveOffenseFor(derived));
  // Bosses are what the ladder is built against, here and in every other sim:
  // normal-monster %damage buys nothing a player of this game lacks.
  result.combat_power = CombatPower(bare, /*vs_boss=*/true);

  CombatParams params = ParamsFor(state, fight);
  int enemies = absl::GetFlag(FLAGS_enemies);
  // Remove the map's pacing stretch, so the figure is at 1x and two levels
  // compare directly. A boss fight is already 1x: ComputeBossParams pins it
  // there, since a fight the player watches needs neither the stretch nor a
  // respawn delay.
  double speed = boss ? 1.0 : GameSpeedFactor(level);
  // The window is given in game seconds but MeasureFight counts stretched
  // seconds, so it's stretched to match; at 200 a ten-minute window is 6000 of
  // them. Passing the flag raw would make --seconds mean a different length at
  // every level. See GameSpeedFactor.
  Sequence played =
      MeasureFight(params, absl::GetFlag(FLAGS_seconds) * speed, enemies);
  if (played.main_attack < 0 || played.seconds <= 0.0) {
    return result;
  }
  const AttackOption* best = &params.attacks[played.main_attack];
  RecordStatLine(bare, *best, result);
  result.weapon = HeldWeaponName(state.character);
  RecordBook(state, derived, best->name, &result);
  result.mirror_pct = derived.mirror_line_pct;
  result.swing_seconds = best->swing_seconds / speed;
  // Total damage over the run's length: attacks, summons and the burns they
  // left. Scaled back to 1x so two levels compare directly.
  result.dps = played.damage * speed / played.seconds;
  RecordShares(params, played, &result);
  // Last of all: a clear pays the character, and every figure above describes
  // the character entering the fight.
  if (fight.real) {
    BossOutcome outcome = FightBoss(state, fight.boss, fight.difficulty);
    result.cleared = outcome.won;
    result.clear_seconds = outcome.seconds;
    result.left = outcome.left;
  }
  return result;
}

// The stat line behind one row, under --detail. Everything the damage chain
// read, so a suspicious figure can be traced to its cause.
void PrintDetail(const Build& build, const Result& result) {
  // The shadow's lines are already in the damage; without this the count next
  // to it would be half what really landed. Its percentage is what one shadow
  // line deals, not its share: a 70% shadow behind a 210% line deals 147%, and
  // a bare 70% next to "210%" would read as a flat figure.
  char shadow_buf[48] = "";
  if (result.mirror_pct > 0.0) {
    std::snprintf(shadow_buf, sizeof(shadow_buf), " + %d shadow @ %.0f%%",
                  result.lines, 100.0 * result.mirror_pct * result.skill_pct);
  }
  std::printf(
      "            %s %d  ATT %d  wc %.2f  mastery %.0f%%  crit %.0f%%/%.0f%%  "
      "dmg %.0f%%  FD %.0f%%  boss %.0f%%  IED %.0f%%\n"
      "            swing %.0f (%d lines @ %.0f%%%s)  final attack %.0f  "
      "unspent SP %d\n            ",
      PrimaryStatName(build.job), result.primary, result.attack,
      result.weapon_constant, 100.0 * result.mastery, 100.0 * result.crit_rate,
      100.0 * result.crit_dmg, 100.0 * result.damage_pct,
      100.0 * result.final_dmg_pct, 100.0 * result.boss_pct, 100.0 * result.ied,
      result.swing_damage, result.lines, 100.0 * result.skill_pct, shadow_buf,
      result.final_attack_damage, result.unspent_sp);
  for (const std::pair<std::string, int>& skill : result.skills) {
    std::printf("%s %d  ", skill.first.c_str(), skill.second);
  }
  if (!result.nodes.empty()) {
    std::vector<Result::NodeHeld> nodes = result.nodes;
    std::sort(nodes.begin(), nodes.end(),
              [](const Result::NodeHeld& a, const Result::NodeHeld& b) {
                return a.points > b.points;
              });
    int spent = 0;
    std::printf("\n            matrix: ");
    for (const Result::NodeHeld& node : nodes) {
      std::printf("%s %d (%dvp)  ", node.name.c_str(), node.level, node.points);
      spent += node.points;
    }
    std::printf("= %d VP", spent);
  }
  std::printf("\n            ");
  for (const std::pair<std::string, double>& share : result.shares) {
    std::printf("%s %.1f%%  ", share.first.c_str(), 100.0 * share.second);
  }
  if (absl::GetFlag(FLAGS_endowed)) {
    const GearSpend& spend = result.spend;
    std::printf(
        "\n            spent %.2fB: scrolls %.2fB (%d slots)  stars %.2fB "
        "(%d)  hammers %.2fB (%d)  cubes %.2fB (%d kept of %d)  symbols "
        "%.2fB (%d)\n            %.2fB meso and %lld V Points left over",
        spend.meso() / 1e9, spend.scrolls / 1e9, spend.slots_filled,
        spend.stars / 1e9, spend.stars_gained, spend.hammers / 1e9,
        spend.hammers_driven, spend.cubes / 1e9, spend.cubes_kept,
        spend.cubes_bought, spend.symbols / 1e9, spend.symbol_levels,
        result.meso_left / 1e9, static_cast<long long>(result.v_points_left));
  }
  std::printf("\n\n");
}

// Each branch against every weapon type it has mastery for. Two rows when a
// branch masters two, so the comparison that decides its weapon stays on the
// table.
const Build kBuilds[] = {
    {JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_SWORD},
    {JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_AXE},
    {JOB_PAGE, EQUIP_TYPE_TWO_HANDED_SWORD},
    {JOB_PAGE, EQUIP_TYPE_TWO_HANDED_BLUNT},
    {JOB_SPEARMAN, EQUIP_TYPE_SPEAR},
    {JOB_SPEARMAN, EQUIP_TYPE_POLEARM},
    {JOB_HUNTER, EQUIP_TYPE_BOW},
    {JOB_CROSSBOWMAN, EQUIP_TYPE_CROSSBOW},
    {JOB_ICE_LIGHTNING_WIZARD, EQUIP_TYPE_STAFF},
    {JOB_FIRE_POISON_WIZARD, EQUIP_TYPE_STAFF},
    {JOB_CLERIC, EQUIP_TYPE_STAFF},
    {JOB_ASSASSIN, EQUIP_TYPE_CLAW},
    {JOB_BANDIT, EQUIP_TYPE_DAGGER},
    {JOB_BERSERKER, EQUIP_TYPE_SPEAR},
    {JOB_BERSERKER, EQUIP_TYPE_POLEARM},
    {JOB_CRUSADER, EQUIP_TYPE_TWO_HANDED_SWORD},
    {JOB_CRUSADER, EQUIP_TYPE_TWO_HANDED_AXE},
    {JOB_WHITE_KNIGHT, EQUIP_TYPE_TWO_HANDED_SWORD},
    {JOB_WHITE_KNIGHT, EQUIP_TYPE_TWO_HANDED_BLUNT},
    {JOB_RANGER, EQUIP_TYPE_BOW},
    {JOB_SNIPER, EQUIP_TYPE_CROSSBOW},
    {JOB_ICE_LIGHTNING_MAGE, EQUIP_TYPE_STAFF},
    {JOB_FIRE_POISON_MAGE, EQUIP_TYPE_STAFF},
    {JOB_PRIEST, EQUIP_TYPE_STAFF},
    {JOB_HERMIT, EQUIP_TYPE_CLAW},
    {JOB_CHIEF_BANDIT, EQUIP_TYPE_DAGGER},
    {JOB_DARK_KNIGHT, EQUIP_TYPE_SPEAR},
    {JOB_DARK_KNIGHT, EQUIP_TYPE_POLEARM},
    {JOB_PALADIN, EQUIP_TYPE_TWO_HANDED_SWORD},
    {JOB_PALADIN, EQUIP_TYPE_TWO_HANDED_BLUNT},
    {JOB_HERO, EQUIP_TYPE_TWO_HANDED_SWORD},
    {JOB_HERO, EQUIP_TYPE_TWO_HANDED_AXE},
    {JOB_BOW_MASTER, EQUIP_TYPE_BOW},
    {JOB_MARKSMAN, EQUIP_TYPE_CROSSBOW},
    {JOB_ICE_LIGHTNING_ARCH_MAGE, EQUIP_TYPE_STAFF},
    {JOB_FIRE_POISON_ARCH_MAGE, EQUIP_TYPE_STAFF},
    {JOB_BISHOP, EQUIP_TYPE_STAFF},
    {JOB_NIGHT_LORD, EQUIP_TYPE_CLAW},
    {JOB_SHADOWER, EQUIP_TYPE_DAGGER},
};

// What the table was measured against. A table read a week later is worthless
// if it doesn't say what the character was fighting.
std::string CrowdLine(const Fight& fight) {
  if (fight.real) {
    return fight.data.name() + " " + absl::GetFlag(FLAGS_fight);
  }
  char crowd[64];
  if (absl::GetFlag(FLAGS_boss)) {
    std::snprintf(crowd, sizeof(crowd), "a boss holding %d%% PDR",
                  absl::GetFlag(FLAGS_boss_pdr));
  } else {
    int enemies = absl::GetFlag(FLAGS_enemies);
    std::snprintf(crowd, sizeof(crowd), "%d mob%s of the same level", enemies,
                  enemies == 1 ? "" : "s");
  }
  return crowd;
}

// How the row was seeded, which is most of what the table means.
std::string SeedLine() {
  if (absl::GetFlag(FLAGS_endowed)) {
    char line[192];
    std::snprintf(
        line, sizeof(line),
        "the endowed character: %.1fB meso, %lld honor and %lld V Points, "
        "spent by the shopper",
        absl::GetFlag(FLAGS_meso) / 1e9,
        static_cast<long long>(absl::GetFlag(FLAGS_honor)),
        static_cast<long long>(absl::GetFlag(FLAGS_v_points)));
    return line;
  }
  if (absl::GetFlag(FLAGS_max)) {
    return "the ceiling character: max gear, hyper stats, ability and V matrix";
  }
  return "all AP in the job's primary stat, every skill maxed";
}

// Result of one attempt at a real fight: the clear time, or how much of the
// boss was left when the character gave up.
std::string ClearCell(const Result& result) {
  char cell[24];
  if (result.cleared) {
    std::snprintf(cell, sizeof(cell), "%.0fs", result.clear_seconds);
  } else {
    std::snprintf(cell, sizeof(cell), "%.0f%% left", 100.0 * result.left);
  }
  return cell;
}

void Run(int level) {
  Catalogs catalogs = LoadCatalogsWithDummy(level);
  Fight fight = ChosenFight(catalogs);

  // A real fight is reported in the unit boss decisions use; the dummy stays in
  // the per-second unit the weapon table has always used.
  bool per_minute = fight.real;
  std::printf("Level %d, %s. %s is against %s over %.0fs, at 1x speed.\n\n",
              level, SeedLine().c_str(), per_minute ? "DPM" : "DPS",
              CrowdLine(fight).c_str(), absl::GetFlag(FLAGS_seconds));
  std::printf("%-13s  %-22s  %7s  %12s  %-18s  %5s%s\n", "job", "weapon", "CP",
              per_minute ? "DPM" : "DPS", "swing", "sec",
              fight.real ? "  fight" : "");
  std::printf("%s\n", std::string(fight.real ? 98 : 85, '-').c_str());
  const std::string& only = absl::GetFlag(FLAGS_branch);
  for (const Build& build : kBuilds) {
    if (!only.empty() && build.job != ParseBranch(only)) {
      continue;
    }
    Result result = Measure(catalogs, level, build, fight);
    // A max character that doesn't match the row measured nothing: the branch
    // holds its other weapon, and that row prints instead.
    if (absl::GetFlag(FLAGS_max) && result.combat_power == 0) {
      continue;
    }
    std::string key =
        BestOfType(catalogs, build.weapon, level, /*below_absolab=*/false);
    std::string weapon = result.weapon;
    if (weapon.empty() || weapon == "-") {
      weapon = catalogs.equips.count(key) > 0 ? catalogs.equips.at(key).name()
                                              : "(none this level can wear)";
    }
    // A charm big enough to equalise two jobs pushes the character's CP past an
    // int's range. The figure is the game's own, so it's dropped here rather
    // than widened there.
    std::string power =
        result.combat_power > 0 ? std::to_string(result.combat_power) : "-";
    std::printf("%-13s  %-22s  %7s  %12.1f  %-18s  %5.2f%s%s\n",
                BranchName(build.job).c_str(), weapon.c_str(), power.c_str(),
                result.dps * (per_minute ? 60.0 : 1.0), result.swing.c_str(),
                result.swing_seconds, fight.real ? "  " : "",
                fight.real ? ClearCell(result).c_str() : "");
    if (absl::GetFlag(FLAGS_detail)) {
      PrintDetail(build, result);
    }
    std::fflush(stdout);
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run(absl::GetFlag(FLAGS_level));
  return 0;
}
