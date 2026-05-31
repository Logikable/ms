#include "src/proto_loader.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "absl/log/log.h"
#include "google/protobuf/text_format.h"
#include "src/equip.pb.h"
#include "src/scroll.pb.h"

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

template <typename T>
std::map<std::string, T> LoadTextProtoDir(const std::string& dir_path) {
  std::map<std::string, T> result;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(dir_path)) {
    if (entry.path().extension() != ".textproto") { continue; }
    T proto;
    LoadTextProto(entry.path().string(), &proto);
    result.emplace(entry.path().stem().string(), std::move(proto));
  }
  return result;
}

template std::map<std::string, EquipPrototype>
LoadTextProtoDir<EquipPrototype>(const std::string&);
template std::map<std::string, Scroll>
LoadTextProtoDir<Scroll>(const std::string&);

}  // namespace ms
