/* Reading and writing the player's one save file.
 *
 * The save sits beside the executable rather than in the working directory: a
 * player who double-clicks the binary from a file manager gets whatever cwd
 * that manager felt like, and the save belongs with the game they unzipped.
 *
 * Writing is atomic. The bytes go to a temp file in the same directory, are
 * flushed to disk, and only then replace the save by rename -- so a machine
 * that dies mid-write leaves the previous save whole and a stray temp file,
 * never a half-written save. This is what makes autosaving safe to do under a
 * player who may close the window at any moment.
 */
#ifndef MS_SRC_SAVE_H_
#define MS_SRC_SAVE_H_

#include <chrono>
#include <string>

#include "src/game_state.h"
#include "src/protos/save.pb.h"

namespace ms {

// The format this build writes, and the newest it can read. See
// SaveGame.format_version.
constexpr int kSaveFormatVersion = 1;

// Where the save file lives: alongside the running executable. `argv0` is the
// program path as the OS gave it. Falls back to the working directory when
// that path says nothing about a directory.
std::string SavePathFor(const std::string& argv0);

// How a load ended. kMissing is the ordinary first launch, and the only one of
// these that is not a problem.
enum class LoadStatus {
  kLoaded,
  kMissing,
  // The file is there but is not a save, or is truncated -- a write from a
  // build that died before rename, a wrong file with the right name, an edit.
  kUnreadable,
  // Written by a newer build than this one. Refused rather than read, since
  // the fields it uses may not mean here what they meant there.
  kFromTheFuture,
};

// The outcome of a load, with a sentence fit to show the player. `message` is
// empty on kLoaded and kMissing.
struct LoadResult {
  LoadStatus status = LoadStatus::kMissing;
  std::string message;
};

// Writes `state` to `path`, replacing whatever is there. Returns false if the
// save could not be written, in which case the previous save is untouched.
bool SaveGameToFile(const GameState& state, const std::string& path);

// Reads `path` into `state`, resolving items against the catalogs it already
// holds. Leaves `state` alone unless the status is kLoaded.
LoadResult LoadGameFromFile(GameState& state, const std::string& path);

// How long a session goes between automatic saves. Short enough that closing
// the window costs a player almost nothing, long enough that a few-kilobyte
// write is nowhere near the 300ms combat tick in cost.
constexpr std::chrono::seconds kAutosaveInterval(30);

// When a session writes its save. Held by whoever owns the loop the player is
// leaving, which is the only thing that knows they have left.
class SavePolicy {
 public:
  // `path` is where the game is written; empty turns saving off, which is how
  // the workbench avoids ever touching a player's file. `now` starts the
  // autosave clock, so the first one is due a whole interval in.
  SavePolicy(std::string path, std::chrono::steady_clock::time_point now);

  // Writes the game, whatever the clock says. Returns whether anything was
  // written: a failure is logged and swallowed, since a save that cannot be
  // written is not a reason to take the game down and the previous one is
  // still there.
  bool Save(const GameState& state, std::chrono::steady_clock::time_point now);

  // Save(), but only once kAutosaveInterval has passed since the last one.
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
