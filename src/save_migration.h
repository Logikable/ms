/* Reading a save written by an older build.
 *
 * One function per old format version, and one entry point that upgrades a save
 * to the version this build writes. Kept separate from save.cc because it only
 * grows: every shipped version needs its upgrade kept forever, and none of it
 * is about writing bytes to disk.
 *
 * See SaveGame in save.proto for what each version changed.
 */
#ifndef MS_SRC_SAVE_MIGRATION_H_
#define MS_SRC_SAVE_MIGRATION_H_

#include <map>
#include <string>

#include "src/protos/item.pb.h"
#include "src/protos/save.pb.h"

namespace ms {

// Reads `bytes`, a save in format version `version`, into `save` at the version
// this build writes. Takes the bytes instead of a parsed SaveGame, because an
// old layout must be read through the message it was written with, whose fields
// no longer match SaveGame's. `items` is the item catalog, which version 2
// needs to tell currencies from ordinary drops.
bool UpgradeSave(int version, const std::string& bytes,
                 const std::map<std::string, ItemPrototype>& items,
                 SaveGame& save);

}  // namespace ms

#endif  // MS_SRC_SAVE_MIGRATION_H_
