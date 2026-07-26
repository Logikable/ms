#include "src/game_state.h"

#include <random>
#include <string>
#include <utility>

#include "src/character.h"
#include "src/protos/character.pb.h"

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

// The level a fresh character starts at, for testing. Level 10 is where the
// first job advancement opens, so the game begins at the choice -- which job
// the character becomes is now the player's to make, not this file's.
constexpr int kStartingLevel = 10;

// A Beginner standing at its first advancement. Built by running the real
// leveling mechanics rather than hardcoding totals, so AP and HP stay
// consistent with the level if it is changed here.
Character MakeStartingCharacterProto() {
  // LevelUp doesn't consume randomness; a local rng keeps this independent of
  // GameState's member rng and its construction order.
  std::mt19937 rng(0);
  CharacterInstance character(rng, MakeBaseBeginnerProto());
  while (character.proto().level() < kStartingLevel) {
    character.LevelUp();
  }
  return character.proto();
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg,
                     std::map<std::string, ItemPrototype> items_arg,
                     std::map<std::string, Mob> mobs_arg,
                     std::map<std::string, MapData> maps_arg,
                     std::map<std::string, Skill> skills_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      skills(std::move(skills_arg)),
      rng(std::random_device{}()),
      character(rng, MakeStartingCharacterProto()) {
}

}  // namespace ms
