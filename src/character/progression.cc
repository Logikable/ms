#include "src/character/progression.h"

#include <cstddef>

#include "absl/log/log.h"
#include "src/character/character.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

struct Unlock {
  Feature feature;
  int level;
};

// The early game in one place, in the order the player meets it.
constexpr Unlock kUnlocks[] = {
    {Feature::kEquipped, 2},
    {Feature::kBag, 3},
    // Deliberately the same level as the bag: it is the bag that makes taking
    // something off possible, so move the two together if either changes.
    {Feature::kUnequip, 3},
    {Feature::kScrolling, 10},
    {Feature::kSkills, 10},
    {Feature::kShop, 20},
    {Feature::kStarForce, 60},
    {Feature::kRecovery, 140},
};

// The lowest level of each pacing band and how far it stretches a duration.
// Read from the bottom up: the last band the level clears is the one that
// applies.
struct Speed {
  int level;
  double factor;
};

constexpr Speed kSpeeds[] = {
    {1, 1.0}, {10, 2.0}, {30, 3.0}, {60, 5.0}, {100, 7.0}, {140, 10.0},
};

}  // namespace

int UnlockLevel(Feature feature) {
  for (const Unlock& unlock : kUnlocks) {
    if (unlock.feature == feature) {
      return unlock.level;
    }
  }
  LOG(FATAL) << "Feature " << static_cast<int>(feature)
             << " has no unlock level";
}

bool Unlocked(Feature feature, const CharacterInstance& character) {
  if (character.proto().level() < UnlockLevel(feature)) {
    return false;
  }
  if (feature == Feature::kSkills) {
    // A Beginner reaching level 10 is being offered an advancement, not
    // skills: the skill sets belong to the jobs, so the tab has nothing to
    // show until one is chosen.
    return character.proto().job() != JOB_BEGINNER;
  }
  return true;
}

double GameSpeedFactor(int level) {
  double factor = kSpeeds[0].factor;
  for (const Speed& speed : kSpeeds) {
    if (level >= speed.level) {
      factor = speed.factor;
    }
  }
  return factor;
}

}  // namespace ms
