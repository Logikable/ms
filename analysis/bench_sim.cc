/* The counterfactual bench: every branch held at the same numbers, so a gap
 * between two of them can be attributed to a cause.
 *
 * This is the one question progression_sim cannot answer, and not for want of
 * accuracy -- by construction every character it produces got where they are
 * differently, which is the whole point of it. Here the gear is written rather
 * than earned, the charm flags (--bonus_stat, --bonus_attack, --bonus_boss_pct,
 * --bonus_ied) hand every branch the same helping of one lever, and --boss_pdr
 * dials a property of the target. Sweep one and the field's answer to it is
 * what moves. That is how ignored defence was shown to be the whole of the
 * Lv230 class spread: the ten branches spread 1.50x at 0% defence and 2.76x at
 * 300%.
 *
 * So READ NOTHING HERE AS A FORECAST. The DPS column says what a branch would
 * do holding gear this file wrote for it, which is not gear anybody reached --
 * the ceiling cubes one ignored-defence line by fiat where the played
 * character ends up with whatever the shopper bought. What a branch actually
 * manages is progression_sim's question and only its question.
 *
 * By default DPS is measured against a lone mob of the character's own level
 * on an otherwise empty map, so it is the character and the weapon being
 * compared and nothing else: no crowd for a wide skill to take advantage of,
 * no spawn cap to hide a difference behind. --enemies and --boss ask the other
 * two questions, and the header says which of the three was asked.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:bench_sim -- --level=140 --enemies=8
 *   bazelisk run //analysis:bench_sim -- --level=230 --max --boss
 * --boss_pdr=300 bazelisk run //analysis:bench_sim -- --level=230 --max --boss
 * --bonus_ied=60
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

// Fixes the random stream every run of this sim draws from. Rewards are
// rolled, so an unseeded run would print a table that moved a little each
// time and hide a real change under the noise.
constexpr unsigned int kSimSeed = 20260813;

// The map and mob the comparison is run on, invented here rather than taken
// from the catalog: the real maps stop well below the trial cap, and a crowd
// would let a wide skill answer a question about one weapon against one mob.
constexpr char kDummyMap[] = "__dps_dummy";
constexpr char kDummyMob[] = "__dps_dummy_mob";
constexpr char kDummyBoss[] = "__dps_dummy_boss";

// One row of the table: which branch, holding what. Whatever the weapon draws
// from is bought with it, since an empty projectile slot reads as a broken
// build -- an Assassin's damage is mostly in the ammunition.
struct Build {
  Job job = JOB_UNSPECIFIED;
  EquipType weapon = EQUIP_TYPE_UNSPECIFIED;
};

// What the row's primary figure is called, so the detail line labels the stat
// it actually holds rather than assuming a warrior's.
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

// The charm the two bonus flags ride on: a hat, because nothing in the
// catalog fills that slot, carrying the stat and the attack and nothing else.
// A charm is not gear a player can reach -- it is there to hold two jobs at
// the same numbers and see what is left between them.
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

// The catalogs with the mob this sim measures against added: one of the
// character's own level, so the level multiplier lands where a player fighting
// their own tier would put it. No PDR and no boss flag unless the flags asked
// -- the whole shipped catalog is built that way, and both would scale every
// row alike anyway. Its HP is the measurement's own: the dummy never falls, so
// a held swing has to see something no hold can finish.
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

// Whether the row is measured against a boss: the invented one --boss stands
// up, or whichever of the catalog's --fight names.
bool Bossing() {
  return absl::GetFlag(FLAGS_boss) || !absl::GetFlag(FLAGS_fight).empty();
}

// The fight --boss asks about: one phase holding the dummy alone.
//
// Built rather than taken from the catalog because the dummy is what makes a
// measurement a measurement -- it never falls and it is the character's own
// level. Handing it to ComputeBossParams instead of building the encounter
// here is the point: a boss fight paces at 1x where a map stretches, halves
// reach, takes the healthiest part first and reads the character's BOSSING
// preset. Approximating that with a boss-flagged mob on a farming character
// is what this sim did until 2026-09-14, and it measured a different
// character than progression_sim did -- by 49% on a Bishop, whose damage is
// one attack-speed-bound cast, and by nothing at all on a Bow Master, whose
// damage is mostly summons.
BossDifficulty DummyFight() {
  BossDifficulty difficulty;
  difficulty.set_name("Dummy");
  Spawn* spawn = difficulty.add_phases()->add_spawns();
  spawn->set_mob(kDummyMob);
  spawn->set_count(1);
  return difficulty;
}

// What every row of the table is measured against, and what the planning
// decisions are ranked on. The invented dummy and a catalog boss are the same
// thing here -- a difficulty and a phase -- so nothing downstream has to ask
// which it got.
struct Fight {
  std::string boss;  // the catalog key, or the dummy's stand-in name
  int difficulty = 0;
  BossDifficulty data;
  // Whether it is a fight that can be WON. The dummy never falls, so only a
  // catalog boss is played out to an end.
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

// The fight's own combat params. One builder for the measurement and for every
// spending decision alike, so a point bought to raise a number was ranked
// against the fight the row actually reports.
CombatParams ParamsFor(GameState& state, const Fight& fight) {
  if (!Bossing()) {
    return ComputeCombatParams(state);
  }
  return ComputeBossParams(state, fight.boss, fight.data,
                           BossObjectivePhase(state.mobs, fight.data));
}

// How long one spending decision is played out for. Twice the slowest cooldown
// the character holds when that is longer than the flag: a node on a
// two-minute clock reads as free damage inside any window it never comes back
// in, and a shopper ranking against that window buys the wrong thing.
double PlanWindow(const GameState& state) {
  double cycle = 0.0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (state.character.HoldsSkillFrom(entry.second)) {
      cycle = std::max(cycle, entry.second.cooldown_seconds());
    }
  }
  return std::max(absl::GetFlag(FLAGS_plan_seconds), 2.0 * cycle);
}

// What the character takes off the fight, per second at 1x. The one rate every
// --endowed decision is ranked on -- the gear, the Hyper Stats, the Ability
// and the matrix alike -- so the whole purse is spent towards one answer.
double PlanRate(GameState& state, const Fight& fight) {
  CombatParams params = ParamsFor(state, fight);
  if (!params.active) {
    return 0.0;
  }
  // A boss fight is pinned at 1x; a map stretches, and MeasureFight counts in
  // the stretched clock either way. See GameSpeedFactor.
  double speed =
      Bossing() ? 1.0 : GameSpeedFactor(state.character.proto().level());
  Sequence played = MeasureFight(params, PlanWindow(state) * speed,
                                 absl::GetFlag(FLAGS_enemies));
  return played.seconds > 0.0 ? played.damage * speed / played.seconds : 0.0;
}

// Puts `key` on the character, in whichever slot its prototype names, dropping
// whatever was already there. Returns false if the catalog has no such item.
// The token AbsoLab's shelf is priced in, and the one thing the endowed
// character is not handed: Lotus and Damien drop it, so a character walking IN
// to those fights has none.
constexpr char kAbsoLabToken[] = "absolab_coin";

// The best weapon of `type` a character at `level` can wear. Named by type
// rather than by catalog key so the table below never has to be edited when a
// tier is added -- which is the whole reason a build says "claw" and not
// "dark_gigantic". `below_absolab` stops at the tier under the one Lotus and
// Damien pay for.
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

bool Wear(GameState& state, const std::string& key) {
  std::map<std::string, EquipPrototype>::const_iterator it =
      state.equips.find(key);
  if (it == state.equips.end()) {
    return false;
  }
  state.character.Unequip(it->second.equip_slot());
  state.character.PickUp(std::make_unique<EquipInstance>(it->second));
  // The one just picked up is the last row of the bag.
  return state.character.Equip(state.character.inventory().size() - 1);
}

// What one build came to.
struct Result {
  int combat_power = 0;
  double dps = 0.0;
  std::string swing;  // the attack the fight chose against a lone mob
  double swing_seconds = 0.0;
  // Everything behind the two headline numbers, for --detail.
  // What they ended up holding. Off the character rather than off the ladder:
  // --endowed BUYS its weapon, and a shelf the purse could not reach is part
  // of the answer.
  std::string weapon;
  int primary = 0;
  int attack = 0;
  double mastery = 0.0;
  double crit_rate = 0.0;
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  double boss_pct = 0.0;
  double ied = 0.0;
  double weapon_constant = 0.0;
  double skill_pct = 0.0;
  int lines = 0;
  // What a shadow copy of one line is worth, or 0 for a character with no
  // Shadow Partner. The swing beside it counts them, so the line has to say
  // they are there.
  double mirror_pct = 0.0;
  double swing_damage = 0.0;
  double final_attack_damage = 0.0;
  int unspent_sp = 0;
  // What playing the fight out actually came to, for a --fight the catalog
  // holds. The rate above says what the character takes off it; this says
  // whether the fight ends before the character's patience does.
  bool cleared = false;
  double clear_seconds = 0.0;
  double left = 0.0;
  // What --endowed did with what it was handed, for the detail line. A bench
  // that hands out a purse has to show what the purse turned into, or a row
  // that reads low cannot be told from a shopper that never spent.
  GearSpend spend;
  int64_t meso_left = 0;
  int64_t v_points_left = 0;
  std::vector<std::pair<std::string, int>> skills;
  // The matrix: each node held, its level and what it cost. Apart from the
  // book because the two are bought out of different pools.
  struct NodeHeld {
    std::string name;
    int level = 0;
    int points = 0;
  };
  std::vector<NodeHeld> nodes;
  // Share of the run's damage each source took, largest first -- a row per
  // swing and a row per clock of its own. What it is for is deciding whether a
  // skill in the book is earning its points.
  std::vector<std::pair<std::string, double>> shares;
};

// Strikes another skill hands `swing`, summed over the book the character
// holds. Greater Vessel of Light gives Blast an eleventh, and a line that
// printed ten beside eleven strikes' worth of damage would not add up.
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

// The whole book the character ended up with, and the figures of the swing
// they settled on. Filled here rather than read off the AttackOption because
// what the page prints is the skill's own data, not the damage it produced.
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
    // A node is held through the matrix rather than through a book, so the two
    // lists are gathered on their own terms -- and kept apart, because what a
    // node cost is the question the V pool's allocation is read by.
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
    // A swing another skill strengthens is worth more per line than its own
    // data says, and the line printing it has to agree with the damage beside
    // it.
    std::map<std::string, SkillBonus>::const_iterator boost =
        derived.skill_bonus.find(swing);
    if (boost != derived.skill_bonus.end()) {
      result->skill_pct += boost->second.skill_pct;
    }
    result->lines =
        SkillLinesAt(entry.second, learned) + BoostedLines(state, swing);
    // Bolt Surplus's strike is in the damage beside this count, so it has to
    // be in the count -- on the same terms the damage chain grants it.
    if (result->lines > 1) {
      result->lines += derived.bonus_attack_lines;
    }
  }
}

// Where the run's damage went, as a share apiece: a row per swing, and a row
// per source on a clock of its own. A summon competes with nothing for the
// clock, so its share is simply what it added.
void RecordShares(const CombatParams& params, const Sequence& played,
                  Result* result) {
  double total = played.damage;
  if (total <= 0.0) {
    return;
  }
  for (int i = 0; i < static_cast<int>(played.damage_by_attack.size()); ++i) {
    if (played.damage_by_attack[i] <= 0.0) {
      continue;
    }
    result->shares.push_back(
        {params.attacks[i].name, played.damage_by_attack[i] / total});
  }
  // Split by what dealt it rather than heaped into one row: on a branch whose
  // summons outweigh its swings, "(own clock) 61%" names no skill to tune.
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

// The ceiling character at `level`, dressed and skilled by --mode=max. The
// branch is named by its own advancement, so the level decides how deep it
// goes -- a Hero at 200 is seeded as a Hero V with the matrix bought out.
GameState MaxState(const Catalogs& catalogs, int level, Job branch) {
  return NewMaxState(catalogs, AdvancementForJobStage(branch, StageOf(branch)),
                     level, kSimSeed);
}

// How many of every other token the bag is given. Past what any shelf asks
// for, so what the character ends up in is the tier rather than the count.
constexpr int kTokensGiven = 999;

// Every token shelf below AbsoLab's, paid for. The Frozen weapons, Princess
// No's secondaries and the boss shoulders are gear a player has by the time
// they meet Lotus, and a bench that left them off would measure the wrong
// character.
//
// Only what some shelf is actually priced in: the bag holds 128 rows a tab,
// and a character handed one of everything has no room left for the gear the
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
      state.character.AddStackable(it->second, kTokensGiven);
    }
  }
}

// The rank --endowed rolls the Inner Ability towards.
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

// Spends what the endowed character was handed, in the order that makes each
// step worth taking: the matrix first, since a node is what the Hyper Stats
// and the cubes are then bought around, then the two allocations off it, then
// the shelf. Gone round --rounds times -- one pass cannot settle it, because
// each of the four changes what the other three are worth.
GearSpend SpendEverything(GameState& state, const Fight& fight) {
  SkillRate rate = [&fight](GameState& inner) {
    return PlanRate(inner, fight);
  };
  GearShopper shopper{GearPlan()};
  for (int round = 0; round < absl::GetFlag(FLAGS_rounds); ++round) {
    // The shelf first, because everything after it is ranked on what the
    // character takes off the fight -- and one still short of the fight's
    // defence wall takes nothing off it whatever they buy. Gear is what
    // carries them over, so it is what the rest gets to plan against.
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

// The endowed character: the same purse, honor and V Points to every branch,
// wearing everything that drops or sells below AbsoLab, and left to spend the
// lot however it likes. What --max writes by fiat this one has to buy, so a
// branch that cannot turn meso into damage reads lower here than it does
// there -- which is the question this mode is for.
GearSpend Endow(GameState& state, const Catalogs& catalogs, int level,
                const Build& build, const Fight& fight) {
  state.current_map = kDummyMap;
  GrowTo(state, level, PathTo(build.job));
  // After the climb, so nothing is spent on the way up: the pools are what the
  // bench is handing over, and a level that spent one would hand a different
  // amount to a branch that reached it sooner.
  state.character.AddMeso(absl::GetFlag(FLAGS_meso));
  state.character.AddHonor(absl::GetFlag(FLAGS_honor));
  state.character.AddVPoints(absl::GetFlag(FLAGS_v_points));
  GiveTokens(state);
  // The weapon first, and handed over rather than bought: the tier above the
  // shop's is a Chaos Root Abyss drop, and no shelf sells it at any price. A
  // bench that made the character shop for one would stand every branch in a
  // weapon four tiers off what they would really be holding. Before the rest
  // because it needs a bag with room in it, and because Outfit then measures
  // the shelf against the weapon that is really in hand.
  Wear(state, BestOfType(catalogs, build.weapon, level,
                         /*below_absolab=*/true));
  EquipType ammo = AmmoFor(build.weapon);
  if (ammo != EQUIP_TYPE_UNSPECIFIED) {
    Wear(state, BestOfType(catalogs, ammo, level, /*below_absolab=*/true));
  }
  // The drops before the shelf, so the shop is asked to fill what is still
  // empty rather than to buy over what fell.
  OutfitDrops(state);
  Outfit(state, /*budget=*/true, build.weapon);
  WearBestFromBag(state.character);
  CollectSymbols(state.character);
  return SpendEverything(state, fight);
}

Result Measure(const Catalogs& catalogs, int level, const Build& build,
               const Fight& fight) {
  bool max = absl::GetFlag(FLAGS_max);
  bool endowed = absl::GetFlag(FLAGS_endowed);
  Result result;
  // The ceiling is seeded whole; the other two start from a fresh character and
  // are built up below. A GameState cannot be assigned, so which one it is has
  // to be settled here.
  GameState state =
      max ? MaxState(catalogs, level, build.job) : NewState(catalogs, kSimSeed);
  state.bosses = catalogs.bosses;
  if (endowed) {
    result.spend = Endow(state, catalogs, level, build, fight);
    result.meso_left = state.character.meso();
    result.v_points_left = state.character.v_points();
  } else if (max) {
    // The ceiling armed them already; the row this build asks about is
    // whichever of the branch's weapons it chose.
    if (state.character.weapon_type() != build.weapon) {
      return result;
    }
  } else {
    GrowTo(state, level, PathTo(build.job));
    if (!Wear(state, BestOfType(catalogs, build.weapon, level,
                                /*below_absolab=*/false))) {
      return result;
    }
    EquipType ammo = AmmoFor(build.weapon);
    if (ammo != EQUIP_TYPE_UNSPECIFIED &&
        !Wear(state, BestOfType(catalogs, ammo, level,
                                /*below_absolab=*/false))) {
      return result;
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

  // A boss fight reads the character's bossing preset -- its hyper stats, its
  // Inner Ability and its gear -- and it is the only preset the Extreme Green
  // Potion's attack speed lands under. Farming here dressed every branch for
  // the wrong fight.
  bool boss = Bossing();
  Activity activity = boss ? Activity::kBossing : Activity::kFarming;

  const Character& proto = state.character.proto();
  DerivedStats derived =
      DerivedStatsFor(state.character, state.skills, {}, {}, activity);
  OffenseStats bare = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(),
      TotalEquipStats(state.character, derived), state.character.weapon_type(),
      /*attack_skill=*/nullptr, /*attack_level=*/0, PassiveOffenseFor(derived));
  // Bosses are what the ladder is built against, here and in every other
  // sim: normal %dmg buys nothing a player of this game is short of.
  result.combat_power = CombatPower(bare, /*vs_boss=*/true);

  CombatParams params = ParamsFor(state, fight);
  int enemies = absl::GetFlag(FLAGS_enemies);
  // Back out the pacing the game stretches a map by, so the figure is the 1x
  // one and two levels can be compared without dividing by hand. A boss fight
  // is already 1x -- ComputeBossParams pins it there, because a fight the
  // player sits and watches wants neither the stretch nor a respawn beat.
  double speed = boss ? 1.0 : GameSpeedFactor(level);
  // The horizon is asked for in game seconds and MeasureFight counts in the
  // stretched ones, so it is stretched to match: at 200 a ten-minute window is
  // 6000 of them. Handing MeasureFight the flag raw would make --seconds mean a
  // different length at every level -- see GameSpeedFactor.
  Sequence played =
      MeasureFight(params, absl::GetFlag(FLAGS_seconds) * speed, enemies);
  if (played.main_attack < 0 || played.seconds <= 0.0) {
    return result;
  }
  const AttackOption* best = &params.attacks[played.main_attack];
  result.swing = best->name;
  result.weapon = HeldWeaponName(state.character);
  result.primary = bare.primary;
  result.attack = bare.attack;
  result.mastery = bare.mastery;
  result.crit_rate = bare.crit_rate;
  result.damage_pct = bare.damage_pct;
  result.final_dmg_pct = bare.final_dmg_pct;
  result.boss_pct = bare.boss_pct;
  result.ied = bare.ied;
  result.weapon_constant = bare.weapon_constant;
  result.swing_damage = best->damage_per_hit[0];
  result.final_attack_damage =
      best->final_attack_damage.empty() ? 0.0 : best->final_attack_damage[0];
  RecordBook(state, derived, best->name, &result);
  result.mirror_pct = derived.mirror_line_pct;
  result.swing_seconds = best->swing_seconds / speed;
  // Everything the run landed over how long it ran: the swings, the summons
  // beside them and the burns they left. Scaled back to 1x, so two levels can
  // be compared without dividing by hand.
  result.dps = played.damage * speed / played.seconds;
  RecordShares(params, played, &result);
  // Last of all: a clear pays the character, and every figure above is what
  // walked IN to the fight.
  if (fight.real) {
    BossOutcome outcome = FightBoss(state, fight.boss, fight.difficulty);
    result.cleared = outcome.won;
    result.clear_seconds = outcome.seconds;
    result.left = outcome.left;
  }
  return result;
}

// The stat line behind one row, under --detail. Everything the damage chain
// read, so a figure that looks wrong can be traced to the factor that made it.
void PrintDetail(const Build& build, const Result& result) {
  // The shadow's lines are already in the damage; without this the count
  // beside it would be half of what really landed. Its percentage is what one
  // shadow line deals, not the share it takes -- a 70% shadow behind a 210%
  // line lands 147%, and the bare 70% beside "210%" reads as a flat figure.
  char shadow_buf[48] = "";
  if (result.mirror_pct > 0.0) {
    std::snprintf(shadow_buf, sizeof(shadow_buf), " + %d shadow @ %.0f%%",
                  result.lines, 100.0 * result.mirror_pct * result.skill_pct);
  }
  std::printf(
      "            %s %d  ATT %d  wc %.2f  mastery %.0f%%  crit %.0f%%  "
      "dmg %.0f%%  FD %.0f%%  boss %.0f%%  IED %.0f%%\n"
      "            swing %.0f (%d lines @ %.0f%%%s)  final attack %.0f  "
      "unspent SP %d\n            ",
      PrimaryStatName(build.job), result.primary, result.attack,
      result.weapon_constant, 100.0 * result.mastery, 100.0 * result.crit_rate,
      100.0 * result.damage_pct, 100.0 * result.final_dmg_pct,
      100.0 * result.boss_pct, 100.0 * result.ied, result.swing_damage,
      result.lines, 100.0 * result.skill_pct, shadow_buf,
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

// Each branch against every weapon type it has mastery for. Two rows where a
// branch masters two, so the comparison that decides which one it is built
// around stays on the table.
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

// What the table says it was hitting. A table read a week later is worth
// nothing if it does not say what was in front of the character.
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

// What one attempt at a real fight came to: the clear time, or how much of the
// body was still standing when the loser walked out.
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

  // A real fight is reported in the unit boss work is decided in; the dummy
  // stays in the per-second one the weapon table has always used.
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
    // A ceiling character the row does not match measured nothing: the branch
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
    // A charm big enough to hold two jobs level carries the character's CP
    // past what an int holds. The figure is the game's own, so it is dropped
    // here rather than widened there.
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
