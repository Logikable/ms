#include <map>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "src/embedded_data.h"
#include "src/frontend/tui.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

ABSL_FLAG(std::string, mode, "play",
          "Which state to start in: 'play' for a new character on Maple "
          "Island, or 'test' for the workbench -- a level 10 Beginner with "
          "meso and a spread of items to exercise the screens with.");

namespace {

ms::GameMode ParseMode(const std::string& mode) {
  if (mode == "play") {
    return ms::GameMode::kPlay;
  }
  if (mode == "test") {
    return ms::GameMode::kTest;
  }
  LOG(FATAL) << "Unknown --mode '" << mode << "'; expected 'play' or 'test'";
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::GameMode mode = ParseMode(absl::GetFlag(FLAGS_mode));

  // The data is compiled in, so there is nothing to find on disk and nothing
  // beside the executable to lose.
  std::map<std::string, ms::EquipPrototype> equips =
      ms::LoadTextProtoMap<ms::EquipPrototype>(ms::EmbeddedEquips());
  std::map<std::string, ms::Scroll> scrolls =
      ms::LoadTextProtoMap<ms::Scroll>(ms::EmbeddedScrolls());
  std::map<std::string, ms::ItemPrototype> items =
      ms::LoadTextProtoMap<ms::ItemPrototype>(ms::EmbeddedItems());
  std::map<std::string, ms::Mob> mobs =
      ms::LoadTextProtoMap<ms::Mob>(ms::EmbeddedMobs());
  std::map<std::string, ms::MapData> maps =
      ms::LoadTextProtoMap<ms::MapData>(ms::EmbeddedMaps());
  std::map<std::string, ms::Skill> skills =
      ms::LoadTextProtoMap<ms::Skill>(ms::EmbeddedSkills());

  ms::GameState state(std::move(equips), std::move(scrolls), std::move(items),
                      std::move(mobs), std::move(maps), std::move(skills),
                      mode);

  ms::Tui(state).Run();
  return 0;
}
