#include "src/character/progression.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/account.h"
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
    // One past the bag, which is the last panel the hotkeys tip has to
    // account for: the tip leaves the corner and the menu takes it over.
    {Feature::kMenu, 5},
    {Feature::kSkills, 10},
    {Feature::kShop, 20},
    // Far enough out that a player meets it once the early game is behind
    // them, and far enough that the meso for spell traces is coming in.
    {Feature::kScrolling, 40},
    // Zakum's own level. There is nothing else on the boss screen yet, so
    // opening it any earlier would only show the player a fight they cannot
    // take.
    {Feature::kBoss, 110},
    // Held to the level of the gear it is for. The tiers below carry 5 and 8
    // star caps; the Frozen weapons a token buys at 120 are the first that
    // take 15, and now that an attempt is priced, opening the screen earlier
    // only offers the player a bill for a star that is barely worth having.
    {Feature::kStarForce, 120},
    // Arcane River opens at 200, and it opens with a symbol handed over -- so
    // the tab arrives with something to put in it.
    {Feature::kSymbols, 200},
};

// What an advancement opens rather than a level, and which one opens it: 1 is
// the first advancement, 2 the second. A separate table because the level is
// not the gate here -- a Beginner who never advances stays at the bottom of it
// however high they climb.
struct StageUnlock {
  Feature feature;
  int stage;
};

// Attack, Magic Attack, Attack Speed and Defense arrive with the first job,
// which is the first thing the player has that moves them. The percent rows
// wait for the second, whose passives are where crit and damage rate first
// come from. The last three wait for the third, which is where the levers that
// write them are -- and two of the three pay out on nothing the player has met
// by then anyway.
constexpr StageUnlock kStageUnlocks[] = {
    {Feature::kCombatStats, 1},
    {Feature::kDamageStats, 2},
    {Feature::kAdvancedStats, 3},
};

// The upgrades a level opens, in the order they arrive. One list rather than a
// condition in the card and another in the menus, so a third joins both at
// once.
constexpr Feature kUpgrades[] = {
    Feature::kScrolling,
    Feature::kStarForce,
};

// The upgrades with a gold trail, and the slug their latch keys are built
// from. The slugs are written into the save, so changing one forgets that
// anybody was ever led anywhere and starts every player's trail over.
struct Led {
  Feature feature;
  const char* slug;
  // Whether the trail starts at the worn weapon's name. Scrolling arrives
  // while the item menu is still a place the player may never have opened, so
  // it needs the first signpost. Star force arrives at 120, by which time they
  // have opened it a hundred times, and a second gold thing on screen only
  // takes the eye off the entry.
  bool from_weapon;
};

constexpr Led kLedUpgrades[] = {
    {Feature::kScrolling, "scrolling", true},
    {Feature::kStarForce, "star_force", false},
};

std::string WeaponLeadKey(const char* slug) {
  return std::string("lead_weapon:") + slug;
}

std::string ActionLeadKey(const char* slug) {
  return std::string("lead_action:") + slug;
}

// The lowest level of each pacing band and how far it stretches a duration.
// Read from the bottom up: the last band the level clears is the one that
// applies.
struct Speed {
  int level;
  double factor;
};

constexpr Speed kSpeeds[] = {
    {1, 2.0}, {10, 3.0}, {30, 4.0}, {60, 6.0}, {100, 8.0}, {140, 10.0},
};

}  // namespace

int UnlockLevel(Feature feature) {
  for (const Unlock& unlock : kUnlocks) {
    if (unlock.feature == feature) {
      return unlock.level;
    }
  }
  for (const StageUnlock& unlock : kStageUnlocks) {
    if (unlock.feature == feature) {
      // The level its advancement is offered at, which is the soonest it can
      // open. Whether it has is the character's business, not the level's.
      return NextAdvancementLevel(unlock.stage - 1);
    }
  }
  LOG(FATAL) << "Feature " << static_cast<int>(feature)
             << " has no unlock level";
}

bool Unlocked(Feature feature, const CharacterInstance& character,
              const AccountInstance& account) {
  for (const StageUnlock& unlock : kStageUnlocks) {
    if (unlock.feature == feature) {
      int stage =
          std::max(character.proto().job_stage(), account.max_job_stage());
      return stage >= unlock.stage;
    }
  }
  int level = std::max(character.proto().level(), account.max_level());
  if (level < UnlockLevel(feature)) {
    return false;
  }
  if (feature == Feature::kSkills) {
    // The one condition the account cannot answer for: a Beginner reaching
    // level 10 is being offered an advancement, not skills. The skill sets
    // belong to the jobs, so the tab has nothing to show until this character
    // chooses one.
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
    case Feature::kSkills:
      return "Skills";
    case Feature::kShop:
      return "the Shop";
    case Feature::kMenu:
      return "the Menu";
    case Feature::kBoss:
      return "Bosses";
    case Feature::kSymbols:
      return "Arcane Symbols";
    case Feature::kCombatStats:
      return "Combat Stats";
    case Feature::kDamageStats:
      return "Damage Stats";
    case Feature::kAdvancedStats:
      return "Advanced Stats";
  }
  LOG(FATAL) << "Feature " << static_cast<int>(feature) << " has no name";
}

std::vector<Feature> UpgradesUnlockedBetween(int from_level, int to_level,
                                             int account_level) {
  std::vector<Feature> opened;
  int from = std::max(from_level, account_level);
  for (Feature feature : kUpgrades) {
    int level = UnlockLevel(feature);
    if (from < level && level <= to_level) {
      opened.push_back(feature);
    }
  }
  return opened;
}

bool LeadToWeapon(const CharacterInstance& character,
                  const AccountInstance& account) {
  for (const Led& led : kLedUpgrades) {
    if (led.from_weapon && Unlocked(led.feature, character, account) &&
        !account.Seen(WeaponLeadKey(led.slug))) {
      return true;
    }
  }
  return false;
}

void FollowedToWeapon(const CharacterInstance& character,
                      AccountInstance& account) {
  for (const Led& led : kLedUpgrades) {
    if (led.from_weapon && Unlocked(led.feature, character, account)) {
      account.MarkSeen(WeaponLeadKey(led.slug));
    }
  }
}

bool LeadToAction(Feature feature, const CharacterInstance& character,
                  const AccountInstance& account) {
  for (const Led& led : kLedUpgrades) {
    if (led.feature == feature) {
      return Unlocked(feature, character, account) &&
             !account.Seen(ActionLeadKey(led.slug));
    }
  }
  return false;
}

void FollowedToAction(Feature feature, AccountInstance& account) {
  for (const Led& led : kLedUpgrades) {
    if (led.feature == feature) {
      account.MarkSeen(ActionLeadKey(led.slug));
    }
  }
}

int HotkeysTipRetireLevel() {
  // The level the menu panel takes the corner over at. Derived rather than
  // written out, so the corner can never hold both or neither.
  return UnlockLevel(Feature::kMenu);
}

bool HotkeysTipVisible(const CharacterInstance& character,
                       const AccountInstance& account) {
  int level = std::max(character.proto().level(), account.max_level());
  return level < HotkeysTipRetireLevel();
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
