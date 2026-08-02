#include <map>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
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
#include "tools/cpp/runfiles/runfiles.h"

ABSL_FLAG(std::string, mode, "play",
          "Which state to start in: 'play' for a new character on Maple "
          "Island, or 'test' for the workbench -- a level 10 Beginner with "
          "meso and a spread of items to exercise the screens with.");

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

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
  std::map<std::string, ms::ItemPrototype> items =
      ms::LoadTextProtoDir<ms::ItemPrototype>(
          runfiles->Rlocation("ms/data/items"));
  std::map<std::string, ms::Mob> mobs =
      ms::LoadTextProtoDir<ms::Mob>(runfiles->Rlocation("ms/data/mobs"));
  std::map<std::string, ms::MapData> maps =
      ms::LoadTextProtoDir<ms::MapData>(runfiles->Rlocation("ms/data/maps"));
  std::map<std::string, ms::Skill> skills =
      ms::LoadTextProtoDir<ms::Skill>(runfiles->Rlocation("ms/data/skills"));

  ms::GameState state(std::move(equips), std::move(scrolls), std::move(items),
                      std::move(mobs), std::move(maps), std::move(skills),
                      mode);

  ms::Tui(state).Run();
  return 0;
}
