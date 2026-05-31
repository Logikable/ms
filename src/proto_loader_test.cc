#include "src/proto_loader.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "gtest/gtest.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

std::string WriteTempFile(const std::string& name, const std::string& content) {
  std::string path = std::string(testing::TempDir()) + "/" + name;
  std::ofstream f(path);
  f << content;
  return path;
}

TEST(ProtoLoaderTest, LoadsValidTextProto) {
  std::string path = WriteTempFile("equip.textproto",
                                   "name: \"Sword\"\nupgrade_slots: 7\n");
  EquipPrototype equip;
  LoadTextProto(path, &equip);
  EXPECT_EQ(equip.name(), "Sword");
  EXPECT_EQ(equip.upgrade_slots(), 7);
}

TEST(LoadTextProtoDirTest, LoadsAllTextprotosKeyedByStem) {
  std::string dir = std::string(testing::TempDir()) + "/proto_dir_test";
  std::filesystem::create_directory(dir);
  WriteTempFile("proto_dir_test/a.textproto", "name: \"A\"\n");
  WriteTempFile("proto_dir_test/b.textproto", "name: \"B\"\n");
  WriteTempFile("proto_dir_test/ignored.txt", "not a proto");

  std::map<std::string, EquipPrototype> result =
      LoadTextProtoDir<EquipPrototype>(dir);
  ASSERT_EQ(result.size(), 2);
  EXPECT_EQ(result.at("a").name(), "A");
  EXPECT_EQ(result.at("b").name(), "B");
}

TEST(ProtoLoaderTest, FatalOnMissingFile) {
  EquipPrototype equip;
  EXPECT_DEATH(LoadTextProto("/nonexistent/path.textproto", &equip),
               "Cannot open textproto");
}

TEST(ProtoLoaderTest, FatalOnInvalidTextProto) {
  std::string path = WriteTempFile("bad.textproto", "not valid proto {{{\n");
  EquipPrototype equip;
  EXPECT_DEATH(LoadTextProto(path, &equip), "Failed to parse textproto");
}

}  // namespace
}  // namespace ms
