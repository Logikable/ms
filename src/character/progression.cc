#include "src/character/progression.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/account.h"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/hyper_stats.h"
#include "src/character/link.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

struct Unlock {
  Feature feature;
  int level;
};

// The early game in one place, in the order the player sees it.
constexpr Unlock kUnlocks[] = {
    // Level 2 unlocks nothing: the first level-up gives AP, and that is its
    // whole lesson. Panels start appearing at 3.
    {Feature::kEquipped, 3},
    {Feature::kBag, 4},
    // Deliberately the same level as the bag: the bag is what makes taking
    // something off possible, so move both together if either changes.
    {Feature::kUnequip, 4},
    // One level after the bag, the last panel the hotkeys tip covers: the tip
    // leaves the corner and the menu replaces it.
    {Feature::kMenu, 5},
    {Feature::kSkills, 10},
    // The same level as skills: a character with a skill book is worth showing
    // in the lobby, and has something to trade.
    {Feature::kMultiplayer, 10},
    {Feature::kShop, 20},
    // Late enough that the player sees it after the early game, and late enough
    // that meso for spell traces is coming in.
    {Feature::kScrolling, 40},
    // Zakum's level, the first boss. Opening it earlier would only show the
    // player fights they can't take.
    {Feature::kBoss, 110},
    // Matched to the gear it's for: the Frozen weapons at 120 are the first
    // that take 15 stars, and opening the screen earlier only offers expensive
    // stars that are barely worth it.
    {Feature::kStarForce, 120},
    // Well past the gear a player scrolls early on: a hammer costs 10 million
    // meso for one slot, which is only worth it on an item they mean to keep.
    {Feature::kHammer, 150},
    // Potential's own level; see kPotentialUnlockLevel. Well after the hammer:
    // a cube is only worth using on gear the player won't replace soon.
    {Feature::kPotential, kPotentialUnlockLevel},
    // Hyper Stats' own level, where the points start; see
    // kHyperStatUnlockLevel.
    {Feature::kHyperStats, kHyperStatUnlockLevel},
    // The Wealth Acquisition Potion's level; see kConsumableUnlockLevel. The
    // second buff waits until 190 and isn't listed at all before then.
    {Feature::kConsumables, kConsumableUnlockLevel},
    // Arcane River opens at 200 and starts with a free symbol, so the tab has
    // something in it when it appears.
    {Feature::kSymbols, 200},
    // Cubing's level: the presets arrive along with the reason to keep two sets
    // of gear. See Feature::kEquipPresets.
    {Feature::kEquipPresets, kPotentialUnlockLevel},
    // Ten levels after Arcane River opens. A second character is what a player
    // wants once the first has most of what the game offers.
    {Feature::kCharacters, 210},
    // The same level as Characters; see Feature::kBank.
    {Feature::kBank, 210},
    // The last link skill threshold. Only the trail waits for it: the row and
    // the skills are available from level 1.
    {Feature::kLinkSkills, kLinkSkillsLevel},
};

// Features unlocked by an advancement instead of a level, and which advancement
// unlocks each. A separate table because level isn't the gate here: a Beginner
// who never advances stays locked however high they level.
struct StageUnlock {
  Feature feature;
  int stage;
};

// Attack, Magic Attack, Attack Speed and Defense appear with the 1st job, the
// first thing that changes them. The percent rows wait for the 2nd, whose
// passives give crit and damage, and the last three for the 3rd, which has the
// skills that set them.
constexpr StageUnlock kStageUnlocks[] = {
    {Feature::kCombatStats, 1},
    {Feature::kDamageStats, 2},
    {Feature::kAdvancedStats, 3},
};

// The upgrades levels unlock, in unlock order. One list, instead of one
// condition in the card and another in the menus, so a new upgrade is added to
// both at once.
constexpr Feature kUpgrades[] = {
    Feature::kScrolling,
    Feature::kStarForce,
    Feature::kHammer,
    Feature::kPotential,
};

// The upgrades with a gold trail, and the name their record keys are built
// from. The names are written into saves, so changing one forgets that anyone
// was led anywhere and restarts every player's trail.
struct Led {
  Feature feature;
  const char* slug;
  // Whether the trail starts at the equipped weapon's name. Scrolling unlocks
  // before the player may ever have opened the item menu, so it needs the
  // marker; star force unlocks at 120, by which point the marker would only
  // distract.
  bool from_weapon;
};

constexpr Led kLedUpgrades[] = {
    {Feature::kScrolling, "scrolling", true},
    {Feature::kStarForce, "star_force", false},
    {Feature::kHammer, "hammer", false},
    {Feature::kPotential, "potential", false},
};

std::string WeaponLeadKey(const char* slug) {
  return std::string("lead_weapon:") + slug;
}

std::string ActionLeadKey(const char* slug) {
  return std::string("lead_action:") + slug;
}

// The Link Skills trail, in the order it's followed.
constexpr const char* kLinkTrailSlugs[] = {"tab", "page", "row"};

// The lowest level of each pacing band and its multiplier. Read from the bottom
// up: the last band the level reaches applies.
struct Speed {
  int level;
  double factor;
};

constexpr Speed kSpeeds[] = {
    {1, 2.0},   {10, 3.0},  {30, 4.0},  {60, 5.0},
    {100, 6.0}, {140, 7.0}, {200, 8.0}, {230, 10.0},
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
      // The level its advancement is offered at, the earliest it can unlock.
      // Whether it has unlocked depends on the character, not the level.
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
  if (feature == Feature::kHyperStats) {
    // Checked against this character's level: the points come from their own
    // levels, so an account-wide unlock would show a new character fourteen
    // rows with nothing to spend.
    return character.proto().level() >= UnlockLevel(feature);
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
    case Feature::kHammer:
      return "the Golden Hammer";
    case Feature::kPotential:
      return "Potential";
    case Feature::kSkills:
      return "Skills";
    case Feature::kShop:
      return "the Shop";
    case Feature::kMenu:
      return "the Menu";
    case Feature::kBoss:
      return "Bosses";
    case Feature::kMultiplayer:
      return "Multiplayer";
    case Feature::kBank:
      return "Bank";
    case Feature::kCharacters:
      return "Characters";
    case Feature::kHyperStats:
      return "Hyper Stats";
    case Feature::kConsumables:
      return "Buffs";
    case Feature::kSymbols:
      return "Arcane Symbols";
    case Feature::kEquipPresets:
      return "Equip Presets";
    case Feature::kLinkSkills:
      return "Link Skills";
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

std::string LinkTrailKey(LinkTrailStep step) {
  return std::string("lead_link:") + kLinkTrailSlugs[static_cast<int>(step)];
}

bool LeadToLinkSkills(LinkTrailStep step, const CharacterInstance& character,
                      const AccountInstance& account) {
  return Unlocked(Feature::kLinkSkills, character, account) &&
         !account.Seen(LinkTrailKey(step));
}

void FollowedToLinkSkills(LinkTrailStep step, AccountInstance& account) {
  account.MarkSeen(LinkTrailKey(step));
}

int HotkeysTipRetireLevel() {
  // The level where the menu panel replaces the tip in the corner. Derived
  // instead of written out, so the corner can never show both or neither.
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
