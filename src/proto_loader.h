#ifndef MS_SRC_PROTO_LOADER_H_
#define MS_SRC_PROTO_LOADER_H_

#include <string>

#include "google/protobuf/message.h"

namespace ms {

// Reads a textproto file at `path` and parses it into `msg`.
// LOG(FATAL) on any I/O or parse error.
void LoadTextProto(const std::string& path, google::protobuf::Message* msg);

}  // namespace ms

#endif  // MS_SRC_PROTO_LOADER_H_
