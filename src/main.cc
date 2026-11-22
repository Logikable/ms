#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
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
#include "src/combat/bis_rate.h"
#include "src/combat/offline.h"
#include "src/console.h"
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
#include "src/save/save.h"
#include "src/save/save_lock.h"

ABSL_FLAG(std::string, mode, "play",
          "Which state to start in: 'play' for a new character on Maple "
          "Island, 'test' for the workbench -- a level 1 Beginner with meso "
          "and a spread of items to exercise the screens with -- or 'max' "
          "for the ceiling, a character standing in everything the climb to "
          "--level pays for, or 'bis' for every upgrade at its maximum and "
          "every line measured best, which takes a while to search.");
ABSL_FLAG(std::string, job, "",
          "--mode=test, max or bis: a job advancement to start at the top "
          "of, such as 'hunter' or 'archer'. The character arrives at the "
          "last level before their next advancement, with the AP spent. Unset "
          "starts the seeded character's own job.");
ABSL_FLAG(int32_t, level, 0,
          "--mode=test, max or bis: the level to arrive at, instead of the "
          "top of the job's own band. Never below the level the job is taken "
          "at, and never above the highest level the game has.");
ABSL_FLAG(std::string, server, ms::DefaultServerAddress(),
          "Where the multiplayer server is, as host:port ([host]:port for "
          "IPv6). Empty plays alone, "
          "which is what a build made without multiplayer does whatever this "
          "says. The workbench connects only to a server named here, never to "
          "the one the game ships with.");
ABSL_FLAG(bool, bgm, true,
          "Play the map and boss music. --nobgm opens the null sound device "
          "instead of the machine's, which is what to pass when the game is "
          "being driven rather than played.");

namespace {

ms::GameMode ParseMode(const std::string& mode) {
  if (mode == "play") {
    return ms::GameMode::kPlay;
  }
  if (mode == "test") {
    return ms::GameMode::kTest;
  }
  if (mode == "max") {
    return ms::GameMode::kMax;
  }
  if (mode == "bis") {
    return ms::GameMode::kBis;
  }
  LOG(FATAL) << "Unknown --mode '" << mode
             << "'; expected 'play', 'test', 'max' or 'bis'";
}

// Every advancement a character can start at, as --job spells them: the enum
// names in lowercase without their prefix. Built from the descriptor instead of
// listed, so a new job is accepted automatically.
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

// The two flags seeded modes share. --mode=max decides gear and skills itself
// (what a level has paid for is the whole point of the mode), so those flags
// stay workbench-only.
void RefuseInPlay(const char* flag, ms::GameMode mode) {
  if (mode == ms::GameMode::kPlay) {
    LOG(FATAL) << flag << " is for --mode=test, max or bis";
  }
}

// The level the workbench reaches. 0 lets the job decide, meaning the top of
// its band. The bounds are checked here instead of in seeding, so the tester
// learns their request is out of range instead of silently getting something
// else.
int ParseLevel(int level, ms::JobAdvancement job, ms::GameMode mode) {
  if (level == 0) {
    return 0;
  }
  RefuseInPlay("--level", mode);
  int floor = ms::NextAdvancementLevel(
      ms::StageForAdvancement(
          job == ms::JOB_ADVANCEMENT_UNSPECIFIED ? ms::kTestAdvancement : job) -
      1);
  // Bounded by the game's highest level, not the trial's cap: the cap is where
  // play stops paying EXP, and the workbench is where the levels above it are
  // looked at.
  if (level < floor || level > ms::kMaxLevel) {
    LOG(FATAL) << "--level " << level << " is outside " << floor << ".."
               << ms::kMaxLevel << " for that job";
  }
  return level;
}

ms::JobAdvancement ParseJob(const std::string& job, ms::GameMode mode) {
  if (job.empty()) {
    return ms::JOB_ADVANCEMENT_UNSPECIFIED;
  }
  RefuseInPlay("--job", mode);
  ms::JobAdvancement value = ms::JOB_ADVANCEMENT_UNSPECIFIED;
  if (ms::JobAdvancement_Parse(kJobPrefix + absl::AsciiStrToUpper(job),
                               &value) &&
      value != ms::JOB_ADVANCEMENT_UNSPECIFIED) {
    return value;
  }
  LOG(FATAL) << "Unknown --job '" << job << "'; expected one of "
             << absl::StrJoin(JobFlagNames(), ", ");
}

// The data is compiled in, so there's nothing to find on disk and nothing next
// to the executable to lose.
std::unique_ptr<ms::GameState> NewGameState(ms::GameMode mode,
                                            const ms::TestOptions& test) {
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

  return std::make_unique<ms::GameState>(
      std::move(equips), std::move(scrolls), std::move(items), std::move(mobs),
      std::move(maps), std::move(skills), mode, test, std::nullopt,
      std::move(sets), std::move(bosses));
}

// Loads the save and pays out the absence, or says why the game won't start.
bool LoadSave(ms::GameState& state, const std::string& save_path,
              ms::OfflineReport& offline) {
  ms::LoadResult load = ms::LoadGameFromFile(state, save_path);
  if (load.status == ms::LoadStatus::kUnreadable ||
      load.status == ms::LoadStatus::kFromTheFuture) {
    // Refused instead of starting over. An unreadable save might still be
    // recoverable, and starting a new game would overwrite it within thirty
    // seconds. Naming the file and the way out keeps this from being a dead
    // end.
    std::fprintf(stderr,
                 "%s\n\nThe game will not start until that file is dealt "
                 "with. Move it somewhere safe to begin a new character, or "
                 "put back a copy that works.\n",
                 load.message.c_str());
    return false;
  }
  // Before the TUI is built, so the first frame shows the character after the
  // absence has paid out, and level-up cards don't show for levels the
  // welcome-back card is about to report.
  offline = ms::ApplyOfflineProgress(
      state, ms::AbsenceSeconds(state.last_seen_unix_seconds,
                                static_cast<std::int64_t>(std::time(nullptr))));
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  // A player opening the game from a file manager gets a console window that
  // closes as soon as this returns. Name it, and keep it open at the end for
  // whatever is still on it.
  ms::NameConsoleWindow();
  ms::ConsoleHold hold;

  absl::ParseCommandLine(argc, argv);
  ms::GameMode mode = ParseMode(absl::GetFlag(FLAGS_mode));
  ms::TestOptions test;
  test.job = ParseJob(absl::GetFlag(FLAGS_job), mode);
  test.level = ParseLevel(absl::GetFlag(FLAGS_level), test.job, mode);
  test.bis_rate = ms::BisFightRate;
  if (mode == ms::GameMode::kBis) {
    for (auto& [job, table] :
         ms::LoadTextProtoMap<ms::BisTable>(ms::EmbeddedBis())) {
      test.bis_tables.push_back(std::move(table));
    }
  }

  std::unique_ptr<ms::GameState> owned = NewGameState(mode, test);
  ms::GameState& state = *owned;

  // The workbench neither reads nor writes a save: it starts from the same
  // state every run, and must never overwrite a real character.
  std::string save_path;
  ms::OfflineReport offline;
  std::unique_ptr<ms::SaveLock> save_lock;
  if (mode == ms::GameMode::kPlay) {
    save_path = ms::SavePathFor(argv[0]);
    save_lock = std::make_unique<ms::SaveLock>(save_path);
    if (save_lock->taken_elsewhere()) {
      std::fprintf(stderr,
                   "This save is already open in another window.\n\nClose "
                   "that one first. To play two characters at once, run "
                   "the game from a second folder with a new save.\n");
      return 1;
    }
    if (!LoadSave(state, save_path, offline)) {
      return 1;
    }
  }

  // The workbench connects only to a server it was explicitly given. It should
  // be able to test the party screens, but a test character doesn't belong in
  // the lobby everyone else uses.
  std::string server = absl::GetFlag(FLAGS_server);
  if (mode != ms::GameMode::kPlay && server == ms::DefaultServerAddress()) {
    server.clear();
  }
  ms::Tui tui(state, save_path, server, absl::GetFlag(FLAGS_bgm));
  tui.ShowOfflineReport(offline);
  tui.Run();
  return 0;
}
