/* What each 2nd-job branch hits for with each weapon it can hold: combat power
 * and single-target DPS, for a maxed character at a chosen level.
 *
 * The character is grown the way a player gets there -- every AP on the primary
 * stat, every SP on whatever it will buy -- so what is compared is a finished
 * build rather than a stat line typed in by hand. DPS is measured against a
 * lone mob of the character's own level on an otherwise empty map, so it is the
 * character and the weapon being compared and nothing else: no crowd for a wide
 * skill to take advantage of, no spawn cap to hide a difference behind.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:weapon_sim
 *   bazelisk run //analysis:weapon_sim -- --level=40
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/damage.h"
#include "src/combat/encounter.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

ABSL_FLAG(int, level, 60, "Character level, and the level of the mob fought.");
ABSL_FLAG(bool, detail, false,
          "Also print the stat line and skill levels behind each row.");

namespace ms {
namespace {

// The map and mob the comparison is run on, invented here rather than taken
// from the catalog: the real maps stop well below the trial cap, and a crowd
// would let a wide skill answer a question about one weapon against one mob.
constexpr char kDummyMap[] = "__dps_dummy";
constexpr char kDummyMob[] = "__dps_dummy_mob";

// One row of the table: which branch, holding what.
struct Build {
  Job job = JOB_UNSPECIFIED;
  std::string weapon;  // equip catalog key
};

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
    default:
      return "?";
  }
}

// The 1st job a branch is reached through, so the sweep climbs the same path a
// player does and collects that book's skills on the way.
Job FirstJobFor(Job branch) {
  switch (branch) {
    case JOB_HUNTER:
    case JOB_CROSSBOWMAN:
      return JOB_ARCHER;
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_FIRE_POISON_WIZARD:
    case JOB_CLERIC:
      return JOB_MAGICIAN;
    default:
      return JOB_SWORDMAN;
  }
}

// Which stat this job's damage is built on, so the sweep spends AP the way a
// player would rather than leaving it in the pool.
StatField PrimaryStatFor(Job job) {
  switch (job) {
    case JOB_ARCHER:
    case JOB_HUNTER:
    case JOB_CROSSBOWMAN:
      return STAT_FIELD_DEX;
    case JOB_MAGICIAN:
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_FIRE_POISON_WIZARD:
    case JOB_CLERIC:
      return STAT_FIELD_INT;
    case JOB_ROGUE:
      return STAT_FIELD_LUK;
    default:
      return STAT_FIELD_STR;
  }
}

// What the row's primary figure is called, so the detail line labels the stat
// it actually holds rather than assuming a warrior's.
const char* PrimaryStatName(Job job) {
  switch (PrimaryStatFor(job)) {
    case STAT_FIELD_DEX:
      return "DEX";
    case STAT_FIELD_INT:
      return "INT";
    default:
      return "STR";
  }
}

struct Catalogs {
  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
};

Catalogs LoadCatalogs(int level) {
  Catalogs c;
  c.equips = LoadTextProtoMap<EquipPrototype>(EmbeddedEquips());
  c.scrolls = LoadTextProtoMap<Scroll>(EmbeddedScrolls());
  c.items = LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  c.mobs = LoadTextProtoMap<Mob>(EmbeddedMobs());
  c.maps = LoadTextProtoMap<MapData>(EmbeddedMaps());
  c.skills = LoadTextProtoMap<Skill>(EmbeddedSkills());

  // A mob of the character's own level, so the level multiplier lands where a
  // player fighting their own tier would put it. No PDR and no boss flag: the
  // whole shipped catalog is built that way, and both would scale every row
  // alike anyway. Its HP is never read -- DPS does not depend on it.
  Mob dummy;
  dummy.set_name("Dummy");
  dummy.set_level(level);
  dummy.set_max_hp(1);
  c.mobs[kDummyMob] = dummy;

  MapData map;
  map.set_name("Dummy");
  MapData::Spawn* spawn = map.add_spawns();
  spawn->set_mob(kDummyMob);
  spawn->set_count(1);
  c.maps[kDummyMap] = map;
  return c;
}

// Brings the character up to `level` the way a player gets there: each
// advancement of `path` as it is offered, every AP on the primary stat, every
// SP on whatever it will buy.
void GrowTo(GameState& state, int level, const std::vector<Job>& path) {
  CharacterInstance& character = state.character;
  int taken = 0;
  while (character.proto().level() < level) {
    character.LevelUp();
    if (character.CanAdvanceJob() && taken < static_cast<int>(path.size())) {
      character.AdvanceJob(path[taken++]);
    }
    while (character.AllocateStat(PrimaryStatFor(character.proto().job()))) {
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      while (character.LearnSkill(entry.second)) {
      }
    }
  }
}

// Puts `key` in the character's hand, replacing whatever they started with.
// Returns false if the catalog has no such item.
bool Wield(GameState& state, const std::string& key) {
  std::map<std::string, EquipPrototype>::const_iterator it =
      state.equips.find(key);
  if (it == state.equips.end()) {
    return false;
  }
  state.character.Unequip(EQUIP_SLOT_PRIMARY_WEAPON);
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
  int primary = 0;
  int attack = 0;
  double mastery = 0.0;
  double crit_rate = 0.0;
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  double weapon_constant = 0.0;
  double skill_pct = 0.0;
  int lines = 0;
  double swing_damage = 0.0;
  double final_attack_damage = 0.0;
  int unspent_sp = 0;
  std::vector<std::pair<std::string, int>> skills;
};

// What one swing of `attack` lands on a lone mob, Final Attack included.
double SoloDamage(const AttackOption& attack) {
  if (attack.damage_per_hit.empty()) {
    return 0.0;
  }
  double damage = attack.damage_per_hit[0];
  if (!attack.final_attack_damage.empty()) {
    damage += attack.final_attack_damage[0];
  }
  return damage;
}

// What the swings came to over the horizon.
struct Sequence {
  double damage = 0.0;
  double seconds = 0.0;  // time the swings that landed actually took
  int main_attack = -1;  // index of the one swung most often
};

// Plays out the swings the fight would actually make against a lone mob, at
// the same step and by the same rule as CombatSim: the best rate available,
// with a recharging skill absent from the choice until it comes back.
//
// A closed form cannot answer this once a cooldown exists -- what the skill is
// worth depends on what gets swung while it recharges, and on how much of a
// charge is already wound up when it returns.
Sequence PlaySwings(const CombatParams& params, double horizon) {
  constexpr double kStep = 0.01;
  std::vector<double> cooldown(params.attacks.size(), 0.0);
  std::vector<int> swings(params.attacks.size(), 0);
  Sequence played;
  double phase = 0.0;
  int pick = -1;  // the swing being wound up, held until it lands
  for (double elapsed = 0.0; elapsed < horizon; elapsed += kStep) {
    for (double& left : cooldown) {
      left = std::max(0.0, left - kStep);
    }
    // Index 0 is the bare poke, which is never committed to.
    if (pick <= 0) {
      pick = -1;
      double best_rate = -1.0;
      for (int i = 0; i < static_cast<int>(params.attacks.size()); ++i) {
        const AttackOption& attack = params.attacks[i];
        if (attack.swing_seconds <= 0.0 || cooldown[i] > 0.0 ||
            attack.heal_fraction > 0.0) {
          continue;  // a cast is not one of the swings being compared
        }
        double rate = SoloDamage(attack) / attack.swing_seconds;
        if (rate > best_rate) {
          best_rate = rate;
          pick = i;
        }
      }
    }
    if (pick < 0) {
      break;
    }
    phase += kStep;
    if (phase < params.attacks[pick].swing_seconds) {
      continue;
    }
    phase -= params.attacks[pick].swing_seconds;
    played.damage += SoloDamage(params.attacks[pick]);
    played.seconds += params.attacks[pick].swing_seconds;
    ++swings[pick];
    cooldown[pick] = params.attacks[pick].cooldown_seconds;
    pick = -1;
  }
  for (int i = 0; i < static_cast<int>(swings.size()); ++i) {
    if (played.main_attack < 0 || swings[i] > swings[played.main_attack]) {
      played.main_attack = i;
    }
  }
  return played;
}

Result Measure(const Catalogs& catalogs, int level, const Build& build) {
  GameState state(catalogs.equips, catalogs.scrolls, catalogs.items,
                  catalogs.mobs, catalogs.maps, catalogs.skills,
                  GameMode::kPlay);
  GrowTo(state, level, {FirstJobFor(build.job), build.job});
  Result result;
  if (!Wield(state, build.weapon)) {
    return result;
  }
  state.current_map = kDummyMap;

  const Character& proto = state.character.proto();
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  OffenseStats bare = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(),
      TotalEquipStats(state.character, derived), state.character.weapon_type(),
      /*attack_skill=*/nullptr, /*attack_level=*/0, PassiveOffenseFor(derived));
  result.combat_power = CombatPower(bare);

  CombatParams params = ComputeCombatParams(state);
  // Long enough that a four-second cooldown lands hundreds of times, so the
  // average is not moved by where the horizon happens to cut.
  constexpr double kHorizonSeconds = 600.0;
  Sequence played = PlaySwings(params, kHorizonSeconds);
  if (played.main_attack < 0 || played.seconds <= 0.0) {
    return result;
  }
  const AttackOption* best = &params.attacks[played.main_attack];
  // Back out the pacing the game stretches everything by, so the figure is the
  // 1x one and two levels can be compared without dividing by hand.
  double speed = GameSpeedFactor(level);
  result.swing = best->name;
  result.primary = bare.primary;
  result.attack = bare.attack;
  result.mastery = bare.mastery;
  result.crit_rate = bare.crit_rate;
  result.damage_pct = bare.damage_pct;
  result.final_dmg_pct = bare.final_dmg_pct;
  result.weapon_constant = bare.weapon_constant;
  result.swing_damage = best->damage_per_hit[0];
  result.final_attack_damage =
      best->final_attack_damage.empty() ? 0.0 : best->final_attack_damage[0];
  for (const std::pair<const int32_t, int32_t>& entry : proto.sp_by_stage()) {
    result.unspent_sp += entry.second;
  }
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    int learned = state.character.skill_level(entry.second);
    if (learned > 0 &&
        state.character.HasAdvancement(entry.second.job_advancement())) {
      result.skills.push_back({entry.second.name(), learned});
      if (entry.second.name() == best->name) {
        result.skill_pct = entry.second.base().skill_pct() +
                           entry.second.per_level().skill_pct() * (learned - 1);
        result.lines = std::max(1, entry.second.lines());
      }
    }
  }
  result.swing_seconds = best->swing_seconds / speed;
  // Averaged over the swings that landed rather than the horizon, so the
  // part-charged swing the horizon cuts off costs nothing. A cooldown skill is
  // worth exactly the share of the swings it actually gets. Scaled back to 1x.
  result.dps = played.damage * speed / played.seconds;
  // Skills on their own clock land beside the swing rather than instead of it.
  for (const AttackOption& extra : params.auto_attacks) {
    if (!extra.damage_per_hit.empty() && extra.interval_seconds > 0.0) {
      result.dps += extra.damage_per_hit[0] / (extra.interval_seconds / speed);
    }
  }
  return result;
}

void Run(int level) {
  Catalogs catalogs = LoadCatalogs(level);
  // Every level-60 weapon each branch can hold, branch by branch. A Fighter
  // masters swords and axes, a Page swords and blunts, a Spearman spears and
  // polearms. The bowman branches master one weapon apiece, so they bring one
  // row each rather than two.
  const Build kBuilds[] = {
      {JOB_FIGHTER, "sparta"},
      {JOB_FIGHTER, "the_shining"},
      {JOB_PAGE, "sparta"},
      {JOB_PAGE, "the_blessing"},
      {JOB_SPEARMAN, "holy_spear"},
      {JOB_SPEARMAN, "skylar"},
      {JOB_HUNTER, "asianic_bow"},
      {JOB_CROSSBOWMAN, "golden_crow"},
      {JOB_ICE_LIGHTNING_WIZARD, "frantic_crow_staff"},
      {JOB_FIRE_POISON_WIZARD, "frantic_crow_staff"},
      {JOB_CLERIC, "frantic_crow_staff"},
  };

  std::printf(
      "Level %d, all AP in the job's primary stat, every skill maxed. DPS is "
      "one mob of the same level, at 1x speed.\n\n",
      level);
  std::printf("%-12s  %-18s  %6s  %8s  %-16s  %5s\n", "job", "weapon", "CP",
              "DPS", "swing", "sec");
  std::printf("%s\n", std::string(72, '-').c_str());
  for (const Build& build : kBuilds) {
    Result result = Measure(catalogs, level, build);
    std::string weapon = catalogs.equips.count(build.weapon) > 0
                             ? catalogs.equips.at(build.weapon).name()
                             : build.weapon;
    std::printf("%-12s  %-18s  %6d  %8.1f  %-16s  %5.2f\n",
                BranchName(build.job).c_str(), weapon.c_str(),
                result.combat_power, result.dps, result.swing.c_str(),
                result.swing_seconds);
    if (absl::GetFlag(FLAGS_detail)) {
      std::printf(
          "            %s %d  ATT %d  wc %.2f  mastery %.0f%%  crit %.0f%%  "
          "dmg %.0f%%  FD %.0f%%\n"
          "            swing %.0f (%d lines @ %.0f%%)  final attack %.0f  "
          "unspent SP %d\n            ",
          PrimaryStatName(build.job), result.primary, result.attack,
          result.weapon_constant, 100.0 * result.mastery,
          100.0 * result.crit_rate, 100.0 * result.damage_pct,
          100.0 * result.final_dmg_pct, result.swing_damage, result.lines,
          100.0 * result.skill_pct, result.final_attack_damage,
          result.unspent_sp);
      for (const std::pair<std::string, int>& skill : result.skills) {
        std::printf("%s %d  ", skill.first.c_str(), skill.second);
      }
      std::printf("\n\n");
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
