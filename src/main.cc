#include <memory>
#include <random>
#include <string>

#include "absl/log/log.h"
#include "src/character.h"
#include "src/commands/equip.h"
#include "src/commands/inv.h"
#include "src/commands/scroll.h"
#include "src/commands/unequip.h"
#include "src/frontend.h"
#include "src/proto_loader.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// TODO: replace with a data-driven item registry once more items are added;
// currently bootstraps with a hardcoded sword and watt scroll.
struct GameState {
  explicit GameState(ms::EquipPrototype sword_proto_arg,
                     ms::Scroll watt_scroll_arg)
      : sword_proto(std::move(sword_proto_arg)),
        watt_scroll(std::move(watt_scroll_arg)),
        character(ms::Character{}),
        rng(std::random_device{}()) {
    character.PickUp(sword_proto);
    character.Equip(0);
  }

  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  ms::EquipPrototype sword_proto;
  ms::Scroll watt_scroll;
  ms::CharacterInstance character;
  std::mt19937 rng;
};

}  // namespace

int main(int argc, char** argv) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &err));
  if (!runfiles) {
    LOG(FATAL) << "Could not create Runfiles: " << err;
  }

  ms::EquipPrototype sword_proto;
  ms::LoadTextProto(
      runfiles->Rlocation("ms/data/equip/sword.textproto"), &sword_proto);

  ms::Scroll watt_scroll;
  ms::LoadTextProto(
      runfiles->Rlocation("ms/data/scrolls/weapon_att_100_t1.textproto"),
      &watt_scroll);

  GameState state(std::move(sword_proto), std::move(watt_scroll));
  ms::Frontend frontend("> ");

  ms::RegisterEquipCommand(frontend, state.character);
  ms::RegisterUnequipCommand(frontend, state.character);
  ms::RegisterInvCommand(frontend, state.character);
  ms::RegisterScrollCommand(frontend, state.character, state.watt_scroll,
                             state.rng);

  frontend.Run();
  return 0;
}
