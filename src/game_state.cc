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

// A level-15 Warrior to start with, so there is 1st-job SP (and AP) on hand to
// test skills. Built by running the real leveling and advancement mechanics
// rather than hardcoding totals, so AP and SP stay consistent with the level
// and job if either is changed here.
Character MakeStartingCharacterProto() {
  // LevelUp and AdvanceJob don't consume randomness; a local rng keeps this
  // independent of GameState's member rng and its construction order.
  std::mt19937 rng(0);
  CharacterInstance character(rng, MakeBaseBeginnerProto());
  while (character.proto().level() < 15) {
    character.LevelUp();
  }
  character.AdvanceJob(JOB_WARRIOR);
  return character.proto();
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg,
                     std::map<std::string, ItemPrototype> items_arg,
                     std::map<std::string, Mob> mobs_arg,
                     std::map<std::string, MapData> maps_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      rng(std::random_device{}()),
      character(rng, MakeStartingCharacterProto()) {
}

}  // namespace ms
