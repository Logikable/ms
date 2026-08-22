#include "src/save.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "absl/log/log.h"
#include "src/game_state.h"
#include "src/protos/save.pb.h"

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
  *save.mutable_character() = state.character.ToProto();
  save.set_current_map(state.current_map);
  save.set_created_unix_seconds(state.created_unix_seconds);
  save.set_playtime_seconds(static_cast<int64_t>(state.playtime_seconds));
  *save.mutable_keybinds() = state.keybinds;

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

  state.character.RestoreFrom(save.character(), state.equips, state.items);
  state.current_map = save.current_map();
  state.playtime_seconds = static_cast<double>(save.playtime_seconds());
  // Left alone when the save has no creation time to give -- one written
  // before the field existed. The state was stamped when it was built, so
  // holding on to that reads as "created now" rather than as the epoch.
  if (save.created_unix_seconds() != 0) {
    state.created_unix_seconds = save.created_unix_seconds();
  }
  // A save from before the field existed carries nothing here, which the
  // frontend reads as the default bindings.
  state.keybinds = save.keybinds();
  return {LoadStatus::kLoaded, ""};
}

}  // namespace ms
