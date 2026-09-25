/* Reading and writing the player's save file.
 *
 * The save sits next to the executable, not in the working directory: a player
 * who double-clicks the binary in a file manager gets whatever working
 * directory that manager chooses, and the save belongs with the game they
 * unzipped.
 *
 * Writing is atomic. The bytes go to a temp file in the same directory, are
 * flushed to disk, and only then replace the save by rename. A machine that
 * dies mid-write leaves the previous save intact plus a stray temp file, never
 * a half-written save. That's what makes autosaving safe when the player may
 * close the window at any moment.
 */
#ifndef MS_SRC_SAVE_H_
#define MS_SRC_SAVE_H_

#include <chrono>
#include <string>

#include "src/game_state.h"
#include "src/protos/save.pb.h"

namespace ms {

// The format this build writes, and the newest it can read. Older versions are
// upgraded on read; see save_migration.h. See SaveGame.format_version for what
// each version contains.
constexpr int kSaveFormatVersion = 3;

// Where the save file lives: next to the running executable. `argv0` is the
// program path the OS gave. Falls back to the working directory if that path
// has no directory part.
std::string SavePathFor(const std::string& argv0);

// How a load ended. kMissing is the normal first launch, and the only one that
// isn't a problem.
enum class LoadStatus {
  kLoaded,
  kMissing,
  // The file exists but isn't a save, is truncated, or has no character: a
  // write from a build that died before the rename, a wrong file with the right
  // name, or an edit.
  kUnreadable,
  // Written by a newer build. Refused instead of read, since its fields may not
  // mean the same thing here.
  kFromTheFuture,
};

// The outcome of a load, with a sentence suitable for the player. `message` is
// empty for kLoaded and kMissing.
struct LoadResult {
  LoadStatus status = LoadStatus::kMissing;
  std::string message;
};

// Writes `state` to `path`, replacing what's there. Returns false if the save
// couldn't be written, in which case the previous save is untouched.
bool SaveGameToFile(const GameState& state, const std::string& path);

// Reads `path` into `state`, resolving items against the catalogs it already
// has. The character with the offline check becomes the played one; the rest
// are kept untouched so the next write preserves them. Leaves `state` unchanged
// unless the status is kLoaded.
LoadResult LoadGameFromFile(GameState& state, const std::string& path);

// How long a session goes between automatic saves. Short enough that closing
// the window loses almost nothing, and long enough that a few-kilobyte write
// costs far less than the 300ms combat tick.
constexpr std::chrono::seconds kAutosaveInterval(30);

// Decides when a session saves. Owned by whoever runs the loop the player is
// leaving, since only it knows they've left.
class SavePolicy {
 public:
  // `path` is where the game is written; empty disables saving, which keeps the
  // workbench from ever touching a player's file. `now` starts the autosave
  // timer, so the first autosave is a full interval away.
  SavePolicy(std::string path, std::chrono::steady_clock::time_point now);

  // Saves the game regardless of the timer. Returns whether anything was
  // written. A failure is logged and ignored: failing to save isn't a reason to
  // crash, and the previous save is still there.
  bool Save(const GameState& state, std::chrono::steady_clock::time_point now);

  // Like Save(), but only once kAutosaveInterval has passed since the last
  // save.
  bool AutosaveIfDue(const GameState& state,
                     std::chrono::steady_clock::time_point now);

  bool saving() const {
    return !path_.empty();
  }

 private:
  std::string path_;
  std::chrono::steady_clock::time_point last_save_;
};

}  // namespace ms

#endif  // MS_SRC_SAVE_H_
