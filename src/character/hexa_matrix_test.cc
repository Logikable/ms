#include "src/character/hexa_matrix.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "src/character/sol_erda.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

void ExpectWhole(HexaNodeKind kind, int64_t sol_erda, int64_t fragments) {
  const HexaPrice whole = HexaNodePrice(kind, 0, kHexaNodeMaxLevel);
  EXPECT_EQ(whole.energy, sol_erda * kEnergyPerSolErda)
      << HexaNodeKind_Name(kind);
  EXPECT_EQ(whole.fragments, fragments) << HexaNodeKind_Name(kind);
}

// GMS's totals for each kind, from the wiki's table.
TEST(HexaMatrixTest, EachLadderCostsWhatGmsChargesForTheWholeNode) {
  ExpectWhole(HEXA_NODE_KIND_ORIGIN, 145, 4400);
  ExpectWhole(HEXA_NODE_KIND_ASCENT, 150, 4500);
  ExpectWhole(HEXA_NODE_KIND_MASTERY, 83, 2252);
  ExpectWhole(HEXA_NODE_KIND_ENHANCEMENT, 123, 3383);
  ExpectWhole(HEXA_NODE_KIND_COMMON, 208, 6268);
  ExpectWhole(HEXA_NODE_KIND_ARCHETYPE, 137, 4035);
  ExpectWhole(HEXA_NODE_KIND_UNSPECIFIED, 0, 0);
  EXPECT_EQ(MaxHexaNodeLevel(HEXA_NODE_KIND_MASTERY), 30);
  EXPECT_EQ(MaxHexaNodeLevel(HEXA_NODE_KIND_UNSPECIFIED), 0);
}

// A whole matrix: Origin, Ascent, four of each job kind, two common nodes and
// the line's archetype node.
TEST(HexaMatrixTest, AWholeMatrixCostsGmsTotal) {
  HexaPrice total;
  auto add = [&](HexaNodeKind kind, int count) {
    const HexaPrice whole = HexaNodePrice(kind, 0, kHexaNodeMaxLevel);
    total.energy += count * whole.energy;
    total.fragments += count * whole.fragments;
  };
  add(HEXA_NODE_KIND_ORIGIN, 1);
  add(HEXA_NODE_KIND_ASCENT, 1);
  add(HEXA_NODE_KIND_MASTERY, 4);
  add(HEXA_NODE_KIND_ENHANCEMENT, 4);
  add(HEXA_NODE_KIND_COMMON, 2);
  add(HEXA_NODE_KIND_ARCHETYPE, 1);
  EXPECT_EQ(total.energy, 1672 * kEnergyPerSolErda);
  EXPECT_EQ(total.fragments, 48011);
}

TEST(HexaMatrixTest, StepsAreGmsPerLevelPrices) {
  EXPECT_EQ(HexaNodeStepPrice(HEXA_NODE_KIND_ORIGIN, 1).fragments, 0)
      << "the Origin comes with the advancement";
  EXPECT_EQ(HexaNodeStepPrice(HEXA_NODE_KIND_ASCENT, 1).fragments, 100);
  EXPECT_EQ(HexaNodeStepPrice(HEXA_NODE_KIND_ORIGIN, 10).energy,
            10 * kEnergyPerSolErda);
  EXPECT_EQ(HexaNodeStepPrice(HEXA_NODE_KIND_ENHANCEMENT, 20).fragments, 263);
  EXPECT_EQ(HexaNodeStepPrice(HEXA_NODE_KIND_COMMON, 30).fragments, 750);
  EXPECT_EQ(HexaNodeStepPrice(HEXA_NODE_KIND_MASTERY, 31).fragments, 0);
  EXPECT_EQ(HexaNodePrice(HEXA_NODE_KIND_MASTERY, 9, 10).fragments, 100);
  EXPECT_EQ(HexaNodePrice(HEXA_NODE_KIND_MASTERY, 10, 10).fragments, 0);
}

}  // namespace
}  // namespace ms
