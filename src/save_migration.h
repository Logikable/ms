/* Reading a save written by an older build.
 *
 * One function per format version that has been left behind, and one entry
 * point that runs a save forward to the version this build writes. Kept apart
 * from save.cc because it only grows: every version the game ships needs its
 * upgrade kept forever, and none of it has anything to do with putting bytes
 * on a disk.
 *
 * See SaveGame in save.proto for what each version changed.
 */
#ifndef MS_SRC_SAVE_MIGRATION_H_
#define MS_SRC_SAVE_MIGRATION_H_

#include <string>

#include "src/protos/save.pb.h"

namespace ms {

// Reads `bytes`, a save at format version `version`, into `save` at the version
// this build writes. Takes the BYTES rather than a parsed SaveGame: an old
// layout has to be read through the message it was written with, whose fields
// are not SaveGame's any more.
bool UpgradeSave(int version, const std::string& bytes, SaveGame& save);

}  // namespace ms

#endif  // MS_SRC_SAVE_MIGRATION_H_
