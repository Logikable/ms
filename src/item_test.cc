#include "src/item.h"

#include "gtest/gtest.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class EquipTraceTest : public ::testing::Test {
 protected:
  EquipPrototype MakeSword() {
    EquipPrototype p;
    p.set_name("Sword");
    p.set_required_level(10);
    p.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    return p;
  }
};

TEST_F(EquipTraceTest, NameAppendsTraceSuffix) {
  EquipTrace trace(MakeSword(), Equip());
  EXPECT_EQ(trace.name(), "Sword Trace");
}

TEST_F(EquipTraceTest, PrototypeMatchesInput) {
  EquipTrace trace(MakeSword(), Equip());
  EXPECT_EQ(trace.prototype().name(), "Sword");
  EXPECT_EQ(trace.prototype().required_level(), 10);
}

TEST_F(EquipTraceTest, EquipStateMatchesInput) {
  Equip state;
  state.set_stars(3);
  EquipTrace trace(MakeSword(), state);
  EXPECT_EQ(trace.equip_state().stars(), 3);
}

TEST_F(EquipTraceTest, StarsAccessorReflectsState) {
  Equip state;
  state.set_stars(4);
  EquipTrace trace(MakeSword(), state);
  EXPECT_EQ(trace.stars(), 4);
}

TEST_F(EquipTraceTest, MaxStarsMatchesRequiredLevel) {
  EquipTrace trace(MakeSword(), Equip());
  EXPECT_EQ(trace.max_stars(), 5);
}

TEST_F(EquipTraceTest, StatsReflectsBaseAndScrollStats) {
  EquipPrototype proto = MakeSword();
  proto.mutable_base_stats()->set_str(5);
  Equip state;
  state.mutable_scroll_stats()->set_str(3);
  EquipTrace trace(proto, state);
  EXPECT_EQ(trace.stats().str(), 8);
}

TEST_F(EquipTraceTest, StarForceStatGainsInherited) {
  // At 2★, warrior item: kPrimaryStatDeltas[0]+[1] = 4 each for STR and DEX.
  Equip state;
  state.set_stars(2);
  EquipTrace trace(MakeSword(), state);
  EquipStats gains = trace.StarForceStatGains();
  EXPECT_EQ(gains.str(), 4);
  EXPECT_EQ(gains.dex(), 4);
  EXPECT_EQ(gains.int_(), 0);
  EXPECT_EQ(gains.luk(), 0);
}

}  // namespace
}  // namespace ms
