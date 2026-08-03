/* Utilities for loading textproto files from disk. LoadTextProtoDir is a
 * template defined in proto_loader.cc; it is only explicitly instantiated for
 * EquipPrototype and Scroll. To support additional types, add an explicit
 * instantiation in proto_loader.cc.
 */
#ifndef MS_SRC_PROTO_LOADER_H_
#define MS_SRC_PROTO_LOADER_H_

#include <map>
#include <string>

#include "google/protobuf/message.h"

namespace ms {

// Reads a textproto file at `path` and parses it into `msg`.
// LOG(FATAL) on any I/O or parse error.
void LoadTextProto(const std::string& path, google::protobuf::Message* msg);

// Loads every *.textproto under `dir_path`, subfolders included, into a map
// keyed by filename stem. Folders are for the reader's benefit only: they do
// not enter the key, so an item can be filed differently without anything that
// names it having to change. LOG(FATAL) on any I/O or parse error, or if two
// files anywhere under `dir_path` share a stem.
template <typename T>
std::map<std::string, T> LoadTextProtoDir(const std::string& dir_path);

// Parses an already-read set of textprotos, keyed by name, into a map on the
// same keys -- the shape //src:embedded_data hands back. The game proper reads
// its data this way; LoadTextProtoDir is for tests, which have the files.
// LOG(FATAL) on any parse error.
template <typename T>
std::map<std::string, T> LoadTextProtoMap(
    const std::map<std::string, std::string>& sources);

}  // namespace ms

#endif  // MS_SRC_PROTO_LOADER_H_
