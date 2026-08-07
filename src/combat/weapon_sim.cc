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
 *   bazelisk run //src/combat:weapon_sim
 *   bazelisk run //src/combat:weapon_sim -- --level=40
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

// The three branches by name. Spelled out here rather than borrowed from the
// frontend's JobName, which a combat tool has no business reaching into.
std::string BranchName(Job job) {
  switch (job) {
    case JOB_FIGHTER:
      return "Fighter";
    case JOB_PAGE:
      return "Page";
    case JOB_SPEARMAN:
      return "Spearman";
    default:
      return "?";
  }
}

// Which stat this job's damage is built on, so the sweep spends AP the way a
// player would rather than leaving it in the pool.
StatField PrimaryStatFor(Job job) {
  switch (job) {
    case JOB_ARCHER:
      return STAT_FIELD_DEX;
    case JOB_MAGICIAN:
      return STAT_FIELD_INT;
    case JOB_ROGUE:
      return STAT_FIELD_LUK;
    default:
      return STAT_FIELD_STR;
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
  int str = 0;
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

// The attack the fight would swing at a single mob: the one landing the most
// per second, reach being worth nothing when there is only one thing to reach.
// The same rule CombatSim::BestAttack uses, Final Attack included.
const AttackOption* BestSingleTarget(const CombatParams& params) {
  const AttackOption* best = nullptr;
  double best_rate = -1.0;
  for (const AttackOption& attack : params.attacks) {
    if (attack.damage_per_hit.empty() || attack.swing_seconds <= 0.0) {
      continue;
    }
    double damage = attack.damage_per_hit[0];
    if (!attack.final_attack_damage.empty()) {
      damage += attack.final_attack_damage[0];
    }
    double rate = damage / attack.swing_seconds;
    if (rate > best_rate) {
      best_rate = rate;
      best = &attack;
    }
  }
  return best;
}

Result Measure(const Catalogs& catalogs, int level, const Build& build) {
  GameState state(catalogs.equips, catalogs.scrolls, catalogs.items,
                  catalogs.mobs, catalogs.maps, catalogs.skills,
                  GameMode::kPlay);
  GrowTo(state, level, {JOB_SWORDMAN, build.job});
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
  const AttackOption* best = BestSingleTarget(params);
  if (best == nullptr) {
    return result;
  }
  // Back out the pacing the game stretches everything by, so the figure is the
  // 1x one and two levels can be compared without dividing by hand.
  double speed = GameSpeedFactor(level);
  result.swing = best->name;
  result.str = bare.primary;
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
  double per_swing = best->damage_per_hit[0];
  if (!best->final_attack_damage.empty()) {
    per_swing += best->final_attack_damage[0];
  }
  result.dps = per_swing / result.swing_seconds;
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
  // polearms.
  const Build kBuilds[] = {
      {JOB_FIGHTER, "sparta"},      {JOB_FIGHTER, "the_shining"},
      {JOB_PAGE, "sparta"},         {JOB_PAGE, "the_blessing"},
      {JOB_SPEARMAN, "holy_spear"}, {JOB_SPEARMAN, "skylar"},
  };

  std::printf(
      "Level %d, all AP in STR, every skill maxed. DPS is one mob of "
      "the same level, at 1x speed.\n\n",
      level);
  std::printf("%-10s  %-18s  %6s  %8s  %-16s  %5s\n", "job", "weapon", "CP",
              "DPS", "swing", "sec");
  std::printf("%s\n", std::string(72, '-').c_str());
  for (const Build& build : kBuilds) {
    Result result = Measure(catalogs, level, build);
    std::string weapon = catalogs.equips.count(build.weapon) > 0
                             ? catalogs.equips.at(build.weapon).name()
                             : build.weapon;
    std::printf("%-10s  %-18s  %6d  %8.1f  %-16s  %5.2f\n",
                BranchName(build.job).c_str(), weapon.c_str(),
                result.combat_power, result.dps, result.swing.c_str(),
                result.swing_seconds);
    if (absl::GetFlag(FLAGS_detail)) {
      std::printf(
          "            STR %d  ATT %d  wc %.2f  mastery %.0f%%  crit %.0f%%  "
          "dmg %.0f%%  FD %.0f%%\n"
          "            swing %.0f (%d lines @ %.0f%%)  final attack %.0f  "
          "unspent SP %d\n            ",
          result.str, result.attack, result.weapon_constant,
          100.0 * result.mastery, 100.0 * result.crit_rate,
          100.0 * result.damage_pct, 100.0 * result.final_dmg_pct,
          result.swing_damage, result.lines, 100.0 * result.skill_pct,
          result.final_attack_damage, result.unspent_sp);
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
