#include "src/proto_loader.h"

#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "src/equip.pb.h"

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
