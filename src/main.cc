#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/character.h"
#include "src/commands/scroll.h"
#include "src/proto_loader.h"
#include "src/tui.h"
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
    character.PickUp(equips.at("long_sword"));
    character.PickUp(equips.at("sabre"));
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
  ms::RunTui(state.character, state.scrolls, state.rng);
  return 0;
}
