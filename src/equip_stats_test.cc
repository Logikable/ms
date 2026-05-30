#include "src/equip_stats.h"

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "src/equip.pb.h"

namespace ms {
namespace {

TEST(SumEquipStatsTest, EmptyListReturnsZero) {
  EquipStats result = SumEquipStats(absl::Span<const EquipStats>{});
  EXPECT_EQ(result.str(), 0);
  EXPECT_EQ(result.attack(), 0);
}

TEST(SumEquipStatsTest, SingleElementIsIdentity) {
  EquipStats a;
  a.set_str(5);
  a.set_attack(10);
  const EquipStats sources[] = {a};
  EquipStats result = SumEquipStats(sources);
  EXPECT_EQ(result.str(), 5);
  EXPECT_EQ(result.attack(), 10);
}

TEST(SumEquipStatsTest, AllFieldsAreSummed) {
  EquipStats a;
  a.set_str(1);
  a.set_dex(2);
  a.set_int_(3);
  a.set_luk(4);
  a.set_attack(5);
  a.set_magic_attack(6);
  a.set_max_hp(7);
  a.set_def(8);

  EquipStats b;
  b.set_str(10);
  b.set_dex(20);
  b.set_int_(30);
  b.set_luk(40);
  b.set_attack(50);
  b.set_magic_attack(60);
  b.set_max_hp(70);
  b.set_def(80);

  const EquipStats sources[] = {a, b};
  EquipStats result = SumEquipStats(sources);
  EXPECT_EQ(result.str(), 11);
  EXPECT_EQ(result.dex(), 22);
  EXPECT_EQ(result.int_(), 33);
  EXPECT_EQ(result.luk(), 44);
  EXPECT_EQ(result.attack(), 55);
  EXPECT_EQ(result.magic_attack(), 66);
  EXPECT_EQ(result.max_hp(), 77);
  EXPECT_EQ(result.def(), 88);
}

TEST(SumEquipStatsTest, ThreeElementsAccumulate) {
  EquipStats a;
  a.set_attack(3);
  const EquipStats sources[] = {a, a, a};
  EquipStats result = SumEquipStats(sources);
  EXPECT_EQ(result.attack(), 9);
}

}  // namespace
}  // namespace ms
