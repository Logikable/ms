/* Drives the real combat engine to the level cap and reports the meso a
 * player holds on reaching each milestone level, selling all Etc loot as it
 * drops. Scratch analysis tool, not part of the game.
 */
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "src/character/character.h"
#include "src/combat/combat.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
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
#include "tools/cpp/runfiles/runfiles.h"

namespace {

// Fixes the random stream every run of this sim draws from. Rewards are
// rolled, so an unseeded run would print a table that moved a little each
// time and hide a real change under the noise.
constexpr unsigned int kSimSeed = 20260813;

using bazel::tools::cpp::runfiles::Runfiles;

// Spawn-count-weighted mean mob level, floored -- the same number the map list
// sorts and labels maps by (map_select_panel.cc).
int WeightedLevel(const ms::GameState& state, const ms::MapData& map) {
  int levels = 0;
  int spawned = 0;
  for (const ms::MapData::Spawn& spawn : map.spawns()) {
    std::map<std::string, ms::Mob>::const_iterator it =
        state.mobs.find(spawn.mob());
    if (it == state.mobs.end()) {
      continue;
    }
    levels += it->second.level() * spawn.count();
    spawned += spawn.count();
  }
  return spawned == 0 ? 0 : levels / spawned;
}

// The highest-level map the player has out-levelled: what the map list walks
// them onto as they climb.
std::string AppropriateMap(const ms::GameState& state, int player_level) {
  std::string best;
  int best_level = -1;
  for (const std::pair<const std::string, ms::MapData>& entry : state.maps) {
    int level = WeightedLevel(state, entry.second);
    if (level <= player_level && level > best_level) {
      best_level = level;
      best = entry.first;
    }
  }
  return best;
}

// Sells every Etc stack the character is holding.
void SellAllEtc(ms::CharacterInstance& character) {
  while (!character.stackables(ms::ITEM_CATEGORY_ETC).empty()) {
    const ms::StackableItem& stack =
        character.stackables(ms::ITEM_CATEGORY_ETC)[0];
    if (character.SellStackable(ms::ITEM_CATEGORY_ETC, 0, stack.count()) == 0) {
      break;  // unsellable stack would loop forever
    }
  }
}

// Plays the character the way a player would between kills: takes the first
// advancement when it opens, sinks every AP into the job's primary stat, and
// spends SP on its attack skill first, then whatever else it can raise.
void PlayCharacter(ms::GameState& state) {
  ms::CharacterInstance& c = state.character;
  if (c.CanAdvanceJob()) {
    // Whatever the current job is offered next -- the branch does not matter
    // here, since meso per kill is the same whoever swings.
    std::vector<ms::Job> choices =
        ms::JobChoicesForStage(c.proto().job(), c.proto().job_stage() + 1);
    c.AdvanceJob(choices.empty() ? ms::JOB_SWORDMAN : choices.front());
  }
  ms::StatField primary = ms::PrimaryStatField(c.proto().job());
  if (primary != ms::STAT_FIELD_UNSPECIFIED && c.proto().ap() > 0) {
    c.AllocateStat(primary, c.proto().ap());
  }
  for (int pass = 0; pass < 2; ++pass) {
    for (const std::pair<const std::string, ms::Skill>& entry : state.skills) {
      // Attack skills first: they are what raises damage per swing, and a
      // player short on SP buys those before the passives.
      bool attack = entry.second.kind() == ms::SKILL_KIND_ATTACK;
      if ((pass == 0) != attack) {
        continue;
      }
      while (c.LearnSkill(entry.second, 1)) {
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &err));
  if (!runfiles) {
    LOG(FATAL) << "Could not create Runfiles: " << err;
  }
  ms::GameState state(
      ms::LoadTextProtoDir<ms::EquipPrototype>(
          runfiles->Rlocation("ms/data/equip")),
      ms::LoadTextProtoDir<ms::Scroll>(runfiles->Rlocation("ms/data/scrolls")),
      ms::LoadTextProtoDir<ms::ItemPrototype>(
          runfiles->Rlocation("ms/data/items")),
      ms::LoadTextProtoDir<ms::Mob>(runfiles->Rlocation("ms/data/mobs")),
      ms::LoadTextProtoDir<ms::MapData>(runfiles->Rlocation("ms/data/maps")),
      ms::LoadTextProtoDir<ms::Skill>(runfiles->Rlocation("ms/data/skills")),
      ms::GameMode::kPlay, ms::JOB_ADVANCEMENT_UNSPECIFIED, kSimSeed);

  // The weapon under test. Kill speed should not change meso per level, so
  // running this with weapons orders of magnitude apart is the check.
  std::string weapon = argc > 1 ? argv[1] : "fafnir_mistilteinn";
  state.character.PickUp(
      std::make_unique<ms::EquipInstance>(state.equips.at(weapon)));
  state.character.Equip(0);
  printf("weapon %s, starting at level %d\n", weapon.c_str(),
         state.character.proto().level());

  ms::CombatSim sim;
  // Rebuilt only when the character levels or moves map, the two things that
  // change what a fight is worth. Building it every step prices every attack
  // against every mob afresh, which is where this sim spent its time.
  ms::CombatParams params;
  int params_level = -1;
  std::string params_map;
  const int kMilestones[] = {5, 10, 15, 20, 25, 30, 40, 50, 60};
  const int kNumMilestones =
      static_cast<int>(sizeof(kMilestones) / sizeof(kMilestones[0]));
  int next = 0;
  double seconds = 0.0;
  int64_t last_meso = -1;
  int64_t stalled = 0;
  while (next < kNumMilestones) {
    int level = state.character.proto().level();
    std::string map = AppropriateMap(state, level);
    if (map != state.current_map) {
      state.current_map = map;
    }
    PlayCharacter(state);
    if (level != params_level || state.current_map != params_map) {
      params = ms::ComputeCombatParams(state);
      params_level = level;
      params_map = state.current_map;
    }
    ms::AdvanceCombat(state, sim, params, 0.3);
    seconds += 0.3;
    SellAllEtc(state.character);
    int now = state.character.proto().level();
    while (next < kNumMilestones && now >= kMilestones[next]) {
      printf("level %2d  meso %10lld   (%.1f h farmed, map %s)\n",
             kMilestones[next],
             static_cast<long long>(state.character.proto().meso()),
             seconds / 3600.0, state.current_map.c_str());
      ++next;
    }
    // A map whose mobs cannot be brought down inside one respawn beat pays
    // nothing at all -- the beat refills the queue at full HP and the damage
    // so far is lost. Report that rather than spinning on it.
    if (state.character.proto().meso() == last_meso) {
      ++stalled;
    } else {
      stalled = 0;
      last_meso = state.character.proto().meso();
    }
    if (stalled > 2000000) {
      printf("stalled at level %d on %s -- cannot kill here\n", now,
             state.current_map.c_str());
      break;
    }
  }
  return 0;
}
