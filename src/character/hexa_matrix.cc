#include "src/character/hexa_matrix.h"

#include <algorithm>

#include "src/character/sol_erda.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// GMS's per-level prices from the wiki's cost table, whole Sol Erda then
// Fragments, levels 1 to 30.
struct Step {
  int sol_erda;
  int fragments;
};
using Ladder = Step[kHexaNodeMaxLevel];

constexpr Ladder kOriginLadder = {
    {5, 100}, {1, 30},   {1, 35},  {1, 40},   {2, 45},  {2, 50},
    {2, 55},  {3, 60},   {3, 65},  {10, 200}, {3, 80},  {3, 90},
    {4, 100}, {4, 110},  {4, 120}, {4, 130},  {4, 140}, {4, 150},
    {5, 160}, {15, 350}, {5, 170}, {5, 180},  {5, 190}, {5, 200},
    {5, 210}, {6, 220},  {6, 230}, {6, 240},  {7, 250}, {20, 500}};
constexpr Ladder kMasteryLadder = {
    {3, 50},  {1, 15},  {1, 18},  {1, 20},  {1, 23},  {1, 25},
    {1, 28},  {2, 30},  {2, 33},  {5, 100}, {2, 40},  {2, 45},
    {2, 50},  {2, 55},  {2, 60},  {2, 65},  {2, 70},  {2, 75},
    {3, 80},  {8, 175}, {3, 85},  {3, 90},  {3, 95},  {3, 100},
    {3, 105}, {3, 110}, {3, 115}, {3, 120}, {4, 125}, {10, 250}};
constexpr Ladder kEnhancementLadder = {
    {4, 75},  {1, 23},   {1, 27},  {1, 30},  {2, 34},  {2, 38},
    {2, 42},  {3, 45},   {3, 49},  {8, 150}, {3, 60},  {3, 68},
    {3, 75},  {3, 83},   {3, 90},  {3, 98},  {3, 105}, {3, 113},
    {4, 120}, {12, 263}, {4, 128}, {4, 135}, {4, 143}, {4, 150},
    {4, 158}, {5, 165},  {5, 173}, {5, 180}, {6, 188}, {15, 375}};
constexpr Ladder kCommonLadder = {
    {7, 125}, {2, 38},   {2, 44},  {2, 50},   {3, 57},   {3, 63},
    {3, 69},  {5, 75},   {5, 82},  {14, 300}, {5, 110},  {5, 124},
    {6, 138}, {6, 152},  {6, 165}, {6, 179},  {6, 193},  {6, 207},
    {7, 220}, {17, 525}, {7, 234}, {7, 248},  {7, 262},  {7, 275},
    {7, 289}, {9, 303},  {9, 317}, {9, 330},  {10, 344}, {20, 750}};
constexpr Ladder kArchetypeLadder = {
    {4, 90},  {1, 25},   {1, 30},  {1, 35},  {2, 40},  {2, 45},
    {2, 50},  {3, 55},   {3, 60},  {9, 180}, {3, 73},  {3, 81},
    {3, 90},  {3, 98},   {4, 107}, {4, 115}, {4, 124}, {4, 132},
    {4, 141}, {14, 315}, {4, 151}, {5, 160}, {5, 170}, {5, 179},
    {5, 189}, {5, 198},  {5, 208}, {5, 217}, {6, 227}, {18, 450}};

const Ladder* LadderFor(HexaNodeKind kind) {
  switch (kind) {
    case HEXA_NODE_KIND_ORIGIN:
    case HEXA_NODE_KIND_ASCENT:
      return &kOriginLadder;
    case HEXA_NODE_KIND_MASTERY:
      return &kMasteryLadder;
    case HEXA_NODE_KIND_ENHANCEMENT:
      return &kEnhancementLadder;
    case HEXA_NODE_KIND_COMMON:
      return &kCommonLadder;
    case HEXA_NODE_KIND_ARCHETYPE:
      return &kArchetypeLadder;
    default:
      return nullptr;
  }
}

}  // namespace

int MaxHexaNodeLevel(HexaNodeKind kind) {
  return LadderFor(kind) == nullptr ? 0 : kHexaNodeMaxLevel;
}

HexaPrice HexaNodeStepPrice(HexaNodeKind kind, int level) {
  const Ladder* ladder = LadderFor(kind);
  if (ladder == nullptr || level < 1 || level > kHexaNodeMaxLevel ||
      (kind == HEXA_NODE_KIND_ORIGIN && level == 1)) {
    return {};
  }
  const Step& step = (*ladder)[level - 1];
  return {step.sol_erda * kEnergyPerSolErda, step.fragments};
}

HexaPrice HexaNodePrice(HexaNodeKind kind, int from, int to) {
  HexaPrice total;
  for (int level = std::max(from + 1, 1); level <= to; ++level) {
    const HexaPrice step = HexaNodeStepPrice(kind, level);
    total.energy += step.energy;
    total.fragments += step.fragments;
  }
  return total;
}

}  // namespace ms
