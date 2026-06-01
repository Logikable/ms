#include <map>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "src/character.h"
#include "src/proto_loader.h"
#include "src/frontend/tui.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

struct GameState {
  explicit GameState(std::map<std::string, ms::EquipPrototype> equips_arg)
      : equips(std::move(equips_arg)),
        character(ms::Character{}) {
    character.PickUp(equips.at("sword"));
    character.Equip(0);
    character.PickUp(equips.at("long_sword"));
    character.PickUp(equips.at("sabre"));
  }

  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  std::map<std::string, ms::EquipPrototype> equips;
  ms::CharacterInstance character;
};

}  // namespace

int main(int argc, char** argv) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &err));
  if (!runfiles) {
    LOG(FATAL) << "Could not create Runfiles: " << err;
  }

  std::map<std::string, ms::EquipPrototype> equips =
      ms::LoadTextProtoDir<ms::EquipPrototype>(
          runfiles->Rlocation("ms/data/equip"));

  GameState state(std::move(equips));
  ms::RunTui(state.character);
  return 0;
}
