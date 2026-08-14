#include "src/proto_loader.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "absl/log/log.h"
#include "google/protobuf/text_format.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

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
       std::filesystem::recursive_directory_iterator(dir_path)) {
    if (entry.path().extension() != ".textproto") {
      continue;
    }
    const std::string stem = entry.path().stem().string();
    // The key is the stem alone, so two subfolders naming the same file are
    // one item as far as every caller is concerned. Better to stop than to
    // keep whichever the directory walk reached first.
    if (result.count(stem) > 0) {
      LOG(FATAL) << "Duplicate textproto name '" << stem << "' under "
                 << dir_path;
    }
    T proto;
    LoadTextProto(entry.path().string(), &proto);
    result.emplace(stem, std::move(proto));
  }
  return result;
}

template <typename T>
std::map<std::string, T> LoadTextProtoMap(
    const std::map<std::string, std::string>& sources) {
  std::map<std::string, T> result;
  for (const std::pair<const std::string, std::string>& entry : sources) {
    T proto;
    if (!google::protobuf::TextFormat::ParseFromString(entry.second, &proto)) {
      LOG(FATAL) << "Failed to parse embedded textproto: " << entry.first;
    }
    result.emplace(entry.first, std::move(proto));
  }
  return result;
}

// Explicit instantiations are required because the template body is defined
// here in the .cc rather than in the header.
template std::map<std::string, EquipPrototype> LoadTextProtoDir<EquipPrototype>(
    const std::string&);
template std::map<std::string, Scroll> LoadTextProtoDir<Scroll>(
    const std::string&);
template std::map<std::string, Mob> LoadTextProtoDir<Mob>(const std::string&);
template std::map<std::string, MapData> LoadTextProtoDir<MapData>(
    const std::string&);
template std::map<std::string, ItemPrototype> LoadTextProtoDir<ItemPrototype>(
    const std::string&);
template std::map<std::string, Skill> LoadTextProtoDir<Skill>(
    const std::string&);
template std::map<std::string, EquipSet> LoadTextProtoDir<EquipSet>(
    const std::string&);

template std::map<std::string, EquipPrototype> LoadTextProtoMap<EquipPrototype>(
    const std::map<std::string, std::string>&);
template std::map<std::string, Scroll> LoadTextProtoMap<Scroll>(
    const std::map<std::string, std::string>&);
template std::map<std::string, Mob> LoadTextProtoMap<Mob>(
    const std::map<std::string, std::string>&);
template std::map<std::string, MapData> LoadTextProtoMap<MapData>(
    const std::map<std::string, std::string>&);
template std::map<std::string, ItemPrototype> LoadTextProtoMap<ItemPrototype>(
    const std::map<std::string, std::string>&);
template std::map<std::string, Skill> LoadTextProtoMap<Skill>(
    const std::map<std::string, std::string>&);
template std::map<std::string, EquipSet> LoadTextProtoMap<EquipSet>(
    const std::map<std::string, std::string>&);

}  // namespace ms
