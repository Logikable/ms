#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "src/embedded_data.h"
#include "src/frontend/tui.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"
#include "src/save.h"

ABSL_FLAG(std::string, mode, "play",
          "Which state to start in: 'play' for a new character on Maple "
          "Island, or 'test' for the workbench -- a level 10 Beginner with "
          "meso and a spread of items to exercise the screens with.");
ABSL_FLAG(std::string, job, "",
          "Workbench only (--mode=test): a job advancement to start at the "
          "top of, such as 'hunter' or 'archer'. The character arrives at the "
          "last level before their next advancement, with the AP spent and "
          "the SP left to spend. Unset starts the workbench's own job with "
          "its whole book already bought.");

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

// Every advancement a character can actually be started at, as --job spells
// them: the enum's own names lowercased and stripped of their prefix. Built
// from the descriptor rather than listed, so a new job joins the flag by
// existing.
constexpr char kJobPrefix[] = "JOB_ADVANCEMENT_";

std::vector<std::string> JobFlagNames() {
  const google::protobuf::EnumDescriptor* descriptor =
      ms::JobAdvancement_descriptor();
  std::vector<std::string> names;
  for (int i = 0; i < descriptor->value_count(); ++i) {
    if (descriptor->value(i)->number() == ms::JOB_ADVANCEMENT_UNSPECIFIED) {
      continue;
    }
    std::string name(descriptor->value(i)->name());
    names.push_back(
        absl::AsciiStrToLower(name.substr(std::strlen(kJobPrefix))));
  }
  return names;
}

// The advancement --job names, or UNSPECIFIED for the empty flag. Dies on
// anything else rather than falling back on the default: a typo that started
// the wrong job silently would cost more than it saved.
ms::JobAdvancement ParseJob(const std::string& job, ms::GameMode mode) {
  if (job.empty()) {
    return ms::JOB_ADVANCEMENT_UNSPECIFIED;
  }
  if (mode != ms::GameMode::kTest) {
    LOG(FATAL) << "--job is for the workbench; pass --mode=test with it";
  }
  ms::JobAdvancement value = ms::JOB_ADVANCEMENT_UNSPECIFIED;
  if (ms::JobAdvancement_Parse(kJobPrefix + absl::AsciiStrToUpper(job),
                               &value) &&
      value != ms::JOB_ADVANCEMENT_UNSPECIFIED) {
    return value;
  }
  LOG(FATAL) << "Unknown --job '" << job << "'; expected one of "
             << absl::StrJoin(JobFlagNames(), ", ");
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::GameMode mode = ParseMode(absl::GetFlag(FLAGS_mode));
  ms::JobAdvancement test_job = ParseJob(absl::GetFlag(FLAGS_job), mode);

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
  std::map<std::string, ms::EquipSet> sets =
      ms::LoadTextProtoMap<ms::EquipSet>(ms::EmbeddedSets());
  std::map<std::string, ms::Boss> bosses =
      ms::LoadTextProtoMap<ms::Boss>(ms::EmbeddedBosses());

  ms::GameState state(std::move(equips), std::move(scrolls), std::move(items),
                      std::move(mobs), std::move(maps), std::move(skills), mode,
                      test_job);
  // Handed over after the state is built and before anything is loaded into
  // it: what a set pays is worked out from what is worn, and a save restores
  // the worn map through the same recompute.
  state.character.UseEquipSets(std::move(sets));
  state.bosses = std::move(bosses);

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
