#include "analysis/spell_trace_cost.h"

#include "absl/log/log.h"

namespace ms {
namespace {

// Costs run [rate][category]: rates in the order 100, 70, 30, 15; categories
// in the order armor, gloves, weapon, accessory. A 0 means GMS sells no such
// scroll.
struct Band {
  int min_level;
  int cost[4][4];
};

// Two judgement calls, both on cells the wiki leaves as "?":
//
//  - Level 20-29 accessories are filled in as 2/3/3. The rows either side are
//    1/2/2 and 3/4/4, so this is the only reading that keeps the column
//    climbing by one at each step.
//  - Level 250+ gloves, weapons and accessories are left at 0. Nothing in this
//    game reaches level 250, so an invented number there would only be a
//    number nobody checked.
//
// The wiki has no rows at all for 170-199 or 210-249, so those levels fall
// back to the band below them, which is what GMS itself does.
const Band kBands[] = {
    {0, {{1, 1, 1, 1}, {2, 1, 2, 2}, {2, 2, 3, 2}, {0, 0, 4, 0}}},
    {20, {{2, 1, 2, 2}, {2, 2, 3, 3}, {3, 2, 4, 3}, {0, 0, 5, 0}}},
    {30, {{3, 2, 3, 3}, {4, 3, 4, 4}, {5, 4, 5, 4}, {0, 0, 6, 0}}},
    {40, {{4, 3, 4, 3}, {5, 4, 5, 4}, {6, 5, 6, 5}, {0, 0, 7, 0}}},
    {50, {{5, 4, 5, 4}, {6, 5, 6, 5}, {7, 6, 7, 6}, {0, 0, 8, 0}}},
    {60, {{5, 4, 5, 5}, {6, 6, 7, 6}, {7, 7, 8, 7}, {0, 0, 10, 0}}},
    {70, {{6, 5, 7, 6}, {7, 7, 8, 7}, {8, 8, 10, 9}, {0, 0, 12, 0}}},
    {80, {{8, 11, 14, 12}, {11, 14, 18, 15}, {13, 17, 22, 18}, {0, 0, 26, 0}}},
    {90, {{10, 14, 17, 15}, {14, 18, 23, 19}, {17, 22, 28, 23}, {0, 0, 33, 0}}},
    {100,
     {{13, 17, 22, 18}, {17, 23, 28, 24}, {20, 27, 34, 28}, {0, 0, 40, 0}}},
    {110,
     {{16, 20, 26, 22}, {20, 27, 34, 28}, {24, 33, 40, 34}, {0, 0, 48, 0}}},
    {120,
     {{57, 75, 93, 80},
      {72, 96, 120, 100},
      {87, 117, 144, 120},
      {0, 0, 174, 0}}},
    {130,
     {{72, 96, 120, 100},
      {93, 123, 156, 130},
      {114, 150, 186, 155},
      {0, 0, 222, 0}}},
    {140,
     {{90, 120, 144, 125},
      {117, 156, 192, 160},
      {138, 186, 228, 195},
      {0, 0, 276, 0}}},
    {150,
     {{185, 245, 300, 155},
      {240, 320, 390, 200},
      {290, 380, 480, 240},
      {0, 0, 575, 0}}},
    {160,
     {{220, 295, 370, 185},
      {285, 385, 480, 240},
      {345, 460, 575, 285},
      {0, 0, 690, 0}}},
    {200,
     {{435, 580, 725, 360},
      {565, 750, 940, 470},
      {675, 900, 1125, 560},
      {910, 1080, 1350, 0}}},
    {250, {{850, 0, 0, 0}, {1100, 0, 0, 0}, {1325, 0, 0, 0}, {1560, 0, 0, 0}}},
};

int RateRow(int success_rate) {
  switch (success_rate) {
    case 100:
      return 0;
    case 70:
      return 1;
    case 30:
      return 2;
    case 15:
      return 3;
    default:
      LOG(FATAL) << "no spell trace scroll is sold at " << success_rate << "%";
  }
}

}  // namespace

int SpellTraceCost(int required_level, TraceCategory category,
                   int success_rate) {
  const Band* found = &kBands[0];
  for (int i = 0; i < static_cast<int>(sizeof(kBands) / sizeof(kBands[0]));
       ++i) {
    if (kBands[i].min_level <= required_level) {
      found = &kBands[i];
    }
  }
  return found->cost[RateRow(success_rate)][static_cast<int>(category)];
}

}  // namespace ms
