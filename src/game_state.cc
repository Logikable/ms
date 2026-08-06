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

// Where the workbench's character stands: the end of 2nd job, holding the
// whole of what this game has to hand out.
constexpr int kTestLevel = 60;
constexpr Job kTestJobPath[] = {JOB_SWORDMAN, JOB_SPEARMAN};

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

// Climbs the workbench's character to the end of 2nd job the way a player
// gets there, so nothing it holds is out of a player's reach -- it is thirty
// hours of grinding, handed over.
//
// Everything earned on the way is spent: AP into the job's primary stat, SP
// into whatever it will buy. A workbench hands over finished states, and a
// level 60 with an unspent pool would still need a hundred and fifty
// keypresses before any of 2nd job was on screen. The Level-Up items are what
// is left for exercising the two allocation screens -- LevelUp is not bounded
// by the trial cap, so there is always more AP and SP to be had.
void GrowToSecondJob(GameState& state) {
  CharacterInstance& character = state.character;
  int taken = 0;
  while (character.proto().level() < kTestLevel) {
    character.LevelUp();
    if (character.CanAdvanceJob() &&
        taken <
            static_cast<int>(sizeof(kTestJobPath) / sizeof(kTestJobPath[0]))) {
      character.AdvanceJob(kTestJobPath[taken++]);
    }
    // After the advancement, not before: it puts every allocated point back in
    // the pool and re-spends it for the new job.
    while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      while (character.LearnSkill(entry.second)) {
      }
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

// The workbench. Everything here exists to reach a screen without playing up
// to it, so the items are the awkward ones: a fully scrolled weapon at high
// star force, a trace to recover, and the base item recovering it consumes.
// What the workbench multiplies combat EXP by. High enough that the early
// levels go by while the tester watches, which is what makes the level-gated
// features reachable without farming for them.
constexpr int kTestExpMultiplier = 5;

void SeedTest(GameState& state) {
  state.exp_multiplier = kTestExpMultiplier;

  // Enough to buy anything the shop stocks, several times over, so the buying
  // screens can be exercised without grinding for the meso first.
  state.character.AddMeso(1000000);

  // The ladder in a bag. Skipped when the catalog has no such item, as every
  // other piece of seeding is.
  std::map<std::string, ItemPrototype>::const_iterator level_up =
      state.items.find("level_up");
  if (level_up != state.items.end()) {
    state.character.AddStackable(level_up->second, kTestLevelUpItems);
  }

  // Worn, not carried, and before anything else goes in the bag. A level 1
  // character has neither the equipped panel nor the bag yet, so a workbench
  // that only put a weapon in the bag could never swing one -- and without a
  // weapon there is no combat, no EXP, and no way off level 1 at all.
  GiveEquip(state, "sword");
  if (!state.character.inventory().empty()) {
    state.character.Equip(0);
  }

  GiveEquip(state, "long_sword");
  GiveEquip(state, "machete");

  std::map<std::string, EquipPrototype>::const_iterator fafnir =
      state.equips.find("fafnir_mistilteinn");
  if (fafnir != state.equips.end()) {
    // Fully scrolled Fafnir at 20 stars -- high star force on a finished
    // endgame weapon.
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

  // Last, so the advancement's stat reset lands on a character already
  // holding their gear rather than tearing through it afterwards.
  GrowToSecondJob(state);

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
                     std::map<std::string, Skill> skills_arg, GameMode mode)
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
    SeedTest(*this);
  } else {
    SeedPlay(*this);
  }
}

}  // namespace ms
