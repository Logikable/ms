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
#include "src/item/item.h"
#include "src/protos/save.pb.h"
#include "src/roster.h"
#include "src/save_migration.h"

namespace ms {
namespace {

constexpr char kSaveFileName[] = "ms.save";

// The file an in-progress write goes to. Next to the save instead of in a temp
// directory, because a rename is only atomic within one filesystem.
constexpr char kTempFileName[] = "ms.save.writing";

// Flushes the stream and asks the OS to write the bytes to disk. Otherwise the
// rename can happen before the contents are written, and a power loss in
// between leaves a save of the right size full of zeroes.
bool FlushToDisk(std::ofstream& out, const std::string& path) {
  out.flush();
  if (!out.good()) {
    return false;
  }
  out.close();
  // std::ofstream has no fsync, and reopening through stdio is the portable way
  // to get one. A failure here is fine to continue from: the bytes are written,
  // only the ordering guarantee is missing.
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file != nullptr) {
    std::fflush(file);
    std::fclose(file);
  }
  return true;
}

// Writes every character on the account into `save`, the played one back in
// their original slot, and records which one the next launch opens on.
void WriteCharacters(const GameState& state, SaveGame& save) {
  std::vector<CharacterSave> all = AllCharacters(state);
  for (CharacterSave& slot : all) {
    *save.add_characters() = std::move(slot);
  }
  save.set_offline_character(
      std::clamp(state.offline_slot, 0, save.characters_size() - 1));
}

// The account as the file should store it: the session's account, with the
// played character's progress added. The high-water mark reflects what the
// characters in the file have reached, and only this code knows the played one
// has progressed.
Account AccountToWrite(const GameState& state) {
  Account account = state.account.ToProto();
  RecordProgress(account, state.character.proto().level(),
                 state.character.proto().job_stage());
  return account;
}

// The slot the save opens on, or the first if it's out of range (a hand-edited
// file, or one whose character was deleted by a build that couldn't renumber
// the check).
int OfflineSlot(const SaveGame& save) {
  int slot = save.offline_character();
  return slot >= 0 && slot < save.characters_size() ? slot : 0;
}

// Loads the account and raises its unlocks to include every character in the
// file. The high-water mark is written on save, so this only matters for a file
// from before it existed or edited by hand, but it costs one pass and keeps the
// account's promise that anything unlocked stays unlocked.
void LoadAccount(const SaveGame& save, GameState& state) {
  state.account = AccountInstance(save.account());
  state.account.RestoreBank(save.account().bank(),
                            IndexByDisplayName(state.equips),
                            IndexByDisplayName(state.items));
  for (const CharacterSave& slot : save.characters()) {
    state.account.RecordProgress(slot.character().level(),
                                 slot.character().job_stage());
  }
}

// Opens the save on the character with the offline check, and keeps the rest as
// they are. A launch starts there: it's who was left farming, and the absence
// about to be paid is theirs.
void LoadCharacters(const SaveGame& save, GameState& state) {
  std::vector<CharacterSave> all(save.characters().begin(),
                                 save.characters().end());
  int slot = OfflineSlot(save);
  PutIntoPlay(state, all, slot);
  state.offline_slot = slot;
}

}  // namespace

std::string SavePathFor(const std::string& argv0) {
  std::filesystem::path program(argv0);
  std::filesystem::path directory = program.parent_path();
  if (directory.empty()) {
    // Run by bare name from the PATH, so the executable's location isn't known.
    // The working directory is the fallback.
    return kSaveFileName;
  }
  return (directory / kSaveFileName).string();
}

bool SaveGameToFile(const GameState& state, const std::string& path) {
  SaveGame save;
  save.set_format_version(kSaveFormatVersion);
  WriteCharacters(state, save);
  *save.mutable_account() = AccountToWrite(state);
  // Stamped here instead of copied from the state: this field means when the
  // file was written, and only the write knows that.
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

  // This step makes the whole thing atomic: until it returns, the old save is
  // the save. rename replaces the target in one operation on both POSIX and
  // Windows.
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
  // ParseFromString rejects a truncated or non-proto file. It doesn't catch
  // every corruption, since proto has no checksum, but it catches what a
  // half-write or a wrong file actually look like.
  if (!save.ParseFromString(bytes)) {
    return {LoadStatus::kUnreadable,
            "The save file is damaged and cannot be read: " + path};
  }
  if (save.format_version() > kSaveFormatVersion) {
    return {
        LoadStatus::kFromTheFuture,
        "The save file was written by a newer version of the game: " + path};
  }
  if (!UpgradeSave(save.format_version(), bytes, state.items, save)) {
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
