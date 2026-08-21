/* What each 2nd-job branch hits for with each weapon it can hold: combat power
 * and single-target DPS, for a maxed character at a chosen level.
 *
 * The character is grown the way a player gets there -- every AP on the primary
 * stat, every SP on whatever it will buy -- so what is compared is a finished
 * build rather than a stat line typed in by hand. By default DPS is measured
 * against a lone mob of the character's own level on an otherwise empty map, so
 * it is the character and the weapon being compared and nothing else: no crowd
 * for a wide skill to take advantage of, no spawn cap to hide a difference
 * behind. --enemies and --boss ask the other two questions, and the header
 * says which of the three was asked.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers to
 * look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:weapon_sim
 *   bazelisk run //analysis:weapon_sim -- --level=40
 *   bazelisk run //analysis:weapon_sim -- --level=140 --enemies=8
 *   bazelisk run //analysis:weapon_sim -- --level=140 --boss --boss_pdr=40
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
#include "analysis/sim_gear.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/damage.h"
#include "src/combat/encounter.h"
#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/projectile.h"
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
ABSL_FLAG(bool, upgraded, false,
          "Wear everything at its ceiling: every upgrade slot filled with the "
          "spell trace that swings hardest on it, and stars up to the item's "
          "own maximum. The default is gear straight off the shelf.");
ABSL_FLAG(int, boss_pdr, 0,
          "Percent of the mob's physical defence, which every Ignore DEF "
          "lever in the game is measured against. 0 is the shipped catalog, "
          "where all of them cancel nothing.");

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

// One row of the table: which branch, holding what. Whatever the weapon draws
// from is bought with it, since an empty projectile slot reads as a broken
// build -- an Assassin's damage is mostly in the ammunition.
struct Build {
  Job job = JOB_UNSPECIFIED;
  EquipType weapon = EQUIP_TYPE_UNSPECIFIED;
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
    case JOB_MARKSMAN:
      return "Marksman";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return "I/L Arch Mage";
    case JOB_FIRE_POISON_ARCH_MAGE:
      return "F/P Arch Mage";
    case JOB_NIGHT_LORD:
      return "Night Lord";
    case JOB_SHADOWER:
      return "Shadower";
    case JOB_BISHOP:
      return "Bishop";
    default:
      return "?";
  }
}

// The advancements a branch is reached through, in order, so the sweep climbs
// the same path a player does and collects each book's skills on the way. Read
// off the game's own stage table rather than listed, so a new job joins by
// existing -- which is how the Berserker's three-step path came for free.
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

EquipPrototype Charm(Job job, int stat, int attack, int boss_pct) {
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
    while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      while (character.LearnSkill(entry.second)) {
      }
    }
  }
}

// Puts `key` on the character, in whichever slot its prototype names, dropping
// whatever was already there. Returns false if the catalog has no such item.
// The best weapon of `type` a character at `level` can wear. Named by type
// rather than by catalog key so the table below never has to be edited when a
// tier is added -- which is the whole reason a build says "claw" and not
// "dark_gigantic".
std::string BestOfType(const Catalogs& catalogs, EquipType type, int level) {
  std::string best;
  int best_level = -1;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       catalogs.equips) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() != type || proto.required_level() > level ||
        proto.required_level() <= best_level) {
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
  int primary = 0;
  int attack = 0;
  double mastery = 0.0;
  double crit_rate = 0.0;
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
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
  std::vector<std::pair<std::string, int>> skills;
  // Share of the run's damage each attack took, largest first, with everything
  // on a clock of its own gathered into one row. What it is for is deciding
  // whether a skill in the book is earning its points.
  std::vector<std::pair<std::string, double>> shares;
};

// Strikes another skill hands `swing`, summed over the book the character
// holds. Greater Vessel of Light gives Blast an eleventh, and a line that
// printed ten beside eleven strikes' worth of damage would not add up.
int BoostedLines(const GameState& state, const std::string& swing) {
  int lines = 0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (state.character.skill_level(entry.second) <= 0 ||
        !state.character.HasAdvancement(entry.second.job_advancement())) {
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
    if (learned <= 0 ||
        !state.character.HasAdvancement(entry.second.job_advancement())) {
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
    std::map<std::string, double>::const_iterator boost =
        derived.skill_pct_bonus.find(swing);
    if (boost != derived.skill_pct_bonus.end()) {
      result->skill_pct += boost->second;
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

// What the summons and the triggered attacks add, per second at 1x. They land
// beside the swing rather than instead of it, so none of this competes with
// the swing damage for the clock.
double OffClockDps(const CombatParams& params, const Sequence& played,
                   const AttackOption& best, double speed, double swing_seconds,
                   int enemies) {
  double dps = OffClockRate(params, played, speed, enemies);
  // How often a triggered attack goes off depends on the swing feeding it: a
  // rapid attack counting a seventh apiece is worth no more of these than a
  // slow one counting a whole attack.
  for (const AttackOption& extra : params.triggered_attacks) {
    if (extra.damage_per_hit.empty() || extra.attacks_per_cast <= 0) {
      continue;
    }
    dps += CrowdDamage(extra, enemies) * best.count_weight /
           (swing_seconds * extra.attacks_per_cast);
  }
  return dps;
}

// Where the run's damage went, as a share apiece. The swings are counted over
// the run and everything on its own clock is one row, since a summon competes
// with nothing for the clock and its share is simply what it added.
void RecordShares(const CombatParams& params, const Sequence& played,
                  double off_clock_dps, double speed, Result* result) {
  double off_clock = off_clock_dps * played.seconds / speed;
  double total = played.damage + off_clock;
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
  if (off_clock > 0.0) {
    result->shares.push_back({"(own clock)", off_clock / total});
  }
  std::sort(result->shares.begin(), result->shares.end(),
            [](const std::pair<std::string, double>& a,
               const std::pair<std::string, double>& b) {
              return a.second > b.second;
            });
}

Result Measure(const Catalogs& catalogs, int level, const Build& build) {
  GameState state(catalogs.equips, catalogs.scrolls, catalogs.items,
                  catalogs.mobs, catalogs.maps, catalogs.skills,
                  GameMode::kPlay, TestOptions{}, kSimSeed);
  GrowTo(state, level, PathTo(build.job));
  Result result;
  if (!Wear(state, BestOfType(catalogs, build.weapon, level))) {
    return result;
  }
  EquipType ammo = AmmoFor(build.weapon);
  if (ammo != EQUIP_TYPE_UNSPECIFIED &&
      !Wear(state, BestOfType(catalogs, ammo, level))) {
    return result;
  }
  state.current_map = kDummyMap;
  if (absl::GetFlag(FLAGS_upgraded)) {
    FullyUpgrade(state);
  }
  int bonus_stat = absl::GetFlag(FLAGS_bonus_stat);
  int bonus_attack = absl::GetFlag(FLAGS_bonus_attack);
  int bonus_boss = absl::GetFlag(FLAGS_bonus_boss_pct);
  if (bonus_stat > 0 || bonus_attack > 0 || bonus_boss > 0) {
    state.equips[kCharm] =
        Charm(build.job, bonus_stat, bonus_attack, bonus_boss);
    Wear(state, kCharm);
  }

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
  int enemies = absl::GetFlag(FLAGS_enemies);
  Sequence played = PlaySwings(params, kHorizonSeconds, enemies);
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
  RecordBook(state, derived, best->name, &result);
  result.mirror_pct = derived.mirror_line_pct;
  result.swing_seconds = best->swing_seconds / speed;
  // Averaged over the swings that landed rather than the horizon, so the
  // part-charged swing the horizon cuts off costs nothing. A cooldown skill is
  // worth exactly the share of the swings it actually gets. Scaled back to 1x.
  result.dps = played.damage * speed / played.seconds;
  double off_clock =
      OffClockDps(params, played, *best, speed, result.swing_seconds, enemies);
  result.dps += off_clock;
  RecordShares(params, played, off_clock, speed, &result);
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
      "dmg %.0f%%  FD %.0f%%\n"
      "            swing %.0f (%d lines @ %.0f%%%s)  final attack %.0f  "
      "unspent SP %d\n            ",
      PrimaryStatName(build.job), result.primary, result.attack,
      result.weapon_constant, 100.0 * result.mastery, 100.0 * result.crit_rate,
      100.0 * result.damage_pct, 100.0 * result.final_dmg_pct,
      result.swing_damage, result.lines, 100.0 * result.skill_pct, shadow_buf,
      result.final_attack_damage, result.unspent_sp);
  for (const std::pair<std::string, int>& skill : result.skills) {
    std::printf("%s %d  ", skill.first.c_str(), skill.second);
  }
  std::printf("\n            ");
  for (const std::pair<std::string, double>& share : result.shares) {
    std::printf("%s %.1f%%  ", share.first.c_str(), 100.0 * share.second);
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

void Run(int level) {
  Catalogs catalogs = LoadCatalogs(level);

  // The header names the fight the flags asked for. A table read a week later
  // is worth nothing if it does not say what was being hit.
  char crowd[64];
  if (absl::GetFlag(FLAGS_boss)) {
    std::snprintf(crowd, sizeof(crowd), "a boss holding %d%% PDR",
                  absl::GetFlag(FLAGS_boss_pdr));
  } else {
    int enemies = absl::GetFlag(FLAGS_enemies);
    std::snprintf(crowd, sizeof(crowd), "%d mob%s of the same level", enemies,
                  enemies == 1 ? "" : "s");
  }
  std::printf(
      "Level %d, all AP in the job's primary stat, every skill maxed. DPS is "
      "against %s, at 1x speed.\n\n",
      level, crowd);
  std::printf("%-13s  %-22s  %7s  %12s  %-18s  %5s\n", "job", "weapon", "CP",
              "DPS", "swing", "sec");
  std::printf("%s\n", std::string(85, '-').c_str());
  for (const Build& build : kBuilds) {
    Result result = Measure(catalogs, level, build);
    std::string key = BestOfType(catalogs, build.weapon, level);
    std::string weapon = catalogs.equips.count(key) > 0
                             ? catalogs.equips.at(key).name()
                             : "(none this level can wear)";
    // A charm big enough to hold two jobs level carries the character's CP
    // past what an int holds. The figure is the game's own, so it is dropped
    // here rather than widened there.
    std::string power =
        result.combat_power > 0 ? std::to_string(result.combat_power) : "-";
    std::printf("%-13s  %-22s  %7s  %12.1f  %-18s  %5.2f\n",
                BranchName(build.job).c_str(), weapon.c_str(), power.c_str(),
                result.dps, result.swing.c_str(), result.swing_seconds);
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
