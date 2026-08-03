#include <cstdio>
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
#include "src/save.h"

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

  // The workbench neither reads nor writes a save: it starts from its known
  // state every run, and must never be able to overwrite a real character.
  std::string save_path;
  if (mode == ms::GameMode::kPlay) {
    save_path = ms::SavePathFor(argv[0]);
    ms::LoadResult load = ms::LoadGameFromFile(state, save_path);
    if (load.status == ms::LoadStatus::kUnreadable ||
        load.status == ms::LoadStatus::kFromTheFuture) {
      // Refused rather than started over. A save that cannot be read might
      // still be recoverable, and beginning a new game here would write over
      // it within thirty seconds. Naming the file and the way out is what
      // keeps that from being a dead end.
      std::fprintf(stderr,
                   "%s\n\nThe game will not start until that file is dealt "
                   "with. Move it somewhere safe to begin a new character, or "
                   "put back a copy that works.\n",
                   load.message.c_str());
      return 1;
    }
  }

  ms::Tui(state, save_path).Run();
  return 0;
}
