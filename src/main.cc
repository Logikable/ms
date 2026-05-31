#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/character.h"
#include "src/commands/equip.h"
#include "src/commands/help.h"
#include "src/commands/inv.h"
#include "src/commands/scroll.h"
#include "src/commands/unequip.h"
#include "src/frontend.h"
#include "src/proto_loader.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

struct GameState {
  explicit GameState(std::map<std::string, ms::EquipPrototype> equips_arg,
                     std::vector<ms::Scroll> scrolls_arg)
      : equips(std::move(equips_arg)),
        scrolls(std::move(scrolls_arg)),
        character(ms::Character{}),
        rng(std::random_device{}()) {
    character.PickUp(equips.at("sword"));
    character.Equip(0);
  }

  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  std::map<std::string, ms::EquipPrototype> equips;
  std::vector<ms::Scroll> scrolls;
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

  std::map<std::string, ms::EquipPrototype> equips =
      ms::LoadTextProtoDir<ms::EquipPrototype>(
          runfiles->Rlocation("ms/data/equip"));
  std::map<std::string, ms::Scroll> scroll_map =
      ms::LoadTextProtoDir<ms::Scroll>(
          runfiles->Rlocation("ms/data/scrolls"));
  std::vector<ms::Scroll> scrolls;
  scrolls.reserve(scroll_map.size());
  for (const std::pair<const std::string, ms::Scroll>& entry : scroll_map) {
    scrolls.push_back(entry.second);
  }
  ms::SortScrolls(scrolls);

  GameState state(std::move(equips), std::move(scrolls));
  ms::Frontend frontend("> ");

  ms::RegisterHelpCommand(frontend);
  ms::RegisterEquipCommand(frontend, state.character);
  ms::RegisterUnequipCommand(frontend, state.character);
  ms::RegisterInvCommand(frontend, state.character);
  ms::RegisterScrollCommand(frontend, state.character, state.scrolls,
                             state.rng);

  frontend.Run();
  return 0;
}
