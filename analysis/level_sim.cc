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
 *   - the best weapon of the branch's own type that the shop will sell them
 *     and they can pay for, bought the level it comes within reach;
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
#include "analysis/parallel.h"
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

namespace ms {
namespace {

// The levels the table reports a running total at.
constexpr int kMilestones[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
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
    case JOB_ASSASSIN:
    case JOB_BANDIT:
      return STAT_FIELD_LUK;
    default:
      return STAT_FIELD_STR;
  }
}

// What the player goes shopping for. One type per job rather than a search
// over everything on the shelf: within a type the ladder only climbs, and
// which type a branch ends on is already settled -- see weapon_sim, where the
// Fighter's axe, the Page's blunt and the Spearman's spear each top their
// branch.
EquipType PreferredWeapon(Job job) {
  switch (job) {
    case JOB_FIGHTER:
      return EQUIP_TYPE_TWO_HANDED_AXE;
    case JOB_PAGE:
      return EQUIP_TYPE_TWO_HANDED_BLUNT;
    case JOB_SPEARMAN:
      return EQUIP_TYPE_SPEAR;
    case JOB_MAGICIAN:
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_FIRE_POISON_WIZARD:
    case JOB_CLERIC:
      return EQUIP_TYPE_STAFF;
    case JOB_ROGUE:
    case JOB_ASSASSIN:
      return EQUIP_TYPE_CLAW;
    case JOB_BANDIT:
      return EQUIP_TYPE_DAGGER;
    case JOB_ARCHER:
    case JOB_HUNTER:
      return EQUIP_TYPE_BOW;
    case JOB_CROSSBOWMAN:
      return EQUIP_TYPE_CROSSBOW;
    default:
      return EQUIP_TYPE_ONE_HANDED_SWORD;
  }
}

// What a claw draws from. A claw carries almost no attack of its own -- an
// Assassin's damage is mostly in the ammunition -- so a claw user shops for
// two things. Unspecified for everyone else, who shop for one.
EquipType PreferredAmmo(Job job) {
  return PreferredWeapon(job) == EQUIP_TYPE_CLAW ? EQUIP_TYPE_THROWING_STAR
                                                 : EQUIP_TYPE_UNSPECIFIED;
}

// The required level of what is worn in `slot`, which is how one piece is
// ranked against another here: the shop's ladder is ordered by it, and the
// tiers are 10 levels apart.
int HeldTier(const CharacterInstance& character, EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      character.equipped().find(slot);
  if (it == character.equipped().end()) {
    return 0;
  }
  return it->second.prototype().required_level();
}

std::string HeldWeaponName(const CharacterInstance& character) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      character.equipped().find(EQUIP_SLOT_PRIMARY_WEAPON);
  return it == character.equipped().end() ? "-" : it->second.name();
}

// Puts the bag's copy of `name` on, if the character can wear it. Found by
// name rather than by index because equipping shuffles the bag: what is
// displaced goes back into it.
void EquipByName(CharacterInstance& character, const std::string& name) {
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipInstance* item = character.inventory().equip_instance(i);
    if (item != nullptr && item->name() == name &&
        character.CanEquip(item->prototype())) {
      character.Equip(i);
      return;
    }
  }
}

// Spends everything the last level handed over.
void SpendPoints(CharacterInstance& character) {
  while (character.AllocateStat(PrimaryStatFor(character.proto().job()))) {
  }
}

void LearnEverything(GameState& state) {
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    while (state.character.LearnSkill(entry.second)) {
    }
  }
}

// Empties the Etc tab into the meso balance. Nothing in the game uses an Etc
// item for anything else, so a player who is saving for a weapon sells the
// lot.
void SellDrops(CharacterInstance& character) {
  while (!character.stackables(ITEM_CATEGORY_ETC).empty()) {
    int count = character.stackables(ITEM_CATEGORY_ETC)[0].count();
    if (character.SellStackable(ITEM_CATEGORY_ETC, 0, count) <= 0) {
      return;  // unsellable, and every later stack waits behind it
    }
  }
}

// Buys and wears the best piece of `want` on the shelf, if it beats what is
// already in that slot. No-op when nothing new is affordable.
void BuyBest(GameState& state, EquipType want) {
  CharacterInstance& character = state.character;
  const EquipPrototype* best = nullptr;
  for (const std::string& key : ShopWeaponStock(state.equips)) {
    const EquipPrototype& proto = state.equips.at(key);
    if (proto.equip_type() != want || !character.CanEquip(proto) ||
        proto.shop_price() > character.meso()) {
      continue;
    }
    if (best == nullptr || proto.required_level() > best->required_level()) {
      best = &proto;
    }
  }
  if (best == nullptr ||
      best->required_level() <= HeldTier(character, best->equip_slot())) {
    return;
  }
  if (character.Buy(*best, 1)) {
    EquipByName(character, best->name());
  }
}

// Everything the player goes shopping for: the weapon of their branch's own
// type, and the ammunition it draws from if it needs any.
void GoShopping(GameState& state) {
  if (!Unlocked(Feature::kShop, state.character)) {
    return;
  }
  Job job = state.character.proto().job();
  BuyBest(state, PreferredWeapon(job));
  EquipType ammo = PreferredAmmo(job);
  if (ammo != EQUIP_TYPE_UNSPECIFIED) {
    BuyBest(state, ammo);
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
  GoShopping(state);
  PickMap(state, maps, beats, step);
}

// One level of one branch's climb, for --detail.
struct Stint {
  int level = 0;
  double seconds = 0.0;
  std::string map;
  std::string weapon;
};

// What one branch's climb came to.
struct Climb {
  // Playtime in seconds at each milestone, or -1 for one never reached.
  double milestone_seconds[kNumMilestones];
  std::vector<Stint> stints;
};

Climb Play(const Catalogs& catalogs, Job branch,
           const std::vector<std::string>& maps) {
  double step = absl::GetFlag(FLAGS_step);
  int beats = absl::GetFlag(FLAGS_probe_beats);
  double give_up = absl::GetFlag(FLAGS_give_up_hours) * 3600.0;

  GameState state(catalogs.equips, catalogs.scrolls, catalogs.items,
                  catalogs.mobs, catalogs.maps, catalogs.skills,
                  GameMode::kPlay);
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

void Run() {
  Catalogs catalogs = LoadCatalogs();
  std::vector<std::string> maps = HuntingGrounds(catalogs);
  const Job kBranches[] = {
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
  };

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
  int count = static_cast<int>(sizeof(kBranches) / sizeof(kBranches[0]));
  std::vector<Climb> climbs(count);
  ParallelFor(count,
              [&](int i) { climbs[i] = Play(catalogs, kBranches[i], maps); });

  for (int i = 0; i < count; ++i) {
    std::printf("%-13s", BranchName(kBranches[i]).c_str());
    for (int m = 0; m < kNumMilestones; ++m) {
      std::printf("  %8s", Clock(climbs[i].milestone_seconds[m]).c_str());
    }
    std::printf("\n");
    if (absl::GetFlag(FLAGS_detail)) {
      PrintDetail(catalogs, climbs[i]);
    }
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
