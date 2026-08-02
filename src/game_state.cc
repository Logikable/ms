#include "src/game_state.h"

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

// The level kTest starts at. Level 10 is where the first job advancement
// opens, so the workbench begins at the choice -- which job the character
// becomes is the tester's to make, not this file's.
constexpr int kTestLevel = 10;

// A Beginner standing at its first advancement. Built by running the real
// leveling mechanics rather than hardcoding totals, so AP and HP stay
// consistent with the level if it is changed here.
Character MakeTestCharacterProto() {
  // LevelUp doesn't consume randomness; a local rng keeps this independent of
  // GameState's member rng and its construction order.
  std::mt19937 rng(0);
  CharacterInstance character(rng, MakeBaseBeginnerProto());
  while (character.proto().level() < kTestLevel) {
    character.LevelUp();
  }
  return character.proto();
}

Character MakeCharacterProtoFor(GameMode mode) {
  if (mode == GameMode::kTest) {
    return MakeTestCharacterProto();
  }
  return MakeBaseBeginnerProto();
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

// A player starts armed and with nothing else: the Sword is worn rather than
// carried, so the bag really is empty.
void SeedPlay(GameState& state) {
  GiveEquip(state, "sword");
  if (!state.character.inventory().empty()) {
    state.character.Equip(0);
  }
  state.current_map = "maple_island";
}

// The workbench. Everything here exists to reach a screen without playing up
// to it, so the items are the awkward ones: a fully scrolled weapon at high
// star force, a trace to recover, and the base item recovering it consumes.
void SeedTest(GameState& state) {
  // Enough to buy anything the shop stocks, several times over, so the buying
  // screens can be exercised without grinding for the meso first.
  state.character.AddMeso(1000000);

  GiveEquip(state, "sword");
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
      character(rng, MakeCharacterProtoFor(mode)) {
  if (mode == GameMode::kTest) {
    SeedTest(*this);
  } else {
    SeedPlay(*this);
  }
}

}  // namespace ms
