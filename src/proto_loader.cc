#include "src/proto_loader.h"

#include <fstream>
#include <string>

#include "absl/log/log.h"
#include "google/protobuf/text_format.h"

namespace ms {

void LoadTextProto(const std::string& path, google::protobuf::Message* msg) {
  std::ifstream file(path);
  if (!file.is_open()) {
    LOG(FATAL) << "Cannot open textproto: " << path;
  }
  const std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  if (!google::protobuf::TextFormat::ParseFromString(content, msg)) {
    LOG(FATAL) << "Failed to parse textproto: " << path;
  }
}

}  // namespace ms
