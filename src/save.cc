#include "src/save.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "src/account.h"
#include "src/game_state.h"
#include "src/protos/save.pb.h"
#include "src/save_migration.h"

namespace ms {
namespace {

constexpr char kSaveFileName[] = "ms.save";

// The name the in-progress write goes to. Beside the save rather than in a
// temp directory, because a rename is only atomic within one filesystem.
constexpr char kTempFileName[] = "ms.save.writing";

// Flushes the stream and asks the OS to put the bytes on the disk. Without
// this the rename can land before the contents do, and a machine that loses
// power in between comes back to a save that is the right size and all zeroes.
bool FlushToDisk(std::ofstream& out, const std::string& path) {
  out.flush();
  if (!out.good()) {
    return false;
  }
  out.close();
  // std::ofstream has no fsync of its own, and reopening through stdio is the
  // portable way to reach one. A failure here is worth carrying on from: the
  // bytes are written, only the ordering guarantee is missing.
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file != nullptr) {
    std::fflush(file);
    std::fclose(file);
  }
  return true;
}

// The played character as the save holds them, with the two clocks and the
// map that are theirs alone.
CharacterSave ActiveCharacterSave(const GameState& state) {
  CharacterSave slot;
  *slot.mutable_character() = state.character.ToProto();
  slot.set_current_map(state.current_map);
  slot.set_created_unix_seconds(state.created_unix_seconds);
  slot.set_playtime_seconds(static_cast<int64_t>(state.playtime_seconds));
  return slot;
}

// Puts every character on the account into `save`, the played one back in the
// slot they came from. The others were never unpacked, so they go out exactly
// as they came in.
void WriteCharacters(const GameState& state, SaveGame& save) {
  const std::vector<CharacterSave>& others = state.inactive_characters;
  int slot =
      std::clamp(state.active_character, 0, static_cast<int>(others.size()));
  for (int i = 0; i < slot; ++i) {
    *save.add_characters() = others[i];
  }
  *save.add_characters() = ActiveCharacterSave(state);
  for (std::size_t i = slot; i < others.size(); ++i) {
    *save.add_characters() = others[i];
  }
  save.set_active_character(slot);
}

// The account as the file should carry it: what the session holds, with the
// played character's climb folded in. The watermark is what the characters in
// the file have reached, and this is the only place that knows the one being
// played has moved.
AccountInstance AccountToWrite(const GameState& state) {
  AccountInstance account(state.account.proto());
  account.RecordProgress(state.character.proto().level(),
                         state.character.proto().job_stage());
  return account;
}

// The slot the save says is being played, or the first if it says something
// out of range -- a hand-edited file, or one whose active character was
// deleted by a build that could do that.
int ActiveSlot(const SaveGame& save) {
  int slot = save.active_character();
  return slot >= 0 && slot < save.characters_size() ? slot : 0;
}

// Loads the account, and raises its unlocks to take in every character in the
// file. The watermark is written on save, so this only matters for a file
// that predates it or was edited by hand -- but it costs one pass and keeps
// the account's promise: what any character opened stays open.
void LoadAccount(const SaveGame& save, GameState& state) {
  state.account = AccountInstance(save.account());
  for (const CharacterSave& slot : save.characters()) {
    state.account.RecordProgress(slot.character().level(),
                                 slot.character().job_stage());
  }
}

// Loads the active character into play and keeps the rest as they arrived.
void LoadCharacters(const SaveGame& save, GameState& state) {
  int slot = ActiveSlot(save);
  const CharacterSave& active = save.characters(slot);
  state.character.RestoreFrom(active.character(), state.equips, state.items);
  // A save written under older rules is the one thing that can arrive with its
  // books unbalanced, so this is the door to check all four at: the AP and the
  // SP against the levels that paid them, the skills against the maximums the
  // data states now, and the Hyper Stats against the points the level pays.
  // SP last -- ReconcileSkills moves points between a book and its pool.
  state.character.ReconcileAp();
  state.character.ReconcileSkills(state.skills);
  state.character.ReconcileHyperStats();
  state.character.ReconcileSp(state.skills);
  state.current_map = active.current_map();
  state.playtime_seconds = static_cast<double>(active.playtime_seconds());
  // Left alone when the save has no creation time to give -- one written
  // before the field existed. The state was stamped when it was built, so
  // holding on to that reads as "created now" rather than as the epoch.
  if (active.created_unix_seconds() != 0) {
    state.created_unix_seconds = active.created_unix_seconds();
  }

  state.active_character = slot;
  state.inactive_characters.clear();
  for (int i = 0; i < save.characters_size(); ++i) {
    if (i != slot) {
      state.inactive_characters.push_back(save.characters(i));
    }
  }
}

}  // namespace

std::string SavePathFor(const std::string& argv0) {
  std::filesystem::path program(argv0);
  std::filesystem::path directory = program.parent_path();
  if (directory.empty()) {
    // Invoked by bare name off the PATH, so the executable's own location is
    // not on offer. The working directory is the honest fallback.
    return kSaveFileName;
  }
  return (directory / kSaveFileName).string();
}

bool SaveGameToFile(const GameState& state, const std::string& path) {
  SaveGame save;
  save.set_format_version(kSaveFormatVersion);
  WriteCharacters(state, save);
  *save.mutable_account() = AccountToWrite(state).proto();
  // Stamped here rather than carried from the state: what this field means is
  // when the file was written, and only the write knows that.
  save.set_last_seen_unix_seconds(static_cast<int64_t>(std::time(nullptr)));

  std::string bytes;
  if (!save.SerializeToString(&bytes)) {
    LOG(ERROR) << "Could not serialize the save";
    return false;
  }

  std::filesystem::path target(path);
  std::filesystem::path temp = target.parent_path() / kTempFileName;
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      LOG(ERROR) << "Could not open " << temp.string() << " to write the save";
      return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out.good() || !FlushToDisk(out, temp.string())) {
      LOG(ERROR) << "Could not write the save to " << temp.string();
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      return false;
    }
  }

  // The step that makes the whole thing atomic: until this returns, the old
  // save is the save. rename replaces the target in one operation on both
  // POSIX and Windows.
  std::error_code error;
  std::filesystem::rename(temp, target, error);
  if (error) {
    LOG(ERROR) << "Could not replace " << path << ": " << error.message();
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return false;
  }
  return true;
}

LoadResult LoadGameFromFile(GameState& state, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {LoadStatus::kMissing, ""};
  }
  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  if (!in.good() && !in.eof()) {
    return {LoadStatus::kUnreadable,
            "The save file could not be read: " + path};
  }

  SaveGame save;
  // ParseFromString rejects a truncated or non-proto file. It does not reject
  // every corruption -- proto has no checksum -- but it catches the shapes a
  // half-write and a wrong file actually take.
  if (!save.ParseFromString(bytes)) {
    return {LoadStatus::kUnreadable,
            "The save file is damaged and cannot be read: " + path};
  }
  if (save.format_version() > kSaveFormatVersion) {
    return {
        LoadStatus::kFromTheFuture,
        "The save file was written by a newer version of the game: " + path};
  }
  if (!UpgradeSave(save.format_version(), bytes, save)) {
    return {LoadStatus::kUnreadable,
            "The save file is damaged and cannot be read: " + path};
  }
  if (save.characters().empty()) {
    return {LoadStatus::kUnreadable,
            "The save file holds no characters: " + path};
  }

  LoadAccount(save, state);
  LoadCharacters(save, state);
  state.last_seen_unix_seconds = save.last_seen_unix_seconds();
  return {LoadStatus::kLoaded, ""};
}

SavePolicy::SavePolicy(std::string path,
                       std::chrono::steady_clock::time_point now)
    : path_(std::move(path)), last_save_(now) {
}

bool SavePolicy::Save(const GameState& state,
                      std::chrono::steady_clock::time_point now) {
  if (path_.empty()) {
    return false;
  }
  last_save_ = now;
  if (!SaveGameToFile(state, path_)) {
    LOG(ERROR) << "Could not save the game to " << path_;
    return false;
  }
  return true;
}

bool SavePolicy::AutosaveIfDue(const GameState& state,
                               std::chrono::steady_clock::time_point now) {
  if (path_.empty() || now - last_save_ < kAutosaveInterval) {
    return false;
  }
  return Save(state, now);
}

}  // namespace ms
