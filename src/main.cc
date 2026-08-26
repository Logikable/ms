#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "src/build_config.h"
#include "src/character/exp_table.h"
#include "src/combat/offline.h"
#include "src/embedded_data.h"
#include "src/frontend/tui.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/multiplayer/protocol.h"
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
          "last level before their next advancement, with the AP spent. Unset "
          "starts the workbench's own job.");
ABSL_FLAG(int32_t, level, 0,
          "Workbench only (--mode=test): the level to arrive at, instead of "
          "the top of the job's own band. Never below the level the job is "
          "taken at, and never above the trial's ceiling.");
ABSL_FLAG(std::string, equips, "clean",
          "Workbench only (--mode=test): what state the character's gear "
          "arrives in. 'clean' as it drops, 'scroll' with every upgrade slot "
          "passed, or 'sf' with that and every star.");
ABSL_FLAG(std::string, server, ms::DefaultServerAddress(),
          "Where the multiplayer server is, as host:port. Empty plays alone, "
          "which is what a build made without multiplayer does whatever this "
          "says. The workbench connects only to a server named here, never to "
          "the one the game ships with.");
ABSL_FLAG(std::string, skills, "zero",
          "Workbench only (--mode=test): what to do with the book the "
          "character's job is standing in. 'zero' leaves it unbought with the "
          "SP in the pool, 'max' buys the whole of it. The books behind it "
          "are bought either way.");

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
// Dies on a workbench flag passed to a real game, so a tester who meant
// --mode=test hears about it rather than playing on without what they asked
// for.
void RefuseOutsideTheWorkbench(const char* flag, ms::GameMode mode) {
  if (mode != ms::GameMode::kTest) {
    LOG(FATAL) << flag << " is for the workbench; pass --mode=test with it";
  }
}

ms::TestEquips ParseEquips(const std::string& equips, ms::GameMode mode) {
  if (equips == "clean") {
    return ms::TestEquips::kClean;
  }
  RefuseOutsideTheWorkbench("--equips", mode);
  if (equips == "scroll") {
    return ms::TestEquips::kScrolled;
  }
  if (equips == "sf") {
    return ms::TestEquips::kStarForced;
  }
  LOG(FATAL) << "Unknown --equips '" << equips
             << "'; expected clean, scroll or sf";
}

ms::TestSkills ParseSkills(const std::string& skills, ms::GameMode mode) {
  if (skills == "zero") {
    return ms::TestSkills::kZero;
  }
  RefuseOutsideTheWorkbench("--skills", mode);
  if (skills == "max") {
    return ms::TestSkills::kMax;
  }
  LOG(FATAL) << "Unknown --skills '" << skills << "'; expected zero or max";
}

// The level the workbench arrives at. 0 leaves it to the job, which is the
// top of its own band. The bounds are checked here rather than in the seeding
// so the tester is told what they asked for is out of reach, rather than
// quietly given something else.
int ParseLevel(int level, ms::JobAdvancement job, ms::GameMode mode) {
  if (level == 0) {
    return 0;
  }
  RefuseOutsideTheWorkbench("--level", mode);
  int floor = ms::NextAdvancementLevel(
      ms::StageForAdvancement(
          job == ms::JOB_ADVANCEMENT_UNSPECIFIED ? ms::kTestAdvancement : job) -
      1);
  if (level < floor || level > ms::kTrialLevelCap) {
    LOG(FATAL) << "--level " << level << " is outside " << floor << ".."
               << ms::kTrialLevelCap << " for that job";
  }
  return level;
}

ms::JobAdvancement ParseJob(const std::string& job, ms::GameMode mode) {
  if (job.empty()) {
    return ms::JOB_ADVANCEMENT_UNSPECIFIED;
  }
  RefuseOutsideTheWorkbench("--job", mode);
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
  ms::TestOptions test;
  test.job = ParseJob(absl::GetFlag(FLAGS_job), mode);
  test.level = ParseLevel(absl::GetFlag(FLAGS_level), test.job, mode);
  test.equips = ParseEquips(absl::GetFlag(FLAGS_equips), mode);
  test.skills = ParseSkills(absl::GetFlag(FLAGS_skills), mode);

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
                      test, std::nullopt, std::move(sets));
  state.bosses = std::move(bosses);

  // The workbench neither reads nor writes a save: it starts from its known
  // state every run, and must never be able to overwrite a real character.
  std::string save_path;
  ms::OfflineReport offline;
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
    // Before the TUI is built, so the character the first frame draws is the
    // one the absence already paid, and the level-up cards do not go up for
    // levels the welcome-back card is about to report.
    offline = ms::ApplyOfflineProgress(
        state,
        ms::AbsenceSeconds(state.last_seen_unix_seconds,
                           static_cast<std::int64_t>(std::time(nullptr))));
  }

  // The workbench reaches a server it was pointed at and no other. The party
  // screens are screens like any other and it should be able to exercise
  // them, but a character built to try things out has no business turning up
  // in the lobby everybody else is in.
  std::string server = absl::GetFlag(FLAGS_server);
  if (mode != ms::GameMode::kPlay && server == ms::DefaultServerAddress()) {
    server.clear();
  }
  ms::Tui tui(state, save_path, server);
  tui.ShowOfflineReport(offline);
  tui.Run();
  return 0;
}
