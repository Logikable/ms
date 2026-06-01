#include "src/game_state.h"

#include <random>
#include <utility>

#include "src/protos/character.pb.h"

namespace ms {

namespace {

Character MakeStartingCharacterProto() {
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  return proto;
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      rng(std::random_device{}()),
      character(rng, MakeStartingCharacterProto()) {
  character.PickUp(equips.at("sword"));
  character.Equip(0);
  character.PickUp(equips.at("long_sword"));
  character.PickUp(equips.at("sabre"));
}

}  // namespace ms
