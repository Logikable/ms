#include <map>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "src/frontend/tui.h"
#include "src/game_state.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

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
  std::map<std::string, ms::Scroll> scrolls =
      ms::LoadTextProtoDir<ms::Scroll>(runfiles->Rlocation("ms/data/scrolls"));

  ms::GameState state(std::move(equips), std::move(scrolls));
  state.character.PickUp(state.equips.at("sword"));
  state.character.Equip(0);
  state.character.PickUp(state.equips.at("long_sword"));
  state.character.PickUp(state.equips.at("sabre"));
  ms::Tui(state).Run();
  return 0;
}
