#ifndef MS_SRC_PROTO_LOADER_H_
#define MS_SRC_PROTO_LOADER_H_

#include <map>
#include <string>

#include "google/protobuf/message.h"

namespace ms {

// Reads a textproto file at `path` and parses it into `msg`.
// LOG(FATAL) on any I/O or parse error.
void LoadTextProto(const std::string& path, google::protobuf::Message* msg);

// Loads all *.textproto files in `dir_path` into a map keyed by filename stem.
// LOG(FATAL) on any I/O or parse error.
template <typename T>
std::map<std::string, T> LoadTextProtoDir(const std::string& dir_path);

}  // namespace ms

#endif  // MS_SRC_PROTO_LOADER_H_
