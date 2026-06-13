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
                     std::map<std::string, Scroll> scrolls_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      rng(std::random_device{}()),
      character(rng, MakeStartingCharacterProto()) {
  auto it = equips.find("Fafnir Mistilteinn");
  if (it != equips.end()) {
    Equip fafnir_state;
    fafnir_state.set_equip_name("Fafnir Mistilteinn");
    fafnir_state.set_remaining_upgrade_slots(0);
    fafnir_state.set_stars(20);
    fafnir_state.mutable_scroll_stats()->set_attack(40);
    fafnir_state.mutable_scroll_stats()->set_str(16);
    character.PickUp(it->second, fafnir_state);
  }
}

}  // namespace ms
