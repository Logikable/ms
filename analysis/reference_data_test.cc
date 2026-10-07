#include "analysis/reference_data.h"

#include <map>
#include <string>

#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"
#include "src/embedded_data.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

using google::protobuf::Struct;
using google::protobuf::Value;

// Built once: ReferenceDataJson checks the star force model against the game
// over every slot and band, which is the point of the test and its cost.
const Struct& Data() {
  static const Struct* const data = [] {
    auto* parsed = new Struct;
    const std::string json =
        ReferenceDataJson(LoadTextProtoMap<EquipPrototype>(EmbeddedEquips()));
    EXPECT_TRUE(google::protobuf::util::JsonStringToMessage(json, parsed).ok());
    return parsed;
  }();
  return *data;
}

const Value& Field(const Struct& s, const std::string& key) {
  return s.fields().at(key);
}

TEST(ReferenceDataTest, ListsEveryCubeAndRank) {
  EXPECT_EQ(Field(Data(), "ranks").list_value().values_size(), 4);
  const Struct& potential = Field(Data(), "potential").struct_value();
  EXPECT_EQ(Field(potential, "cubes").list_value().values_size(), 4);
  EXPECT_EQ(Field(potential, "groups").list_value().values_size(), 5);
}

TEST(ReferenceDataTest, StarForceHasACategoryForTheWeapon) {
  const Struct& stars = Field(Data(), "star_force").struct_value();
  EXPECT_EQ(Field(stars, "rates").list_value().values_size(), 30);
  bool weapon = false;
  for (const Value& c : Field(stars, "categories").list_value().values()) {
    for (const Value& slot :
         Field(c.struct_value(), "slots").list_value().values()) {
      weapon |= slot.string_value() == "Weapon";
    }
  }
  EXPECT_TRUE(weapon);
}

TEST(ReferenceDataTest, InnerAbilityLinesHaveFourRanks) {
  const Struct& ability = Field(Data(), "inner_ability").struct_value();
  ASSERT_GT(Field(ability, "lines").list_value().values_size(), 0);
  for (const Value& line : Field(ability, "lines").list_value().values()) {
    EXPECT_EQ(Field(line.struct_value(), "weights").list_value().values_size(),
              4);
  }
}

TEST(ReferenceDataTest, PageTakesTheDataAtThePlaceholder) {
  EXPECT_EQ(ReferencePage("const DATA = /*REFERENCE_DATA*/null;", "{\"a\":1}"),
            "const DATA = {\"a\":1};");
}

}  // namespace
}  // namespace ms
