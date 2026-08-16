// Checks the shipped equip catalog rather than any one function. The model
// enforces whatever a prototype declares, which is exactly why the declaration
// itself needs pinning: an item added without it is an item the model happily
// lets the player upgrade.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "src/character/character.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"
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

std::map<std::string, ItemPrototype> LoadItems() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<ItemPrototype>(runfiles->Rlocation("ms/data/items"));
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

// Every job the advancement picker can offer, gathered the way it gathers
// them: stage 1 from a Beginner, then the next stage of each job that gave.
std::vector<Job> EveryOfferedJob() {
  std::vector<Job> jobs;
  std::vector<Job> frontier = {JOB_BEGINNER};
  for (int stage = 1; !frontier.empty(); ++stage) {
    std::vector<Job> next;
    for (Job job : frontier) {
      for (Job choice : JobChoicesForStage(job, stage)) {
        jobs.push_back(choice);
        next.push_back(choice);
      }
    }
    frontier = next;
  }
  return jobs;
}

// The job inspect screen tells a player what to go and buy, so no job may name
// a weapon of somebody else's branch, and every job's row must point at
// something buyable. A named type nothing ships yet -- the one-handed axe and
// blunt, which both warrior books name and no item is -- proves neither, and
// is skipped rather than failed. Levelled past every requirement, since what is
// under test is the job and not the tier.
TEST(EquipDataTest, EveryJobOnOfferNamesWeaponsOfItsOwnBranch) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<Job> offered = EveryOfferedJob();
  ASSERT_GE(offered.size(), 20u);  // 4 + 9 + 9 as the game stands
  for (Job job : offered) {
    std::vector<EquipType> weapons = ExpectedWeapons(job);
    EXPECT_FALSE(weapons.empty()) << Job_Name(job);
    std::set<EquipType> named(weapons.begin(), weapons.end());
    std::mt19937 rng(0);
    Character proto;
    proto.set_job(job);
    proto.set_level(200);
    CharacterInstance character(rng, std::move(proto));

    int holdable = 0;
    for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
      const EquipPrototype& p = entry.second;
      if (p.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
          named.count(p.equip_type()) == 0) {
        continue;
      }
      EXPECT_TRUE(character.CanEquip(p))
          << Job_Name(job) << " names " << FormatEquipType(p.equip_type())
          << ", which it cannot hold: " << entry.first;
      ++holdable;
    }
    EXPECT_GT(holdable, 0) << Job_Name(job) << " names nothing that ships";
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
// job with a level it cannot re-arm its off hand at. The two shelves are
// counted apart: what meso buys climbs in tiers, and what a token buys is the
// one Frozen piece.
TEST(EquipDataTest, EverySecondJobHasEveryTier) {
  const std::vector<int> kMesoTiers{30, 60, 100};
  const std::vector<int> kTokenTiers{120};
  std::map<JobAdvancement, std::vector<int>> meso;
  std::map<JobAdvancement, std::vector<int>> token;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SECONDARY) {
      continue;
    }
    JobAdvancement owner = AdvancementForSecondary(proto.equip_type());
    ASSERT_NE(owner, JOB_ADVANCEMENT_UNSPECIFIED)
        << entry.first << " is an off-hand nobody can hold";
    ASSERT_GT(proto.shop_price() + proto.token_price(), 0)
        << entry.first << " is an off-hand nothing buys";
    if (proto.shop_price() > 0) {
      meso[owner].push_back(proto.required_level());
    } else {
      token[owner].push_back(proto.required_level());
    }
  }
  for (int i = JOB_ADVANCEMENT_FIGHTER; i <= JOB_ADVANCEMENT_BANDIT; ++i) {
    JobAdvancement advancement = static_cast<JobAdvancement>(i);
    std::vector<int>& own = meso[advancement];
    std::sort(own.begin(), own.end());
    EXPECT_EQ(own, kMesoTiers)
        << JobAdvancement_Name(advancement) << " has the wrong secondaries";
    EXPECT_EQ(token[advancement], kTokenTiers)
        << JobAdvancement_Name(advancement)
        << " has the wrong Frozen secondary";
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

// Every ladder reaches the top of the shelf, so no branch is left a tier short
// of what the others can buy. Asked against the highest tier there is rather
// than against a level written here: the Frozen tier a token buys sits above
// all of these, and the meso ladders stopping short of it is the content gap,
// not a fault in one of them. The one-handed sword is the exception by design:
// the warrior takes two hands at their 2nd job, so it stops where the
// two-handed tiers start.
TEST(EquipDataTest, EveryWeaponTypeReachesTheTopMesoTier) {
  std::map<EquipType, std::vector<int>> ladders = WeaponLadders();
  ASSERT_FALSE(ladders.empty());
  int top = 0;
  for (const std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    top = std::max(top, ladder.second.back());
  }
  for (const std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    int expected = ladder.first == EQUIP_TYPE_ONE_HANDED_SWORD ? 30 : top;
    EXPECT_EQ(ladder.second.back(), expected)
        << FormatEquipType(ladder.first) << " stops at the wrong tier";
  }
}

// Every weapon type the shop's ladder reaches the cap with has a Frozen one
// above it, so no branch is asked to farm tokens for a weapon it cannot hold.
// The one-handed sword is out for the reason its ladder stops: nobody swings
// one past their 2nd job.
TEST(EquipDataTest, EveryWeaponTypeHasAFrozenTier) {
  std::map<EquipType, int> frozen;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON &&
        proto.token_price() > 0) {
      EXPECT_EQ(frozen.count(proto.equip_type()), 0u)
          << entry.first << " is a second Frozen "
          << FormatEquipType(proto.equip_type());
      frozen[proto.equip_type()] = proto.required_level();
    }
  }
  for (const std::pair<const EquipType, std::vector<int>>& ladder :
       WeaponLadders()) {
    if (ladder.first == EQUIP_TYPE_ONE_HANDED_SWORD) {
      continue;
    }
    ASSERT_EQ(frozen.count(ladder.first), 1u)
        << FormatEquipType(ladder.first) << " has no Frozen tier";
    EXPECT_EQ(frozen[ladder.first], 120)
        << "the Frozen " << FormatEquipType(ladder.first)
        << " is worn at the wrong level";
  }
}

// A price is only a price if something answers it. An item naming a token that
// no data file defines would sit on the shelf at a cost nobody can pay, and the
// loader would say nothing.
TEST(EquipDataTest, EveryTokenPriceNamesATokenThatExists) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.token_price() <= 0 && proto.token_item().empty()) {
      continue;
    }
    ++seen;
    EXPECT_GT(proto.token_price(), 0) << entry.first << " names a token for 0";
    std::map<std::string, ItemPrototype>::const_iterator it =
        items.find(proto.token_item());
    ASSERT_NE(it, items.end())
        << entry.first << " is bought with " << proto.token_item()
        << ", which is not an item";
    EXPECT_FALSE(it->second.currency_mark().empty())
        << proto.token_item() << " pays for " << entry.first
        << " without a mark to draw in the cost column";
    EXPECT_EQ(proto.shop_price(), 0)
        << entry.first << " is on both shelves at once";
  }
  EXPECT_GT(seen, 0) << "nothing in the catalog is bought with a token";
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
    EXPECT_FALSE(FormatEquipSet(entry.second.name()).empty())
        << entry.first << " is an unnamed set";
    std::set<EquipSlot> slots;
    for (const EquipSetMember& member : entry.second.members()) {
      EXPECT_FALSE(FormatSlot(member.slot()).empty())
          << entry.first << " has a member in an unnamed slot";
      EXPECT_TRUE(slots.insert(member.slot()).second)
          << entry.first << " fills " << FormatSlot(member.slot()) << " twice";
      // One item or a family of them, never both and never neither: the
      // inspect screen prints one column from whichever it is, and a member
      // that is both would count a family toward the tiers.
      EXPECT_NE(member.name().empty(), member.family().empty())
          << entry.first << " has a member that is not one item or one family";
      if (member.name().empty()) {
        continue;
      }
      ++checked;
      bool found = false;
      for (const std::pair<const std::string, EquipPrototype>& equip : equips) {
        if (equip.second.name() == member.name()) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << entry.first << " counts \"" << member.name()
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
      EXPECT_LE(tier.pieces(), entry.second.members_size())
          << entry.first << " has a tier past the pieces the set holds";
    }
  }
}

// The levers the inspect screen's set card writes a row for. A tier that pulls
// one outside this list pays the player a bonus nothing tells them about, so
// the card and this list move together -- see InspectPanel::EffectLines.
const char* const kShownLevers[] = {
    "str",        "dex",          "int",        "luk",        "def",
    "attack",     "magic_attack", "attack_pct", "max_hp_pct", "max_mp_pct",
    "damage_pct", "boss_pct",     "ied_pct",    "crit_rate",  "crit_dmg",
    "meso_pct",   "exp_pct",
};

TEST(EquipDataTest, EverySetTierLeverHasARowOnTheInspectScreen) {
  std::set<std::string> shown(std::begin(kShownLevers), std::end(kShownLevers));
  int checked = 0;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    for (const EquipSetTier& tier : entry.second.tiers()) {
      std::vector<const google::protobuf::FieldDescriptor*> fields;
      tier.effect().GetReflection()->ListFields(tier.effect(), &fields);
      EXPECT_FALSE(fields.empty())
          << entry.first << " has a tier that pays nothing";
      for (const google::protobuf::FieldDescriptor* field : fields) {
        ++checked;
        EXPECT_TRUE(shown.count(std::string(field->name())) > 0)
            << entry.first << " pays " << field->name()
            << ", which the inspect screen has no row for";
      }
    }
  }
  EXPECT_GT(checked, 0) << "no set tiers in the catalog to check";
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
