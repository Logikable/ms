#include "src/game_state.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "src/character/character.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

// The level-1 Beginner every character starts from, before any leveling.
Character MakeBaseBeginnerProto() {
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(0);
  proto.mutable_allocated_stats()->set_str(13);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(4);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return proto;
}

// How many Level-Up items the workbench opens with. Enough to carry the
// character it starts as past every gate in the unlock table, including the
// ones above kTrialLevelCap that combat can no longer climb to -- and to keep
// earning AP and SP past the cap, since LevelUp is not bounded by it.
constexpr int kTestLevelUpItems = 199;

// Where the workbench's character stands when --job says nothing: the end of
// 2nd job, holding the whole of what this game has to hand out.
constexpr JobAdvancement kTestAdvancement = JOB_ADVANCEMENT_FIGHTER;

// What the workbench puts in a job's hand. Advancing hands over the weapon for
// the level it happens at, which is the wrong end of the job: the workbench
// starts its character at the TOP of an advancement, and a level 60 Fighter
// swinging the axe they were given at 30 is not the character anyone came to
// test. So every job names the best weapon of its own type that its starting
// level can wear -- level 30 for a 1st job, level 60 for a 2nd.
//
// The one exception is ammunition: the star ladder's next rung after level 50
// is out of a level 60 character's reach, so an Assassin tops out there.
//
// Where a job masters two weapons the better one is named, on the figures in
// //analysis:weapon_sim. A Rogue gets three, as advancing gives them, because
// which of the dagger and the claw is held decides which attack they can
// swing.
std::vector<std::string> WorkbenchWeaponsFor(Job job) {
  switch (job) {
    // The 1st jobs, at level 30.
    case JOB_SWORDMAN:
      return {"gladius"};
    case JOB_ARCHER:
      return {"ryden"};
    case JOB_MAGICIAN:
      return {"circle_winded_staff"};
    case JOB_ROGUE:
      return {"kumbi_throwing_stars", "reef_claw", "steel_guards"};
    // The 2nd jobs, at level 60.
    case JOB_FIGHTER:
      return {"the_shining"};
    case JOB_PAGE:
      return {"the_blessing"};
    case JOB_SPEARMAN:
      return {"holy_spear"};
    case JOB_HUNTER:
      return {"asianic_bow"};
    case JOB_CROSSBOWMAN:
      return {"golden_crow"};
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_FIRE_POISON_WIZARD:
    case JOB_CLERIC:
      return {"frantic_crow_staff"};
    case JOB_ASSASSIN:
      return {"steely_throwing_knives", "dark_gigantic"};
    case JOB_BANDIT:
      return {"deadly_fin"};
    default:
      return StarterEquipsFor(job);
  }
}

// Puts a copy of the named equip in the bag, or does nothing if the catalog
// has no such entry. Lets a GameState be built for a test without the game's
// data files behind it.
void GiveEquip(GameState& state, const std::string& name) {
  std::map<std::string, EquipPrototype>::const_iterator it =
      state.equips.find(name);
  if (it == state.equips.end()) {
    return;
  }
  state.character.PickUp(std::make_unique<EquipInstance>(it->second));
}

// Passed as `unspent_stage` to spend every point the climb earns.
constexpr int kSpendEveryStage = 0;

// Climbs to `level` the way a player gets there, taking each advancement in
// `path` as it is offered, so nothing the workbench holds is out of reach:
// thirty hours of grinding, handed over.
//
// The AP is always spent, into the primary stat, because a character with a
// hundred points in the pool is a hundred keypresses from the screen the
// tester came for. The SP is spent up to `unspent_stage`, whose book and every
// one above it are left alone: the books already behind the character are
// noise, and the one they are standing in is usually the question. The
// Level-Up items are what is left for exercising either screen.
void GrowTo(GameState& state, int level, const std::vector<Job>& path,
            int unspent_stage) {
  CharacterInstance& character = state.character;
  int taken = 0;
  while (character.proto().level() < level) {
    character.LevelUp();
    if (character.CanAdvanceJob() && taken < static_cast<int>(path.size())) {
      character.AdvanceJob(path[taken++]);
    }
    // After the advancement, not before: it puts every allocated point back in
    // the pool and re-spends it for the new job.
    while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      int stage = StageForAdvancement(entry.second.job_advancement());
      if (unspent_stage != kSpendEveryStage && stage >= unspent_stage) {
        continue;
      }
      while (character.LearnSkill(entry.second)) {
      }
    }
  }
}

// Climbs to the top of `advancement`: the job it names, at the last level
// before the next advancement would be offered, having taken every earlier
// advancement on the way to it.
void GrowToJob(GameState& state, JobAdvancement advancement,
               int unspent_stage) {
  Job job = JobForAdvancement(advancement);
  int stage = StageForAdvancement(advancement);
  std::vector<Job> path;
  for (int i = 1; i <= stage; ++i) {
    path.push_back(JobForAdvancement(AdvancementForJobStage(job, i)));
  }
  GrowTo(state, NextAdvancementLevel(stage), path, unspent_stage);
  // The job's own gear, worn rather than carried, since there is no
  // advancement moment here to put it on at. Each piece is equipped from the
  // row it lands on, so a Rogue's three reach three slots -- and whatever a
  // later one displaces goes back to the bag for the tester to swap in.
  for (const std::string& name : WorkbenchWeaponsFor(job)) {
    int row = static_cast<int>(state.character.inventory().size());
    GiveEquip(state, name);
    if (static_cast<int>(state.character.inventory().size()) > row) {
      state.character.Equip(row);
    }
  }
}

// A player starts armed and with nothing else: the Sword is worn rather than
// carried, so the bag really is empty.
void SeedPlay(GameState& state) {
  GiveEquip(state, "sword");
  if (!state.character.inventory().empty()) {
    state.character.Equip(0);
  }
  state.current_map = kHomeMap;
}

// What the workbench multiplies combat EXP by. High enough that the early
// levels go by while the tester watches, which is what makes the level-gated
// features reachable without farming for them.
constexpr int kTestExpMultiplier = 5;

// The awkward items, for the upgrade screens: a fully scrolled weapon at high
// star force, a trace to recover, and the base item recovering it consumes.
void GiveUpgradeItems(GameState& state) {
  std::map<std::string, EquipPrototype>::const_iterator fafnir =
      state.equips.find("fafnir_mistilteinn");
  if (fafnir == state.equips.end()) {
    return;
  }
  Equip scrolled;
  scrolled.set_equip_name("Fafnir Mistilteinn");
  scrolled.set_remaining_upgrade_slots(0);
  scrolled.set_scroll_successes(8);
  scrolled.set_stars(20);
  scrolled.mutable_scroll_stats()->set_attack(40);
  scrolled.mutable_scroll_stats()->set_str(16);
  state.character.PickUp(
      std::make_unique<EquipInstance>(fafnir->second, scrolled));

  // The same at 22 stars but destroyed: the source trace for recovery.
  Equip trace = scrolled;
  trace.set_stars(22);
  state.character.PickUp(std::make_unique<EquipTrace>(fafnir->second, trace));

  // Fresh Fafnir -- the base item that recovering the trace consumes.
  state.character.PickUp(std::make_unique<EquipInstance>(fafnir->second));
}

// The workbench. Everything here exists to reach a screen without playing up
// to it. `chosen` is --job: unset takes kTestAdvancement and buys its whole
// book, so the default workbench is finished rather than half-built.
void SeedTest(GameState& state, JobAdvancement chosen) {
  state.exp_multiplier = kTestExpMultiplier;

  // Enough to buy anything the shop stocks, several times over, so the buying
  // screens can be exercised without grinding for the meso first. A billion
  // rather than a million because spell traces are bought by the thousand at
  // 5,000 each, and the scroll screen is unusable without a pile of them.
  state.character.AddMeso(1000000000);

  // The ladder in a bag. Skipped when the catalog has no such item, as every
  // other piece of seeding is.
  std::map<std::string, ItemPrototype>::const_iterator level_up =
      state.items.find("level_up");
  if (level_up != state.items.end()) {
    state.character.AddStackable(level_up->second, kTestLevelUpItems);
  }

  bool chose_job = chosen != JOB_ADVANCEMENT_UNSPECIFIED;
  if (!chose_job) {
    // The rungs below the default job's weapon, to swap between on the equip
    // screens. A job named by --job brings only its own. The first is worn
    // straight away: it is what the character holds until the climb below
    // hands them their job's weapon, and a catalog without that weapon -- a
    // test's, say -- leaves them holding this rather than nothing.
    GiveEquip(state, "sword");
    if (!state.character.inventory().empty()) {
      state.character.Equip(0);
    }
    GiveEquip(state, "long_sword");
    GiveEquip(state, "machete");
  }
  GiveUpgradeItems(state);
  // Only the book the chosen job is standing in is left unspent. The ones
  // behind them are not what --job was asked for, and leaving those unbought
  // would put the tester through two allocation screens to reach one.
  GrowToJob(state, chose_job ? chosen : kTestAdvancement,
            chose_job ? StageForAdvancement(chosen) : kSpendEveryStage);

  // The weakest hunting ground there is; the tester picks anywhere else from
  // the map select.
  state.current_map = "right_around_lith_harbor";
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg,
                     std::map<std::string, ItemPrototype> items_arg,
                     std::map<std::string, Mob> mobs_arg,
                     std::map<std::string, MapData> maps_arg,
                     std::map<std::string, Skill> skills_arg, GameMode mode,
                     JobAdvancement test_job)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      skills(std::move(skills_arg)),
      rng(std::random_device{}()),
      // Both modes start at level 1. The workbench used to start at 10,
      // standing at its first advancement, but the game reveals itself a
      // level at a time now and starting part way up would skip the half of
      // it worth watching. SeedTest hands it Level-Up items instead, so a
      // tester climbs the ladder on demand rather than beginning above it.
      character(rng, MakeBaseBeginnerProto()),
      created_unix_seconds(static_cast<int64_t>(std::time(nullptr))) {
  if (mode == GameMode::kTest) {
    SeedTest(*this, test_job);
  } else {
    SeedPlay(*this);
  }
}

}  // namespace ms
