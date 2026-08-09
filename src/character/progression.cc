#include "src/character/progression.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

struct Unlock {
  Feature feature;
  int level;
};

// The early game in one place, in the order the player meets it.
constexpr Unlock kUnlocks[] = {
    // Level 2 is left to itself: the first level-up hands over AP, and that
    // is the whole lesson of it. The panels start arriving at 3.
    {Feature::kEquipped, 3},
    {Feature::kBag, 4},
    // Deliberately the same level as the bag: it is the bag that makes taking
    // something off possible, so move the two together if either changes.
    {Feature::kUnequip, 4},
    {Feature::kSkills, 10},
    {Feature::kShop, 20},
    // Far enough out that a player meets it once the early game is behind
    // them, and far enough that the meso for spell traces is coming in.
    {Feature::kScrolling, 40},
    // Above kTrialLevelCap on purpose: star force is written and playable, but
    // it is not what the trial is for, so it waits for the cap to lift. Only
    // the workbench reaches it, by way of the Level-Up item.
    {Feature::kStarForce, 70},
    {Feature::kRecovery, 140},
};

// The upgrades, in the order they arrive. One list rather than a condition in
// the card and another in the menus, so a fourth upgrade joins both at once.
constexpr Feature kUpgrades[] = {
    Feature::kScrolling,
    Feature::kStarForce,
    Feature::kRecovery,
};

// The lowest level of each pacing band and how far it stretches a duration.
// Read from the bottom up: the last band the level clears is the one that
// applies.
struct Speed {
  int level;
  double factor;
};

constexpr Speed kSpeeds[] = {
    {1, 2.0}, {10, 3.0}, {30, 5.0}, {60, 8.0}, {100, 10.0},
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

std::string FeatureName(Feature feature) {
  switch (feature) {
    case Feature::kEquipped:
      return "Equipment";
    case Feature::kBag:
      return "the Bag";
    case Feature::kUnequip:
      return "Unequipping";
    case Feature::kScrolling:
      return "Scrolling";
    case Feature::kStarForce:
      return "Star Force";
    case Feature::kRecovery:
      return "Recovery";
    case Feature::kSkills:
      return "Skills";
    case Feature::kShop:
      return "the Shop";
  }
  LOG(FATAL) << "Feature " << static_cast<int>(feature) << " has no name";
}

std::vector<Feature> UpgradesUnlockedBetween(int from_level, int to_level) {
  std::vector<Feature> opened;
  for (Feature feature : kUpgrades) {
    int level = UnlockLevel(feature);
    if (from_level < level && level <= to_level) {
      opened.push_back(feature);
    }
  }
  return opened;
}

int HotkeysTipRetireLevel() {
  // One level past the bag, which is the last panel the tip has to account
  // for. Derived rather than written out, so retuning the early game moves the
  // tip along with the panels it describes.
  return UnlockLevel(Feature::kBag) + 1;
}

bool HotkeysTipVisible(const CharacterInstance& character) {
  return character.proto().level() < HotkeysTipRetireLevel();
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
