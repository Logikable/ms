#include "src/game_state.h"

#include <random>
#include <utility>

#include "src/protos/character.pb.h"

namespace ms {

namespace {

Character MakeStartingCharacterProto() {
  Character proto;
  proto.set_level(2);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(5);
  proto.mutable_allocated_stats()->set_str(13);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(4);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return proto;
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
