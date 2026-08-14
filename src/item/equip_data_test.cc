// Checks the shipped equip catalog rather than any one function. The model
// enforces whatever a prototype declares, which is exactly why the declaration
// itself needs pinning: an item added without it is an item the model happily
// lets the player upgrade.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

std::map<std::string, EquipPrototype> LoadEquips() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipPrototype>(runfiles->Rlocation("ms/data/equip"));
}

// Stars are ammunition, not a weapon a player invests in. Asserted over the
// whole catalog because the refusal has to be written on each one: nothing
// derives it from the type, deliberately, since a later star may well differ.
TEST(EquipDataTest, ThrowingStarsTakeNoUpgrades) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() != EQUIP_TYPE_THROWING_STAR) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " can be scrolled";
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " can be star forced";
    EXPECT_EQ(proto.upgrade_slots(), 0)
        << entry.first << " carries slots it will never spend";
  }
  EXPECT_GT(seen, 0) << "no throwing stars in the catalog to check";
}

// The refusal is the exception. A catalog where it spread to ordinary weapons
// would pass every check above and leave the player unable to upgrade anything.
TEST(EquipDataTest, OrdinaryWeaponsStillTakeUpgrades) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON) {
      continue;
    }
    EXPECT_TRUE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " cannot be scrolled";
    EXPECT_TRUE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " cannot be star forced";
    EXPECT_GT(proto.upgrade_slots(), 0)
        << entry.first << " has no slots to scroll";
  }
}

// A weapon type has one attack speed, and every weapon of it swings at that
// speed. GMS's own low-level items disagree among themselves -- the polearms
// range over three stages -- but by the level 150 tier, the one that matters,
// Nexon had settled each type on a single value. That value is what the
// catalog uses, all the way down.
TEST(EquipDataTest, AWeaponTypeHasOneAttackSpeed) {
  std::map<EquipType, std::pair<AttackSpeed, std::string>> speed_of_type;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() == EQUIP_TYPE_UNSPECIFIED ||
        proto.attack_speed() == ATTACK_SPEED_UNSPECIFIED) {
      continue;  // not a weapon, or ammunition that is never swung
    }
    std::map<EquipType, std::pair<AttackSpeed, std::string>>::iterator it =
        speed_of_type.find(proto.equip_type());
    if (it == speed_of_type.end()) {
      speed_of_type[proto.equip_type()] = {proto.attack_speed(), entry.first};
      continue;
    }
    EXPECT_EQ(proto.attack_speed(), it->second.first)
        << entry.first << " and " << it->second.second << " are both "
        << FormatEquipType(proto.equip_type())
        << " but swing at different speeds";
  }
}

// GMS gives none of these a slot or a star, and none of ours is near the level
// 200 tier where enhancement begins. Asserted over the catalog for the same
// reason the throwing stars are: each file has to say so itself.
TEST(EquipDataTest, SecondariesTakeNoUpgrades) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SECONDARY) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " can be scrolled";
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " can be star forced";
    EXPECT_EQ(proto.upgrade_slots(), 0)
        << entry.first << " carries slots it will never spend";
  }
  EXPECT_GT(seen, 0) << "no secondaries in the catalog to check";
}

// One per branch at every tier, and no branch left out. A missing one is a 2nd
// job with a level it cannot re-arm its off hand at.
TEST(EquipDataTest, EverySecondJobHasEveryTier) {
  const std::vector<int> kSecondaryTiers{30, 60, 100};
  std::map<JobAdvancement, std::vector<int>> levels;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SECONDARY) {
      continue;
    }
    JobAdvancement owner = AdvancementForSecondary(proto.equip_type());
    ASSERT_NE(owner, JOB_ADVANCEMENT_UNSPECIFIED)
        << entry.first << " is an off-hand nobody can hold";
    levels[owner].push_back(proto.required_level());
  }
  for (int i = JOB_ADVANCEMENT_FIGHTER; i <= JOB_ADVANCEMENT_BANDIT; ++i) {
    JobAdvancement advancement = static_cast<JobAdvancement>(i);
    std::vector<int>& own = levels[advancement];
    std::sort(own.begin(), own.end());
    EXPECT_EQ(own, kSecondaryTiers)
        << JobAdvancement_Name(advancement) << " has the wrong secondaries";
  }
}

// The priced levels of one weapon type, low to high.
std::map<EquipType, std::vector<int>> WeaponLadders() {
  std::map<EquipType, std::vector<int>> ladders;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
        proto.shop_price() <= 0) {
      continue;
    }
    ladders[proto.equip_type()].push_back(proto.required_level());
  }
  for (std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    std::sort(ladder.second.begin(), ladder.second.end());
  }
  return ladders;
}

// Every weapon type climbs in steps of ten with no gap and no tier holding two
// of a kind. A branch that skips a tier is one the player outgrows their weapon
// on and cannot re-arm.
TEST(EquipDataTest, EveryWeaponTypeClimbsInTens) {
  std::map<EquipType, std::vector<int>> ladders = WeaponLadders();
  ASSERT_FALSE(ladders.empty());
  for (const std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    std::vector<int> expected;
    for (int level = ladder.second.front(); level <= ladder.second.back();
         level += 10) {
      expected.push_back(level);
    }
    EXPECT_EQ(ladder.second, expected)
        << FormatEquipType(ladder.first) << " has a hole in its ladder";
  }
}

// Every ladder reaches the top of the game. The one-handed sword is the
// exception by design: the warrior takes two hands at their 2nd job, so it
// stops where the two-handed tiers start.
TEST(EquipDataTest, EveryWeaponTypeReachesTheLevelCap) {
  for (const std::pair<const EquipType, std::vector<int>>& ladder :
       WeaponLadders()) {
    int expected =
        ladder.first == EQUIP_TYPE_ONE_HANDED_SWORD ? 30 : kTrialLevelCap;
    EXPECT_EQ(ladder.second.back(), expected)
        << FormatEquipType(ladder.first) << " stops at the wrong tier";
  }
}

// One tier, one price. Every weapon a level opens costs the same, so the choice
// between branches is never a choice of what the player can afford -- and a
// mistyped price cannot hide among items nobody compares it with.
TEST(EquipDataTest, ATierHasOnePrice) {
  std::map<std::pair<EquipSlot, int>, std::pair<int, std::string>> price_of;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.shop_price() <= 0) {
      continue;
    }
    std::pair<EquipSlot, int> tier{proto.equip_slot(), proto.required_level()};
    if (price_of.find(tier) == price_of.end()) {
      price_of[tier] = {proto.shop_price(), entry.first};
      continue;
    }
    EXPECT_EQ(proto.shop_price(), price_of[tier].first)
        << entry.first << " and " << price_of[tier].second
        << " share a tier but not a price";
  }
}

// GMS buys equipment back at a few percent of what it charges, rising with the
// tier; a flat tenth sits inside that band the whole way. Pinned over the
// catalog because the failure it guards against is an item that pays more than
// it costs, which is not a mispriced item but a meso printer.
TEST(EquipDataTest, StockedEquipsSellForATenthOfTheirPrice) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.shop_price() <= 0) {
      // Not stocked, so there is no price to take a share of and the item
      // names its own. Most of them name nothing and sell for nothing -- that
      // is how a starter sword leaves the bag -- but a dropped piece is worth
      // what it is worth whether or not a shop ever sold one.
      continue;
    }
    ++seen;
    EXPECT_EQ(proto.sell_price(), proto.shop_price() / 10)
        << entry.first << " does not sell for a tenth of its price";
  }
  EXPECT_GT(seen, 0) << "no stocked equips in the catalog to check";
}

std::map<std::string, EquipSet> LoadSets() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipSet>(runfiles->Rlocation("ms/data/sets"));
}

// A set names its pieces by display name, and a name that matches nothing is a
// piece that can never be worn toward the bonus -- silently, because counting
// what is worn cannot tell a misspelling from an item nobody has found yet.
TEST(EquipDataTest, EverySetMemberIsAnItemThatExists) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  int checked = 0;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    EXPECT_FALSE(entry.second.name().empty()) << entry.first << " is unnamed";
    for (const std::string& member : entry.second.members()) {
      ++checked;
      bool found = false;
      for (const std::pair<const std::string, EquipPrototype>& equip : equips) {
        if (equip.second.name() == member) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << entry.first << " counts \"" << member
                         << "\", which no equip file defines";
    }
  }
  EXPECT_GT(checked, 0) << "no sets in the catalog to check";
}

// Tiers are read as "at least this many pieces", so one asking for more pieces
// than the set has is a bonus nobody can reach, and one asking for none pays
// everybody. Both are data mistakes rather than states the model handles.
TEST(EquipDataTest, EverySetTierIsReachable) {
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    for (const EquipSetTier& tier : entry.second.tiers()) {
      EXPECT_GT(tier.pieces(), 1)
          << entry.first << " pays a tier for wearing one piece";
      EXPECT_LE(tier.pieces(), 6)
          << entry.first << " has a tier past the six a set can hold";
    }
  }
}

// A slot or a type added without a display name shows up as a blank column in
// the bag, which reads as a broken item rather than a missing label.
TEST(EquipDataTest, EveryItemsSlotAndTypeHaveNames) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    EXPECT_FALSE(FormatSlot(proto.equip_slot()).empty())
        << entry.first << " wears in an unnamed slot";
    if (proto.equip_type() != EQUIP_TYPE_UNSPECIFIED) {
      EXPECT_FALSE(FormatEquipType(proto.equip_type()).empty())
          << entry.first << " is an unnamed kind of item";
    }
  }
}

}  // namespace
}  // namespace ms
