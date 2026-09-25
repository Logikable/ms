/* Utilities for loading textproto files. The templates are defined in
 * proto_loader.cc and only explicitly instantiated for the catalog types listed
 * there; to support another type, add an explicit instantiation there.
 */
#ifndef MS_SRC_PROTO_LOADER_H_
#define MS_SRC_PROTO_LOADER_H_

#include <map>
#include <string>

#include "google/protobuf/message.h"

namespace ms {

// Reads the textproto file at `path` into `msg`. LOG(FATAL) on any I/O or parse
// error.
void LoadTextProto(const std::string& path, google::protobuf::Message* msg);

// Loads every *.textproto under `dir_path`, including subfolders, into a map
// keyed by filename stem. Folders only help human readers and aren't part of
// the key, so an item can be moved between folders without changing anything
// that refers to it. LOG(FATAL) on any I/O or parse error, or if two files
// under `dir_path` share a stem.
template <typename T>
std::map<std::string, T> LoadTextProtoDir(const std::string& dir_path);

// Parses already-read textprotos, keyed by name, into a map with the same keys:
// the form //src:embedded_data returns. The game reads its data this way;
// LoadTextProtoDir is for tests, which have the files. LOG(FATAL) on any parse
// error.
template <typename T>
std::map<std::string, T> LoadTextProtoMap(
    const std::map<std::string, std::string>& sources);

}  // namespace ms

#endif  // MS_SRC_PROTO_LOADER_H_
